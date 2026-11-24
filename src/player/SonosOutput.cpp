#include "SonosOutput.h"
#include "Messages.h"
#include "core/MediaServer.h"
#include <OS.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>

namespace amp {

namespace {

int64_t NowMs()
{
    return system_time() / 1000;
}

const int64_t kPollInterval = 1000;      // ms between looks at what the room plays
const int64_t kPollNearEnd = 300;        // ... in a song's last seconds, to move on promptly
const int64_t kVolumeInterval = 3000;    // ms between looks at the room's volume
const int64_t kVolumeQuiet = 2500;       // after we set the volume, its echo is not news
const int kFailuresToGiveUp = 3;         // unanswered looks in a row: the room is gone
const int64_t kEndSlack = 5000;          // a stop this close to a song's end is its end
const int64_t kWatchGap = 15000;         // no look for longer: we were not watching (asleep)

} // namespace

struct Command {
    enum Kind { kConnect, kLoad, kPlay, kPause, kStop, kSeek } kind;
    explicit Command(Kind what) : kind(what) {}
    int32 generation = 0;
    RemoteTrack track;
    int64_t ms = 0;
    bool play = false;
    bool initial = false;
};

struct SonosOutput::Shared {
    SonosDevice device;
    std::shared_ptr<MediaServer> server;
    BMessenger control;
    int32 session = 0;

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> queue;
    bool quit = false;
    bool stopOnQuit = false;
    bool volumeDirty = false;
    int volumeWanted = 0;
    std::atomic<uint32_t> serial{0};           // one more for every command

    // what the room plays, for the player between the worker's looks
    std::atomic<int> volume{-1};               // 0..100
    std::atomic<int64_t> volumeSetAt{0};
    std::atomic<int64_t> positionMs{0};
    std::atomic<int64_t> durationMs{0};
    std::atomic<int64_t> positionAt{0};        // when positionMs was true
    std::atomic<bool> playing{false};

    void Post(SonosEvent event, BMessage& message)
    {
        message.what = kMsgSonosEvent;
        message.AddInt32("event", event);
        message.AddInt32("session", session);
        control.SendMessage(&message);
    }

    void Enqueue(const Command& command)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(command);
            serial++;
        }
        wake.notify_one();
    }

    int64_t Position() const
    {
        int64_t position = positionMs;
        if (playing)
            position += NowMs() - positionAt;
        int64_t duration = durationMs;
        if (duration > 0 && position > duration)
            position = duration;
        return position < 0 ? 0 : position;
    }

    void SetPosition(int64_t position, bool isPlaying)
    {
        positionMs = position;
        positionAt = NowMs();
        playing = isPlaying;
    }
};

SonosOutput::SonosOutput(const SonosDevice& device, const std::shared_ptr<MediaServer>& server,
    const BMessenger& control, int32 session)
    : fDevice(device), fSession(session), fShared(std::make_shared<Shared>())
{
    fShared->device = device;
    fShared->server = server;
    fShared->control = control;
    fShared->session = session;
    std::shared_ptr<Shared> shared = fShared;
    fThread = std::thread([shared] { Run(shared); });
}

SonosOutput::~SonosOutput()
{
    if (!fReleased)
        Release(false, false);
}

void SonosOutput::Release(bool stopPlayback, bool wait)
{
    if (fReleased)
        return;
    fReleased = true;
    {
        std::lock_guard<std::mutex> lock(fShared->mutex);
        fShared->quit = true;
        fShared->stopOnQuit = stopPlayback;
        fShared->queue.clear();
    }
    fShared->wake.notify_one();
    if (wait && fThread.joinable())
        fThread.join();
    else if (fThread.joinable())
        fThread.detach();
}

void SonosOutput::Connect()
{
    Command command(Command::kConnect);
    fShared->Enqueue(command);
}

void SonosOutput::Load(int32 generation, const RemoteTrack& track, int64_t startMs, bool play, bool initial)
{
    Command command(Command::kLoad);
    command.generation = generation;
    command.track = track;
    command.ms = startMs;
    command.play = play;
    command.initial = initial;
    // what the player shows until the room reports
    fShared->durationMs = track.durationMs;
    fShared->SetPosition(startMs, false);
    fShared->Enqueue(command);
}

void SonosOutput::Play()
{
    fShared->SetPosition(fShared->Position(), true);
    fShared->Enqueue(Command(Command::kPlay));
}

void SonosOutput::Pause()
{
    fShared->SetPosition(fShared->Position(), false);
    fShared->Enqueue(Command(Command::kPause));
}

void SonosOutput::Stop()
{
    fShared->SetPosition(0, false);
    fShared->Enqueue(Command(Command::kStop));
}

void SonosOutput::Seek(int64_t positionMs)
{
    fShared->SetPosition(positionMs, fShared->playing);
    Command command(Command::kSeek);
    command.ms = positionMs;
    fShared->Enqueue(command);
}

void SonosOutput::SetVolume(float volume)
{
    int value = (int)(volume * 100 + 0.5f);
    value = value < 0 ? 0 : (value > 100 ? 100 : value);
    {
        std::lock_guard<std::mutex> lock(fShared->mutex);
        fShared->volumeWanted = value;
        fShared->volumeDirty = true;
    }
    fShared->volume = value;
    fShared->volumeSetAt = NowMs();
    fShared->wake.notify_one();
}

float SonosOutput::Volume() const
{
    int volume = fShared->volume;
    return volume < 0 ? -1.0f : volume / 100.0f;
}

int64_t SonosOutput::PositionMs() const
{
    return fShared->Position();
}

void SonosOutput::Run(std::shared_ptr<Shared> s)
{
    sonos::Client transport(s->device.coordinatorHost, s->device.coordinatorPort);
    sonos::Client room(s->device.host, s->device.port);
    bool connected = false;
    bool loaded = false;            // a song of ours is on the room
    int32 generation = 0;
    std::string uri;                // ...at this address
    bool seenPlaying = false;       // it has played since it was sent
    bool wantPlaying = false;       // we asked it to play...
    int64_t askedAt = 0, askedFrom = 0; // ...then, from there
    bool dropped = false;           // the room let go of the paused song: resuming finds the place
    int64_t lastLook = 0;           // when the room was last seen
    int lastState = -1;             // the last PlayerState the room was seen in
    int failures = 0;
    int64_t nextPoll = 0, nextVolume = 0;

    auto fail = [&](const std::string& error) {
        BMessage message;
        message.AddString("error", error.c_str());
        s->Post(kSonosFailed, message);
        connected = loaded = false;
    };
    // an unanswered call means the room is gone; a refused one is only reported
    auto check = [&](const sonos::Result& result, const char* what) {
        if (result.ok)
            return true;
        if (result.unreachable)
            fail(result.error);
        else {
            BMessage message;
            message.AddString("error", (std::string(what) + ": " + result.error).c_str());
            s->Post(kSonosError, message);
        }
        return false;
    };

    for (;;) {
        Command command(Command::kConnect);
        bool haveCommand = false, setVolume = false, quit = false, stop = false;
        int volume = 0;
        {
            std::unique_lock<std::mutex> lock(s->mutex);
            int64_t due = loaded ? nextPoll : (connected ? nextVolume : NowMs() + 60000);
            auto until = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(std::max<int64_t>(0, due - NowMs()));
            s->wake.wait_until(lock, until, [&] { return s->quit || !s->queue.empty() || s->volumeDirty; });
            if (s->quit) {
                quit = true;
                stop = s->stopOnQuit;
            } else if (!s->queue.empty()) {
                command = s->queue.front();
                s->queue.pop_front();
                haveCommand = true;
            } else if (s->volumeDirty) {
                setVolume = true;
                volume = s->volumeWanted;
                s->volumeDirty = false;
            }
        }
        if (quit) {
            if (stop && loaded)
                transport.Stop();
            break;
        }
        if (setVolume) {
            if (connected)
                check(room.SetVolume(volume), "Volume");
            continue;
        }
        if (haveCommand) {
            switch (command.kind) {
                case Command::kConnect: {
                    int value = 0;
                    sonos::Result result = room.GetVolume(value);
                    if (!result.ok) {
                        fail(result.error);
                        break;
                    }
                    connected = true;
                    s->volume = value;
                    nextVolume = NowMs() + kVolumeInterval;
                    BMessage message;
                    message.AddFloat("volume", value / 100.0f);
                    s->Post(kSonosReady, message);
                    break;
                }
                case Command::kLoad: {
                    if (!connected)
                        break;
                    BMessage message;
                    message.AddInt32("generation", command.generation);
                    message.AddBool("initial", command.initial);
                    std::string address = sonos::LocalAddressFor(s->device.coordinatorHost);
                    if (address.empty()) {
                        fail("no network route to the speaker");
                        break;
                    }
                    std::string base = "http://" + address + ":" + std::to_string(s->server->Port());
                    std::string url = base + s->server->Publish(command.track.path);
                    std::string art = command.track.artPath.empty() ? std::string()
                        : base + s->server->Publish(command.track.artPath);
                    std::string metadata = sonos::TrackMetadata(url, MediaServer::MimeTypeFor(command.track.path),
                        command.track.title, command.track.artist, command.track.album, art,
                        command.track.durationMs);
                    loaded = false;
                    sonos::Result result = transport.SetAVTransportURI(url, metadata);
                    // a position other than the start is reached before the sound begins
                    if (result.ok && command.ms > 1000)
                        result = transport.Seek(command.ms);
                    if (result.ok && command.play)
                        result = transport.Play();
                    if (result.unreachable) {
                        fail(result.error);
                        break;
                    }
                    message.AddBool("ok", result.ok);
                    if (!result.ok)
                        message.AddString("error", result.error.c_str());
                    else {
                        loaded = true;
                        wantPlaying = command.play;
                        askedAt = NowMs();
                        askedFrom = command.ms;
                        generation = command.generation;
                        uri = url;
                        seenPlaying = false;
                        dropped = false;
                        lastState = -1;
                        failures = 0;
                        lastLook = NowMs();
                        s->SetPosition(command.ms, command.play);
                        nextPoll = NowMs() + kPollInterval;
                    }
                    s->Post(kSonosLoaded, message);
                    break;
                }
                case Command::kPlay:
                    if (!loaded)
                        break;
                    if (dropped) {
                        // a stopped room starts the song over unless told where
                        int64_t position = s->Position();
                        if (position > 1000)
                            check(transport.Seek(position), "Seek");
                        dropped = false;
                    }
                    if (check(transport.Play(), "Play") && !wantPlaying) {
                        wantPlaying = true;
                        askedAt = NowMs();
                        askedFrom = s->Position();
                    }
                    break;
                case Command::kPause:
                    if (loaded)
                        check(transport.Pause(), "Pause");
                    wantPlaying = false;
                    break;
                case Command::kStop:
                    if (loaded)
                        check(transport.Stop(), "Stop");
                    loaded = false;
                    break;
                case Command::kSeek:
                    if (loaded)
                        check(transport.Seek(command.ms), "Seek");
                    break;
            }
            continue;
        }

        // nothing asked of us: look at the room
        int64_t now = NowMs();
        if (connected && now >= nextVolume) {
            nextVolume = now + kVolumeInterval;
            int value = 0;
            if (room.GetVolume(value).ok && value != s->volume && now - s->volumeSetAt > kVolumeQuiet) {
                s->volume = value;
                BMessage message;
                message.AddFloat("volume", value / 100.0f);
                s->Post(kSonosVolume, message);
            }
        }
        if (!loaded || now < nextPoll)
            continue;
        uint32_t serial = s->serial;
        sonos::TransportInfo info;
        sonos::Result result = transport.GetTransportInfo(info);
        if (result.ok)
            result = transport.GetPositionInfo(info);
        nextPoll = NowMs() + kPollInterval;
        if (!result.ok) {
            if (result.unreachable && ++failures >= kFailuresToGiveUp)
                fail("the speaker stopped answering");
            continue;
        }
        failures = 0;
        // a long silence since the last look (this computer slept): what the room did
        // meanwhile was not seen, so a stop then cannot be taken for a song's end
        int64_t lookedAt = NowMs();
        bool watched = lookedAt - lastLook <= kWatchGap;
        lastLook = lookedAt;
        // a command that came in meanwhile makes this look out of date
        if (serial != s->serial)
            continue;
        bool playing = info.state == "PLAYING" || info.state == "TRANSITIONING";
        if (!info.uri.empty() && info.uri != uri) {
            // something else plays there now
            loaded = false;
            s->SetPosition(0, false);
            BMessage message;
            message.AddInt32("generation", generation);
            message.AddInt32("state", kStopped);
            s->Post(kSonosTransport, message);
            continue;
        }
        if (info.state == "STOPPED") {
            int64_t waited = NowMs() - askedAt;
            int64_t songMs = s->durationMs;
            // only a song followed while it played, right to its end, has ended: a stop
            // anywhere else (from the Sonos app, or a paused song the room let go of hours
            // later) must not start the next song by itself
            bool atEnd = watched && wantPlaying && s->playing && songMs > 0
                && s->Position() >= songMs - kEndSlack
                && (seenPlaying || askedFrom + waited >= songMs + 1000);
            if (atEnd) {
                // the song ended (a short one can begin and end between two looks)
                loaded = false;
                s->SetPosition(s->Position(), false);
                BMessage message(kMsgTrackFinished);
                message.AddInt32("generation", generation);
                s->control.SendMessage(&message);
            } else if (watched && wantPlaying && !seenPlaying && waited > 6000) {
                // asked to play, it never did: it could not fetch or read the song
                loaded = false;
                s->SetPosition(askedFrom, false);
                BMessage message;
                message.AddInt32("generation", generation);
                message.AddBool("initial", false);
                message.AddBool("ok", false);
                message.AddString("error", "the speaker could not play it");
                s->Post(kSonosLoaded, message);
            } else if (watched && wantPlaying && !seenPlaying) {
                s->SetPosition(s->Position(), true);    // still starting
            } else if (!wantPlaying) {
                // paused here, and the room has let go of the song (or was sent it paused):
                // it stays paused, and playing again picks up where it was
                dropped = true;
                s->SetPosition(s->Position(), false);
            } else {
                // stopped from elsewhere, or while we were not watching: stopped here too
                loaded = false;
                s->SetPosition(0, false);
                BMessage message;
                message.AddInt32("generation", generation);
                message.AddInt32("state", kStopped);
                s->Post(kSonosTransport, message);
            }
            continue;
        }
        if (playing)
            seenPlaying = true;
        // paused or resumed from the Sonos app: what the room does is what is wanted now
        if (info.state == "PLAYING")
            wantPlaying = true;
        else if (info.state == "PAUSED_PLAYBACK")
            wantPlaying = false;
        if (info.durationMs > 0)
            s->durationMs = info.durationMs;
        // the room reports whole seconds: keep the smoother running estimate when it agrees
        int64_t estimate = s->Position();
        if (!playing || s->playing != playing || std::llabs(estimate - info.positionMs) > 1500)
            s->SetPosition(info.positionMs, playing);
        int64_t duration = s->durationMs;
        if (playing && duration > 0 && duration - s->Position() < 3000)
            nextPoll = NowMs() + kPollNearEnd;
        int state = info.state == "PAUSED_PLAYBACK" ? kPaused : (playing ? kPlaying : -1);
        if (state >= 0 && state != lastState) {
            lastState = state;
            BMessage message;
            message.AddInt32("generation", generation);
            message.AddInt32("state", state);
            s->Post(kSonosTransport, message);
        }
    }
}

void SonosOutput::AddDevice(BMessage& message, const SonosDevice& device)
{
    message.AddString("uuid", device.uuid.c_str());
    message.AddString("name", device.name.c_str());
    message.AddString("host", device.host.c_str());
    message.AddInt32("port", device.port);
    message.AddString("coordinator_uuid", device.coordinatorUuid.c_str());
    message.AddString("coordinator_host", device.coordinatorHost.c_str());
    message.AddInt32("coordinator_port", device.coordinatorPort);
}

std::vector<SonosDevice> SonosOutput::Devices(const BMessage& message)
{
    std::vector<SonosDevice> devices;
    const char* uuid;
    for (int32 i = 0; message.FindString("uuid", i, &uuid) == B_OK; i++) {
        SonosDevice device;
        device.uuid = uuid;
        device.name = message.GetString("name", i, "");
        device.host = message.GetString("host", i, "");
        device.port = message.GetInt32("port", i, 1400);
        device.coordinatorUuid = message.GetString("coordinator_uuid", i, "");
        device.coordinatorHost = message.GetString("coordinator_host", i, "");
        device.coordinatorPort = message.GetInt32("coordinator_port", i, 1400);
        devices.push_back(device);
    }
    return devices;
}

void SonosOutput::StartDiscovery(const BMessenger& target)
{
    std::thread([target] {
        std::string error;
        std::vector<SonosDevice> devices = sonos::Discover(1500, &error);
        // AMP_SONOS_EXTRA="Name=host[;Name=host]" adds rooms the search does not find: a test
        // without speakers (an address nobody answers at shows connecting and failing)
        if (const char* extra = getenv("AMP_SONOS_EXTRA")) {
            std::string list = extra;
            size_t start = 0;
            while (start < list.size()) {
                size_t end = list.find(';', start);
                std::string entry = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
                size_t equals = entry.find('=');
                if (equals != std::string::npos) {
                    SonosDevice device;
                    device.name = entry.substr(0, equals);
                    device.host = device.coordinatorHost = entry.substr(equals + 1);
                    device.uuid = device.coordinatorUuid = "AMP_TEST_" + device.host;
                    devices.push_back(device);
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            if (!devices.empty())
                error.clear();
        }
        BMessage message(kMsgSonosDevices);
        for (const SonosDevice& device : devices)
            AddDevice(message, device);
        if (!error.empty())
            message.AddString("error", error.c_str());
        target.SendMessage(&message);
    }).detach();
}

} // namespace amp
