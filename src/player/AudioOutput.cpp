#include "AudioOutput.h"
#include <MediaDefs.h>
#include <MediaRoster.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>

namespace amp {

// Hears from the media roster when nodes go away. A BSoundPlayer plays into
// the system mixer, which lives in the media_addon_server; when that server
// dies (a crash, or "Restart media services") the player is left talking to
// nothing and never notices. The watcher sees the mixer node being deleted
// and the output then makes a new player as soon as a new mixer exists.
class AudioOutput::Watcher : public BLooper {
public:
    explicit Watcher(AudioOutput& output)
        : BLooper("Amp audio watcher"), fOutput(output)
    {
        Run();
        BMediaRoster* roster = BMediaRoster::Roster();
        if (roster) {
            roster->StartWatching(BMessenger(this), B_MEDIA_NODE_DELETED);
            roster->StartWatching(BMessenger(this), B_MEDIA_SERVER_STARTED);
        }
    }

    void Detach()
    {
        BMediaRoster* roster = BMediaRoster::CurrentRoster();
        if (roster) {
            roster->StopWatching(BMessenger(this), B_MEDIA_NODE_DELETED);
            roster->StopWatching(BMessenger(this), B_MEDIA_SERVER_STARTED);
        }
    }

    void MessageReceived(BMessage* message) override
    {
        if (message->what != B_MEDIA_NODE_DELETED && message->what != B_MEDIA_SERVER_STARTED) {
            BLooper::MessageReceived(message);
            return;
        }
        if (message->what == B_MEDIA_SERVER_STARTED) {
            fOutput.MixerGone(-1);
            return;
        }
        int32 node;
        for (int32 i = 0; message->FindInt32("media_node_id", i, &node) == B_OK; i++)
            fOutput.MixerGone(node);
    }

private:
    AudioOutput& fOutput;
};

AudioOutput::AudioOutput() {}

AudioOutput::~AudioOutput()
{
    Close();
    if (fWatcher) {
        fWatcher->Detach();
        fWatcher->Lock();
        fWatcher->Quit();
        fWatcher = nullptr;
    }
}

bool AudioOutput::CreatePlayer()
{
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = (float)fSampleRate;
    format.channel_count = 2;
    format.format = media_raw_audio_format::B_AUDIO_FLOAT;
    format.byte_order = B_MEDIA_HOST_ENDIAN;
    format.buffer_size = 2048 * sizeof(float) * 2; // 2048 frames per buffer
    BSoundPlayer* player = new BSoundPlayer(&format, "Amp", PlayBuffer, nullptr, this);
    if (player->InitCheck() != B_OK) {
        delete player;
        return false;
    }
    media_node mixer;
    BMediaRoster* roster = BMediaRoster::Roster();
    fMixer = roster && roster->GetAudioMixer(&mixer) == B_OK ? mixer.node : -1;
    if (fMixer >= 0)
        roster->ReleaseNode(mixer);
    fPlayer = player;
    fPlayer->SetHasData(true);
    fPlayer->Start();
    if (!fWatcher)
        fWatcher = new Watcher(*this);
    return true;
}

bool AudioOutput::Open(int sampleRate)
{
    if (fPlayer && fSampleRate == sampleRate)
        return true;
    Close();
    std::lock_guard<std::mutex> lock(fPlayerMutex);
    fSampleRate = sampleRate;
    fCapacity = (size_t)sampleRate * 3 / 2; // 1.5 seconds
    fRing.assign(fCapacity * 2, 0);
    fReadPos = fWritePos = 0;
    fCount = 0;
    fAbort = false;
    fConsumed = 0;
    if (!CreatePlayer()) {
        fSampleRate = 0;
        return false;
    }
    return true;
}

void AudioOutput::Close()
{
    AbortWriters();
    std::lock_guard<std::mutex> lock(fPlayerMutex);
    if (fPlayer) {
        fPlayer->Stop();
        delete fPlayer;
        fPlayer = nullptr;
    }
    fMixer = -1;
    fSampleRate = 0;
    fCount = 0;
    fReadPos = fWritePos = 0;
}

void AudioOutput::MixerGone(media_node_id node)
{
    {
        std::lock_guard<std::mutex> lock(fPlayerMutex);
        if (!fPlayer || (node >= 0 && node != fMixer))
            return;
        // A new media_server numbers its nodes from the start again, so the
        // old mixer's number must not be taken for the new mixer's.
        if (node < 0)
            fMixer = -1;
    }
    if (fReopening.exchange(true))
        return;
    fprintf(stderr, "Amp: the system mixer went away, waiting for a new one\n");
    std::thread([this] { Reopen(); }).detach();
}

// Waits for a new system mixer (the media_addon_server starting again) and
// plays on through a new sound player. The ring buffer and the frame count
// stay as they are, so the song goes on where it was and the position shown
// stays right; the decoder, blocked on a full ring, never notices.
void AudioOutput::Reopen()
{
    BMediaRoster* roster = BMediaRoster::Roster();
    for (int attempt = 0; attempt < 120; attempt++) {
        snooze(500000);
        std::lock_guard<std::mutex> lock(fPlayerMutex);
        if (!fPlayer) // closed in the meantime
            break;
        media_node mixer;
        if (!roster || roster->GetAudioMixer(&mixer) != B_OK)
            continue;
        media_node_id id = mixer.node;
        roster->ReleaseNode(mixer);
        if (id == fMixer)
            continue;
        BSoundPlayer* old = fPlayer;
        fPlayer = nullptr;
        old->Stop();
        delete old;
        if (CreatePlayer()) {
            fReopens++;
            fprintf(stderr, "Amp: playing on through the new system mixer\n");
            break;
        }
    }
    fReopening = false;
}

void AudioOutput::SetVolume(float volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 1)
        volume = 1;
    fVolume = volume;
}

void AudioOutput::SetPlaying(bool playing)
{
    fPlaying = playing;
}

bigtime_t AudioOutput::Latency() const
{
    std::lock_guard<std::mutex> lock(fPlayerMutex);
    return fPlayer ? fPlayer->Latency() : 0;
}

bool AudioOutput::Write(const int16_t* frames, size_t frameCount)
{
    size_t written = 0;
    while (written < frameCount) {
        std::unique_lock<std::mutex> lock(fMutex);
        fSpace.wait_for(lock, std::chrono::milliseconds(200), [this] { return fAbort || fCount < fCapacity; });
        if (fAbort)
            return false;
        if (fCount >= fCapacity)
            continue;
        size_t free = fCapacity - fCount;
        size_t chunk = std::min(free, frameCount - written);
        size_t untilEnd = fCapacity - fWritePos;
        size_t first = std::min(chunk, untilEnd);
        memcpy(&fRing[fWritePos * 2], frames + written * 2, first * 2 * sizeof(int16_t));
        if (chunk > first)
            memcpy(&fRing[0], frames + (written + first) * 2, (chunk - first) * 2 * sizeof(int16_t));
        fWritePos = (fWritePos + chunk) % fCapacity;
        fCount += chunk;
        written += chunk;
    }
    return true;
}

void AudioOutput::Flush()
{
    std::lock_guard<std::mutex> lock(fMutex);
    fReadPos = fWritePos = 0;
    fCount = 0;
    fSpace.notify_all();
}

void AudioOutput::AbortWriters()
{
    fAbort = true;
    fSpace.notify_all();
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fReadPos = fWritePos = 0;
        fCount = 0;
    }
    fAbort = false;
}

size_t AudioOutput::BufferedFrames()
{
    return fCount;
}

void AudioOutput::PlayBuffer(void* cookie, void* buffer, size_t size, const media_raw_audio_format& format)
{
    AudioOutput* self = static_cast<AudioOutput*>(cookie);
    size_t frames = size / (sizeof(float) * format.channel_count);
    self->Fill(static_cast<float*>(buffer), frames);
}

void AudioOutput::Fill(float* out, size_t frames)
{
    if (!fPlaying) {
        memset(out, 0, frames * 2 * sizeof(float));
        return;
    }
    float target = fMuted ? 0.0f : fVolume;
    // perceptual curve: slider 0..1 -> amplitude
    target = target * target;
    size_t available = fCount;
    size_t take = std::min(frames, available);
    if (take < frames)
        fUnderrun = true;
    else
        fUnderrun = false;
    float gain = fCurrentGain;
    float step = (target - gain) / (float)std::max<size_t>(frames, 1);
    size_t readPos = fReadPos;
    for (size_t i = 0; i < take; i++) {
        gain += step;
        out[i * 2] = (float)fRing[readPos * 2] * (gain / 32768.0f);
        out[i * 2 + 1] = (float)fRing[readPos * 2 + 1] * (gain / 32768.0f);
        readPos++;
        if (readPos == fCapacity)
            readPos = 0;
    }
    fCurrentGain = target;
    if (take < frames)
        memset(out + take * 2, 0, (frames - take) * 2 * sizeof(float));
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fReadPos = readPos;
        fCount -= take;
    }
    fConsumed += (int64_t)take;
    fSpace.notify_all();
}

} // namespace amp
