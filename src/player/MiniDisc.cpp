#include "MiniDisc.h"
#include "Messages.h"
#include "MiniDiscPcm.h"
#include "NetMDUsb.h"
#include "core/NetMDSimulator.h"
#include <FindDirectory.h>
#include <Path.h>
#include <OS.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace amp {

namespace {

const int kPollSeconds = 4;
const char* kSimulatorPath = "simulator";
const bigtime_t kProgressInterval = 150000; // at most this often, in microseconds
// One USB transfer that has not finished after this long is stuck: a recorder whose battery runs
// low stops taking data but stays on the bus, and usb_raw waits for a transfer without a limit
// (and holds the device for it). A full 64 KB chunk normally goes in well under a second, and
// the recorder's pauses to write its buffer to the disc take a few seconds at most.
const bigtime_t kStallTimeout = 30000000;
// What the user is told when that happens.
const char* kStalledError = "The MiniDisc recorder stopped taking data and does not answer any more. "
    "A portable recorder does this when its battery runs low.\n\n"
    "Charge it or put in a fresh battery, then disconnect the recorder and connect it again.";
const char* kStalledState = "The recorder stopped responding. Disconnect it and connect it again.";

using Clock = std::chrono::steady_clock;

bool SameDisc(const netmd::DiscInfo& a, const netmd::DiscInfo& b)
{
    return a.present == b.present && a.rawTitle == b.rawTitle && a.trackCount == b.trackCount
        && a.usedFrames == b.usedFrames && a.leftFrames == b.leftFrames && a.totalFrames == b.totalFrames
        && a.writable == b.writable && a.writeProtected == b.writeProtected;
}

// A problem the user can act on, reported as is.
struct UserError {
    std::string message;
};

std::string DescribeError(const netmd::Error& error)
{
    switch (error.kind()) {
        case netmd::Error::kIo:
            return "The MiniDisc recorder stopped responding. Check its cable and its battery or power adapter, "
                "reconnect it (another USB port may help) and try again.";
        case netmd::Error::kTimeout:
            return "The MiniDisc recorder took too long to answer.";
        case netmd::Error::kRejected:
            return std::string("The MiniDisc recorder refused the request (") + error.what() + ").";
        case netmd::Error::kNotImplemented:
            return "The MiniDisc recorder does not support this.";
        default:
            return std::string("Unexpected answer from the MiniDisc recorder: ") + error.what();
    }
}

// Converts the songs of a job to PCM files ahead of the transfer: while one song is going to
// the recorder the next is prepared. At most two finished files wait on disk.
class ConversionQueue {
public:
    struct Result {
        bool done = false;
        bool ok = false;
        std::string path;
        std::string error;
        int64_t bytes = 0;
    };

    ConversionQueue(const std::vector<MiniDiscWriteItem>& items, std::atomic<bool>& cancel)
        : fItems(items), fResults(items.size()), fCancel(cancel)
    {
        BPath temp;
        if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &temp) != B_OK)
            temp.SetTo("/tmp");
        fDirectory = temp.Path();
        fThread = std::thread([this] { Run(); });
    }

    ~ConversionQueue()
    {
        {
            std::lock_guard<std::mutex> guard(fLock);
            fStop = true;
        }
        fChanged.notify_all();
        fThread.join();
        for (Result& result : fResults)
            if (result.ok && !result.path.empty())
                remove(result.path.c_str());
    }

    // Blocks until song `index` is converted; `waiting` gets the conversion progress meanwhile.
    Result Wait(size_t index, const std::function<void(float)>& waiting)
    {
        std::unique_lock<std::mutex> guard(fLock);
        while (!fResults[index].done) {
            if (waiting && fConverting == index) {
                float fraction = fFraction;
                guard.unlock();
                waiting(fraction);
                guard.lock();
            }
            fChanged.wait_for(guard, std::chrono::milliseconds(200));
        }
        return fResults[index];
    }

    // The transfer of song `index` is over: its file goes, and the converter may move on.
    void Consumed(size_t index)
    {
        std::lock_guard<std::mutex> guard(fLock);
        if (fResults[index].ok && !fResults[index].path.empty())
            remove(fResults[index].path.c_str());
        fResults[index].path.clear();
        fConsumed = index + 1;
        fChanged.notify_all();
    }

private:
    void Run()
    {
        for (size_t i = 0; i < fItems.size(); i++) {
            {
                std::unique_lock<std::mutex> guard(fLock);
                fChanged.wait(guard, [&] { return fStop || i < fConsumed + 2; });
                if (fStop)
                    return;
                fConverting = i;
                fFraction = 0;
            }
            char name[64];
            snprintf(name, sizeof(name), "/amp-minidisc-%d-%zu.pcm", (int)getpid(), i);
            Result result;
            result.path = fDirectory + name;
            result.bytes = ConvertToMiniDiscPcm(fItems[i].path, result.path, [this](float fraction) {
                std::lock_guard<std::mutex> guard(fLock);
                fFraction = fraction;
                return !fStop && !fCancel;
            }, result.error);
            result.ok = result.bytes > 0;
            result.done = true;
            if (!result.ok)
                result.path.clear();
            std::lock_guard<std::mutex> guard(fLock);
            fResults[i] = result;
            fChanged.notify_all();
            if (fStop)
                return;
        }
    }

    std::vector<MiniDiscWriteItem> fItems;
    std::vector<Result> fResults;
    std::atomic<bool>& fCancel;
    std::string fDirectory;
    std::mutex fLock;
    std::condition_variable fChanged;
    size_t fConsumed = 0;
    size_t fConverting = (size_t)-1;
    float fFraction = 0;
    bool fStop = false;
    std::thread fThread;
};

} // namespace

struct MiniDiscManager::Shared {
    std::mutex lock;
    std::condition_variable wake;
    BMessenger target;
    bool quit = false;
    // requests for the worker
    bool refresh = false;
    bool refreshContents = false;
    bool erase = false;
    std::unique_ptr<MiniDiscJob> job;
    // the recorder on the bus
    NetMDDeviceId device;
    bool present = false;
    bool deviceChanged = false;
    MiniDiscState state;
    std::atomic<bool> busy{false};
    std::atomic<bool> cancel{false};
    bool exited = false;
    std::condition_variable exit;
    // The watchdog's view of the worker: its USB call in progress (Watch), and what it is doing.
    struct Watch {
        // when the call in progress began; 0: none, -1: the watchdog gave up on it
        std::atomic<bigtime_t> since{0};
    };
    std::shared_ptr<Watch> watch;
    bool needsReconnect = false;   // a worker was given up on: wait until the recorder is free again
    bool eraseRunning = false;
    // the running write: its songs and what became of each (kItem*), for a report and a retry
    std::unique_ptr<MiniDiscJob> running;
    std::vector<int> itemStatus;
    int64_t writtenMs = 0;
    std::unique_ptr<MiniDiscJob> retry;   // the songs a failed write did not get to
    bool watchdogQuit = false;

    void Post(BMessage& message) { target.SendMessage(&message); }
    void PostState()
    {
        BMessage message(kMsgMDState);
        Post(message);
    }
    bool JobPending()
    {
        std::lock_guard<std::mutex> guard(lock);
        return job != nullptr || quit;
    }
};

namespace {

using Shared = MiniDiscManager::Shared;

enum { kItemPending = 0, kItemWritten, kItemSkipped };

// Thrown in a worker the watchdog gave up on, when its stuck call finally returns: the worker
// unwinds without touching the shared state, which belongs to its replacement by then.
struct WorkerAbandoned {};

// The transport as a worker uses it: every USB call is timed for the watchdog.
class WatchedTransport : public netmd::Transport {
public:
    WatchedTransport(netmd::Transport& inner, Shared& shared, std::shared_ptr<Shared::Watch> watch)
        : fInner(inner), fShared(shared), fWatch(std::move(watch))
    {
    }
    ssize_t VendorIn(uint8_t request, void* data, size_t length) override
    {
        Begin();
        ssize_t result = fInner.VendorIn(request, data, length);
        End();
        return result;
    }
    ssize_t VendorOut(uint8_t request, const void* data, size_t length) override
    {
        Begin();
        ssize_t result = fInner.VendorOut(request, data, length);
        End();
        return result;
    }
    ssize_t BulkOut(const void* data, size_t length) override
    {
        Begin();
        ssize_t result = fInner.BulkOut(data, length);
        End();
        return result;
    }
    void Sleep(int milliseconds) override { fInner.Sleep(milliseconds); }

private:
    void Begin()
    {
        bigtime_t idle = 0;
        if (!fWatch->since.compare_exchange_strong(idle, system_time()))
            throw WorkerAbandoned();
    }
    void End()
    {
        if (fWatch->since.exchange(0) != -1)
            return;
        fWatch->since = -1;
        // the stuck call is over (the recorder was unplugged, or answered after all): the
        // device is free for the worker that took over
        {
            std::lock_guard<std::mutex> guard(fShared.lock);
            fShared.needsReconnect = false;
            fShared.deviceChanged = true;
        }
        fShared.wake.notify_all();
        throw WorkerAbandoned();
    }

    netmd::Transport& fInner;
    Shared& fShared;
    std::shared_ptr<Shared::Watch> fWatch;
};

// The songs of `job` a write did not get to, as a job of their own that adds them to the disc.
std::unique_ptr<MiniDiscJob> RemainingJob(const MiniDiscJob& job, const std::vector<int>& status)
{
    std::unique_ptr<MiniDiscJob> rest(new MiniDiscJob);
    rest->name = job.name;
    for (size_t i = 0; i < job.items.size() && i < status.size(); i++)
        if (status[i] == kItemPending)
            rest->items.push_back(job.items[i]);
    if (rest->items.empty())
        rest.reset();
    return rest;
}

void AddRemaining(BMessage& message, const std::vector<int>& status)
{
    for (size_t i = 0; i < status.size(); i++)
        if (status[i] == kItemPending)
            message.AddInt32("remaining", (int32)i);
}

void ReadState(Shared& shared, netmd::Device& device, bool contents)
{
    netmd::DiscInfo info;
    std::string error;
    bool changed = false;
    try {
        info = device.ReadDiscInfo();
        bool known, hadContents;
        netmd::DiscInfo previous;
        {
            std::lock_guard<std::mutex> guard(shared.lock);
            known = shared.state.known;
            hadContents = shared.state.contentsKnown;
            previous = shared.state.disc;
        }
        changed = !known || !SameDisc(previous, info);
        if (info.present && (contents || changed || !hadContents))
            device.ReadContents(info, [&] { return shared.JobPending(); });
        else
            info.tracks = previous.tracks;
    } catch (const netmd::Error& e) {
        error = DescribeError(e);
    }
    {
        std::lock_guard<std::mutex> guard(shared.lock);
        if (!shared.present)
            return;
        if (error.empty()) {
            changed = changed || !shared.state.error.empty() || shared.state.contentsKnown != ((int)info.tracks.size() == info.trackCount);
            shared.state.known = true;
            shared.state.disc = info;
            shared.state.contentsKnown = (int)info.tracks.size() == info.trackCount;
            shared.state.error.clear();
        } else {
            changed = shared.state.error != error;
            shared.state.error = error;
        }
    }
    if (changed)
        shared.PostState();
}

void PostProgress(Shared& shared, const MiniDiscJob& job, int phase, int track, const std::string& title,
    float fraction, float trackFraction, int eta)
{
    BMessage message(kMsgMDProgress);
    message.AddInt32("phase", phase);
    message.AddInt32("track", track);
    message.AddInt32("count", (int32)job.items.size());
    message.AddString("title", title.c_str());
    message.AddString("name", job.name.c_str());
    message.AddFloat("fraction", fraction);
    message.AddFloat("track_fraction", trackFraction);
    message.AddInt32("eta", eta);
    message.AddBool("cancelling", shared.cancel);
    shared.Post(message);
}

void RunJob(Shared& shared, netmd::Device& device, MiniDiscJob& job)
{
    std::string error;
    int written = 0, failed = 0;
    int64_t writtenMs = 0;
    std::string firstFailure;
    bool full = false;
    // progress is measured in PCM bytes: estimated from the durations, exact once converted
    std::vector<uint64_t> sizes;
    uint64_t totalBytes = 0;
    for (const MiniDiscWriteItem& item : job.items) {
        sizes.push_back(netmd::PcmBytesForDuration(item.durationMs));
        totalBytes += sizes.back();
    }
    uint64_t doneBytes = 0;
    uint64_t sentBytes = 0;
    double sendSeconds = 0;
    bigtime_t lastPost = 0;
    auto eta = [&]() -> int {
        if (sendSeconds < 4 || sentBytes == 0)
            return -1;
        double rate = sentBytes / sendSeconds;
        return (int)((totalBytes > doneBytes ? totalBytes - doneBytes : 0) / rate);
    };
    auto overall = [&](uint64_t extra) {
        return totalBytes ? std::min(1.0f, (float)(doneBytes + extra) / (float)totalBytes) : 0.0f;
    };

    {
        std::lock_guard<std::mutex> guard(shared.lock);
        shared.running.reset(new MiniDiscJob(job));
        shared.itemStatus.assign(job.items.size(), kItemPending);
        shared.writtenMs = 0;
        shared.retry.reset();
    }
    auto mark = [&](size_t index, int status) {
        std::lock_guard<std::mutex> guard(shared.lock);
        shared.itemStatus[index] = status;
        shared.writtenMs = writtenMs;
    };

    // the song being sent: a refusal then is about recording it
    int sending = -1;
    std::string sendingTitle;

    PostProgress(shared, job, kMDPreparing, 0, "", 0, 0, -1);
    std::unique_ptr<ConversionQueue> conversions(new ConversionQueue(job.items, shared.cancel));
    try {
        device.Flush();
        netmd::DiscInfo info = device.ReadDiscInfo();
        if (!info.present)
            throw UserError{"There is no MiniDisc in the recorder."};
        if (info.writeProtected)
            throw UserError{"The MiniDisc is write-protected. Slide its record tab closed and try again."};
        if (!info.writable)
            throw UserError{"This MiniDisc cannot be recorded on."};
        if (job.erase) {
            PostProgress(shared, job, kMDErasing, 0, "", 0, 0, -1);
            device.EraseDisc();
            info = device.ReadDiscInfo();
        } else
            device.ReadContents(info);

        // titles share 255 cells of the TOC with the disc title and the songs already there
        std::string discTitle = job.discTitle.empty() ? info.rawTitle : job.discTitle;
        int cells = netmd::kTitleCells - netmd::TitleCells(discTitle);
        for (const netmd::TrackInfo& track : info.tracks)
            cells -= netmd::TitleCells(track.title);
        std::vector<std::string> titles;
        for (const MiniDiscWriteItem& item : job.items)
            titles.push_back(item.title);
        titles = netmd::FitTitles(titles, cells);
        if (!job.discTitle.empty() && job.discTitle != info.rawTitle) {
            try {
                device.SetDiscTitle(job.discTitle);
            } catch (const netmd::Error& e) {
                if (e.kind() == netmd::Error::kIo)
                    throw;
            }
        }

        int64_t leftFrames = info.leftFrames;
        for (size_t i = 0; i < job.items.size(); i++) {
            if (shared.cancel)
                break;
            const MiniDiscWriteItem& item = job.items[i];
            ConversionQueue::Result converted = conversions->Wait(i, [&](float fraction) {
                PostProgress(shared, job, kMDConverting, (int)i + 1, item.title, overall(0), fraction, eta());
            });
            if (!converted.ok) {
                if (shared.cancel)
                    break;
                failed++;
                if (firstFailure.empty())
                    firstFailure = item.path + ": " + converted.error;
                totalBytes -= std::min(totalBytes, sizes[i]);
                mark(i, kItemSkipped);
                continue;
            }
            // the real size replaces the estimate
            totalBytes = totalBytes - std::min(totalBytes, sizes[i]) + (uint64_t)converted.bytes;
            sizes[i] = (uint64_t)converted.bytes;
            int64_t durationMs = converted.bytes * 1000 / netmd::kPcmBytesPerSecond;
            if (netmd::SPFramesForDuration(durationMs) > leftFrames) {
                full = true;
                break;
            }
            PcmFileSource source(converted.path);
            if (!source.IsOpen()) {
                failed++;
                mark(i, kItemSkipped);
                continue;
            }
            auto started = Clock::now();
            uint64_t trackSent = 0;
            PostProgress(shared, job, kMDWriting, (int)i + 1, titles[i], overall(0), 0, eta());
            sending = (int)i;
            sendingTitle = item.title;
            netmd::DownloadTrack(device, source, (uint64_t)converted.bytes, titles[i],
                [&](const netmd::DownloadProgress& progress) {
                    trackSent = progress.sent;
                    bigtime_t now = system_time();
                    if (now - lastPost < kProgressInterval)
                        return;
                    lastPost = now;
                    double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
                    uint64_t sent = sentBytes + std::min<uint64_t>(progress.sent, (uint64_t)converted.bytes);
                    int remaining = -1;
                    if (sendSeconds + elapsed >= 4 && sent > 0) {
                        double rate = sent / (sendSeconds + elapsed);
                        uint64_t done = doneBytes + std::min<uint64_t>(progress.sent, (uint64_t)converted.bytes);
                        remaining = (int)((totalBytes > done ? totalBytes - done : 0) / rate);
                    }
                    PostProgress(shared, job, kMDWriting, (int)i + 1, titles[i],
                        overall(std::min<uint64_t>(progress.sent, (uint64_t)converted.bytes)),
                        progress.total ? (float)progress.sent / progress.total : 0, remaining);
                });
            sendSeconds += std::chrono::duration<double>(Clock::now() - started).count();
            sentBytes += std::min<uint64_t>(trackSent, (uint64_t)converted.bytes);
            doneBytes += (uint64_t)converted.bytes;
            sending = -1;
            conversions->Consumed(i);
            written++;
            writtenMs += durationMs;
            mark(i, kItemWritten);
            int64_t used, total;
            device.Capacity(used, total, leftFrames);
        }
        PostProgress(shared, job, kMDFinishing, (int)job.items.size(), "", 1, 1, -1);
    } catch (const UserError& e) {
        error = e.message;
    } catch (const netmd::Error& e) {
        error = DescribeError(e);
        netmd::Recover(device);
        if (e.kind() == netmd::Error::kRejected) {
            // the usual reasons, which the recorder does not name
            try {
                netmd::DiscInfo now = device.ReadDiscInfo();
                if (!now.present)
                    error = "The MiniDisc was taken out of the recorder.";
                else if (now.writeProtected)
                    error = "The MiniDisc is write-protected. Slide its record tab closed and try again.";
                else if (now.leftFrames < netmd::kFramesPerSecond * 2)
                    error = "The MiniDisc is full.";
                else if (sending >= 0 && std::string(e.what()).find("f003010328") != std::string::npos) {
                    // there is a disc, it takes recordings and the song fits by the recorder's
                    // own count: a portable refuses to record when its battery is too low for it
                    error = "The MiniDisc recorder refused to start recording “" + sendingTitle + "” (song "
                        + std::to_string(sending + 1) + " of " + std::to_string(job.items.size())
                        + "). A portable recorder does this when its battery is too low to record.\n\n"
                        "Charge it or put in a fresh battery, then try again.";
                }
            } catch (const netmd::Error&) {
            }
        }
    }
    conversions.reset();

    BMessage finished(kMsgMDFinished);
    finished.AddString("name", job.name.c_str());
    finished.AddBool("ok", error.empty());
    finished.AddString("error", error.c_str());
    finished.AddBool("cancelled", (bool)shared.cancel);
    finished.AddBool("full", full);
    finished.AddInt32("written", written);
    finished.AddInt32("failed", failed);
    finished.AddString("failure", firstFailure.c_str());
    finished.AddInt32("count", (int32)job.items.size());
    finished.AddInt64("duration", writtenMs);
    {
        std::lock_guard<std::mutex> guard(shared.lock);
        // after a failure the songs not written yet can be written later
        if (!error.empty() && !shared.cancel) {
            shared.retry = RemainingJob(job, shared.itemStatus);
            AddRemaining(finished, shared.itemStatus);
        }
        shared.running.reset();
        shared.busy = false;
        shared.cancel = false;
        shared.state.busy = false;
    }
    shared.Post(finished);
}

void RunErase(Shared& shared, netmd::Device& device)
{
    std::string error;
    try {
        device.Flush();
        if (!device.DiscPresent())
            throw UserError{"There is no MiniDisc in the recorder."};
        device.EraseDisc();
    } catch (const UserError& e) {
        error = e.message;
    } catch (const netmd::Error& e) {
        error = DescribeError(e);
    }
    {
        std::lock_guard<std::mutex> guard(shared.lock);
        shared.busy = false;
        shared.state.busy = false;
    }
    BMessage finished(kMsgMDFinished);
    finished.AddBool("erase", true);
    finished.AddBool("ok", error.empty());
    finished.AddString("error", error.c_str());
    shared.Post(finished);
}

// The recorder the worker talks to: a USB device, or the simulator (AMP_NETMD_SIMULATE).
class WorkerTransport {
public:
    bool Open(const std::string& path, std::string& error)
    {
        Close();
        if (path == kSimulatorPath) {
            // one simulated recorder for the whole run: a worker that takes over from a stuck
            // one finds the disc as it was left
            static std::shared_ptr<netmd::Simulator> simulator;
            if (!simulator) {
                const char* speed = getenv("AMP_NETMD_SIMULATE");
                simulator.reset(new netmd::Simulator(speed && atof(speed) > 0 ? atof(speed) : 4.0));
                simulator->discTitle = "Simulated Disc";
                simulator->tracks = {{"Old Song One", 512 * 212, netmd::kEncodingLP2},
                    {"Old Song Two", 512 * 187, netmd::kEncodingLP2}, {"Old Song Three", 512 * 243, netmd::kEncodingSP}};
                // AMP_NETMD_SIMULATE_STALL=<upload>[:<seconds>]: that upload (from 0) stops partway
                // AMP_NETMD_SIMULATE_REFUSE=<upload>: from that upload (from 0) on, recording is refused
                if (const char* refuse = getenv("AMP_NETMD_SIMULATE_REFUSE"))
                    simulator->refuseUpload = atoi(refuse);
                if (const char* stall = getenv("AMP_NETMD_SIMULATE_STALL")) {
                    simulator->stallUpload = atoi(stall);
                    const char* colon = strchr(stall, ':');
                    simulator->stallMs = (colon ? atoi(colon + 1) : 60) * 1000;
                }
            }
            fSimulator = simulator;
            fCurrent = fSimulator.get();
            return true;
        }
        if (!fUsb.Open(path.c_str(), error))
            return false;
        fCurrent = &fUsb;
        return true;
    }
    void Close()
    {
        fUsb.Close();
        fCurrent = nullptr;
    }
    bool IsOpen() const { return fCurrent != nullptr; }
    netmd::Transport& Get() { return *fCurrent; }

private:
    NetMDUsbTransport fUsb;
    std::shared_ptr<netmd::Simulator> fSimulator;
    netmd::Transport* fCurrent = nullptr;
};

void Worker(std::shared_ptr<Shared> shared);

// Runs the worker and reports its end, so quitting can wait for a clean stop.
void RunWorker(std::shared_ptr<Shared> shared)
{
    try {
        Worker(shared);
    } catch (const WorkerAbandoned&) {
        return; // another worker has taken over
    }
    std::lock_guard<std::mutex> guard(shared->lock);
    shared->exited = true;
    shared->exit.notify_all();
}

void Worker(std::shared_ptr<Shared> shared)
{
    WorkerTransport transport;
    std::string openPath;
    auto watch = std::make_shared<Shared::Watch>();
    {
        std::lock_guard<std::mutex> guard(shared->lock);
        shared->watch = watch;
    }
    while (true) {
        std::unique_ptr<MiniDiscJob> job;
        bool erase, contents, present;
        NetMDDeviceId device;
        {
            std::unique_lock<std::mutex> guard(shared->lock);
            shared->wake.wait_for(guard, std::chrono::seconds(kPollSeconds), [&] {
                return shared->quit || shared->job || shared->erase || shared->refresh || shared->deviceChanged;
            });
            if (shared->quit)
                return;
            job = std::move(shared->job);
            erase = shared->erase;
            contents = shared->refreshContents || shared->deviceChanged;
            shared->erase = shared->refresh = shared->refreshContents = shared->deviceChanged = false;
            // after a stuck call the recorder is left alone until it is free again
            present = shared->present && !shared->needsReconnect;
            device = shared->device;
        }
        if (!present) {
            transport.Close();
            openPath.clear();
            if (job || erase) {
                std::lock_guard<std::mutex> guard(shared->lock);
                shared->busy = false;
                shared->state.busy = false;
            }
            if (job) {
                BMessage finished(kMsgMDFinished);
                finished.AddString("name", job->name.c_str());
                finished.AddBool("ok", false);
                finished.AddString("error", "The MiniDisc recorder is not connected.");
                shared->Post(finished);
            }
            continue;
        }
        if (openPath != device.path) {
            std::string error;
            transport.Close();
            if (transport.Open(device.path, error)) {
                openPath = device.path;
                try {
                    netmd::Device(transport.Get()).Flush();
                } catch (const netmd::Error&) {
                }
            } else {
                std::lock_guard<std::mutex> guard(shared->lock);
                shared->state.error = "Amp cannot open the MiniDisc recorder: " + error;
                shared->busy = false;
                shared->state.busy = false;
            }
        }
        if (!transport.IsOpen()) {
            shared->PostState();
            if (job) {
                BMessage finished(kMsgMDFinished);
                finished.AddString("name", job->name.c_str());
                finished.AddBool("ok", false);
                finished.AddString("error", "Amp cannot open the MiniDisc recorder.");
                shared->Post(finished);
            }
            continue;
        }
        WatchedTransport watched(transport.Get(), *shared, watch);
        netmd::Device md(watched);
        if (job) {
            RunJob(*shared, md, *job);
            ReadState(*shared, md, true);
        } else if (erase) {
            {
                std::lock_guard<std::mutex> guard(shared->lock);
                shared->eraseRunning = true;
            }
            RunErase(*shared, md);
            {
                std::lock_guard<std::mutex> guard(shared->lock);
                shared->eraseRunning = false;
            }
            ReadState(*shared, md, true);
        } else
            ReadState(*shared, md, contents);
    }
}

// Gives up on a worker whose USB call has not returned for kStallTimeout: the write or erase
// it was doing is reported as failed (with the songs it did not get to, for a retry), and a
// new worker takes over. That one leaves the recorder alone until it is unplugged or the
// stuck call returns after all; the old worker then unwinds (WorkerAbandoned).
void Watchdog(std::shared_ptr<Shared> shared)
{
    bigtime_t timeout = kStallTimeout;
    if (const char* seconds = getenv("AMP_NETMD_STALL_TIMEOUT"))
        if (atof(seconds) > 0)
            timeout = (bigtime_t)(atof(seconds) * 1000000);
    std::unique_lock<std::mutex> guard(shared->lock);
    while (!shared->watchdogQuit) {
        shared->wake.wait_for(guard, std::chrono::seconds(1), [&] { return shared->watchdogQuit; });
        if (shared->watchdogQuit || !shared->watch)
            continue;
        std::shared_ptr<Shared::Watch> watch = shared->watch;
        bigtime_t since = watch->since;
        if (since <= 0 || system_time() - since < timeout)
            continue;
        if (!watch->since.compare_exchange_strong(since, -1))
            continue; // it returned just now
        fprintf(stderr, "Amp: a USB transfer to the MiniDisc recorder has not finished in %d s; giving up on it\n",
            (int)(timeout / 1000000));
        shared->needsReconnect = true;
        shared->watch.reset();
        BMessage finished(kMsgMDFinished);
        bool report = false;
        if (shared->running) {
            const MiniDiscJob& job = *shared->running;
            int written = 0, failed = 0;
            for (int status : shared->itemStatus) {
                written += status == kItemWritten;
                failed += status == kItemSkipped;
            }
            finished.AddString("name", job.name.c_str());
            finished.AddBool("ok", false);
            finished.AddString("error", kStalledError);
            finished.AddBool("cancelled", false);
            finished.AddBool("stalled", true);
            finished.AddInt32("written", written);
            finished.AddInt32("failed", failed);
            finished.AddInt32("count", (int32)job.items.size());
            finished.AddInt64("duration", shared->writtenMs);
            shared->retry = RemainingJob(job, shared->itemStatus);
            AddRemaining(finished, shared->itemStatus);
            shared->running.reset();
            report = true;
        } else if (shared->eraseRunning) {
            finished.AddBool("erase", true);
            finished.AddBool("ok", false);
            finished.AddString("error", kStalledError);
            shared->eraseRunning = false;
            report = true;
        }
        shared->busy = false;
        shared->cancel = false;
        bool connected = shared->state.connected;
        std::string name = shared->state.deviceName;
        shared->state = MiniDiscState();
        shared->state.connected = connected;
        shared->state.deviceName = name;
        shared->state.error = kStalledState;
        std::thread(RunWorker, shared).detach();
        guard.unlock();
        if (report)
            shared->Post(finished);
        shared->PostState();
        guard.lock();
    }
}

} // namespace

MiniDiscManager::MiniDiscManager()
    : fShared(std::make_shared<Shared>())
{
}

MiniDiscManager::~MiniDiscManager()
{
    Stop();
}

void MiniDiscManager::Start(const BMessenger& target)
{
    fShared->target = target;
    std::thread(RunWorker, fShared).detach();
    fWatchdog = std::thread(Watchdog, fShared);
    std::shared_ptr<Shared> shared = fShared;
    fRoster.reset(new NetMDRoster());
    fRoster->onAdded = [shared](const NetMDDeviceId& id) {
        {
            std::lock_guard<std::mutex> guard(shared->lock);
            if (shared->present)
                return; // one recorder at a time
            shared->device = id;
            shared->present = true;
            shared->deviceChanged = true;
            shared->state = MiniDiscState();
            shared->state.connected = true;
            shared->state.deviceName = id.name;
        }
        shared->wake.notify_all();
        shared->PostState();
    };
    fRoster->onRemoved = [shared](const std::string& path) {
        {
            std::lock_guard<std::mutex> guard(shared->lock);
            if (!shared->present || shared->device.path != path)
                return;
            shared->present = false;
            shared->needsReconnect = false; // unplugging ends a stuck call
            shared->deviceChanged = true;
            bool busy = shared->busy;
            shared->state = MiniDiscState();
            shared->state.busy = busy;
        }
        shared->wake.notify_all();
        shared->PostState();
    };
    fRoster->Start();
    if (getenv("AMP_NETMD_SIMULATE")) {
        NetMDDeviceId simulated;
        simulated.path = kSimulatorPath;
        simulated.name = "Simulated NetMD recorder";
        fRoster->onAdded(simulated);
    }
}

void MiniDiscManager::Stop()
{
    if (fRoster) {
        fRoster->Stop();
        fRoster.reset();
    }
    std::unique_lock<std::mutex> guard(fShared->lock);
    fShared->quit = true;
    fShared->cancel = true;
    fShared->wake.notify_all();
    // Let the worker finish the exchange it is in: some recorders stop answering for good
    // when a command is cut off halfway. A transfer that never returns is not waited for.
    fShared->exit.wait_for(guard, std::chrono::seconds(5), [&] { return fShared->exited; });
    fShared->watchdogQuit = true;
    guard.unlock();
    fShared->wake.notify_all();
    if (fWatchdog.joinable())
        fWatchdog.join();
}

MiniDiscState MiniDiscManager::State()
{
    std::lock_guard<std::mutex> guard(fShared->lock);
    return fShared->state;
}

bool MiniDiscManager::Busy() const
{
    return fShared->busy;
}

bool MiniDiscManager::Cancelling() const
{
    return fShared->cancel;
}

void MiniDiscManager::Refresh(bool contents)
{
    {
        std::lock_guard<std::mutex> guard(fShared->lock);
        fShared->refresh = true;
        fShared->refreshContents = fShared->refreshContents || contents;
    }
    fShared->wake.notify_all();
}

bool MiniDiscManager::Write(const MiniDiscJob& job)
{
    {
        std::lock_guard<std::mutex> guard(fShared->lock);
        if (fShared->busy || !fShared->present || fShared->needsReconnect)
            return false;
        fShared->job.reset(new MiniDiscJob(job));
        fShared->busy = true;
        fShared->cancel = false;
        fShared->state.busy = true;
    }
    fShared->wake.notify_all();
    fShared->PostState();
    return true;
}

bool MiniDiscManager::WriteRemaining()
{
    std::unique_ptr<MiniDiscJob> rest;
    {
        std::lock_guard<std::mutex> guard(fShared->lock);
        if (!fShared->retry)
            return false;
        rest.reset(new MiniDiscJob(*fShared->retry));
    }
    return Write(*rest);
}

void MiniDiscManager::Erase()
{
    {
        std::lock_guard<std::mutex> guard(fShared->lock);
        if (fShared->busy || !fShared->present || fShared->needsReconnect)
            return;
        fShared->erase = true;
        fShared->busy = true;
        fShared->state.busy = true;
    }
    fShared->wake.notify_all();
    fShared->PostState();
}

void MiniDiscManager::Cancel()
{
    if (fShared->busy)
        fShared->cancel = true;
}

} // namespace amp
