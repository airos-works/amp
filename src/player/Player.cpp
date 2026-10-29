#include "Player.h"
#include "core/Library.h"
#include <Message.h>
#include <OS.h>
#include <cstdio>

namespace amp {

Player::Player(Library& library)
    : fLibrary(library), fDecoder(fOutput)
{
    fDecoder.onFinished = [this] { TrackFinished(fGeneration); };
}

Player::~Player()
{
    Shutdown();
}

void Player::Start()
{
    if (fRunning)
        return;
    fRunning = true;
    fTicker = std::thread([this] { Ticker(); });
}

void Player::Shutdown()
{
    fRunning = false;
    if (fTicker.joinable())
        fTicker.join();
    StopEngines();
    fOutput.Close();
}

void Player::SetState(PlayerState state)
{
    if (fState == state)
        return;
    fState = state;
    PostState();
}

void Player::PostState()
{
    BMessage message(kMsgPlayerStateChanged);
    message.AddInt32("state", fState);
    message.AddInt64("track", fCurrentTrack);
    fTarget.SendMessage(&message);
}

void Player::PostError(const std::string& error)
{
    BMessage message(kMsgPlayerError);
    message.AddString("error", error.c_str());
    fTarget.SendMessage(&message);
}

void Player::PlayTracks(const std::vector<int64_t>& trackIds, int startIndex)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fQueue.Set(trackIds, startIndex);
    StartCurrent();
}

void Player::EnqueueNext(const std::vector<int64_t>& trackIds)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    bool wasEmpty = fQueue.Empty();
    fQueue.InsertNext(trackIds);
    if (wasEmpty)
        StartCurrent();
}

void Player::EnqueueLast(const std::vector<int64_t>& trackIds)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    bool wasEmpty = fQueue.Empty();
    fQueue.Append(trackIds);
    if (wasEmpty)
        StartCurrent();
}

std::vector<int64_t> Player::QueueTracks()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fQueue.Tracks();
}

void Player::StopEngines()
{
    fGeneration++;
    fDecoder.Stop();
    fOutput.SetPlaying(false);
    fOutput.AbortWriters();
}

void Player::StartCurrent()
{
    int64_t trackId = fQueue.Current();
    if (trackId == 0) {
        StopEngines();
        fCurrentTrack = 0;
        SetState(kStopped);
        return;
    }
    std::string path;
    int64_t duration = 0;
    {
        Library::Locker locker(fLibrary);
        const Track* track = fLibrary.TrackById(trackId);
        if (!track) {
            fQueue.RemoveTrack(trackId);
            StartCurrent();
            return;
        }
        path = track->uri;
        duration = track->durationMs;
    }
    StopEngines();
    fCurrentTrack = trackId;
    fDurationMs = duration;
    PostState();
    StartLocal(trackId, path);
}

void Player::StartLocal(int64_t trackId, const std::string& path)
{
    std::string error;
    if (!fDecoder.Open(path, error)) {
        PostError("Cannot play \"" + path + "\": " + error);
        // skip to the next track
        if (fQueue.Next(false))
            StartCurrent();
        else
            SetState(kStopped);
        return;
    }
    if (fDecoder.DurationMs() > 0)
        fDurationMs = fDecoder.DurationMs();
    fOutput.SetPlaying(true);
    fDecoder.Start();
    fState = kPlaying;
    PostState();
}

void Player::TrackFinished(int generation)
{
    // Runs on the decoder thread: hand over to the application looper so that
    // the decoder is never stopped (joined) from its own thread.
    if (generation != fGeneration)
        return;
    BMessage message(kMsgTrackFinished);
    message.AddInt32("generation", generation);
    if (fControl.IsValid())
        fControl.SendMessage(&message);
    else
        HandleTrackFinished(generation);
}

void Player::HandleTrackFinished(int generation)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (generation != fGeneration)
        return;
    if (fQueue.Next(false))
        StartCurrent();
    else {
        StopEngines();
        fCurrentTrack = 0;
        SetState(kStopped);
    }
}

void Player::PlayPause()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    switch (fState) {
        case kPlaying:
            fOutput.SetPlaying(false);
            SetState(kPaused);
            break;
        case kPaused:
            fOutput.SetPlaying(true);
            SetState(kPlaying);
            break;
        case kStopped:
            if (!fQueue.Empty())
                StartCurrent();
            break;
        default:
            break;
    }
}

void Player::Stop()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    StopEngines();
    fCurrentTrack = 0;
    SetState(kStopped);
}

void Player::Next()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fQueue.Empty())
        return;
    if (fQueue.Next(true))
        StartCurrent();
    else
        Stop();
}

void Player::Previous()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fQueue.Empty())
        return;
    if (PositionMs() > 3000) {
        Seek(0);
        return;
    }
    fQueue.Previous();
    StartCurrent();
}

void Player::Seek(int64_t positionMs)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fState == kStopped)
        return;
    fDecoder.Seek(positionMs);
}

void Player::SetVolume(float volume)
{
    fOutput.SetVolume(volume);
}

void Player::SetShuffle(bool shuffle)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fQueue.SetShuffle(shuffle);
}

bool Player::Shuffle()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fQueue.Shuffle();
}

void Player::SetRepeat(RepeatMode mode)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fQueue.SetRepeat(mode);
}

RepeatMode Player::Repeat()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fQueue.Repeat();
}

int64_t Player::PositionMs()
{
    if (fState == kStopped)
        return 0;
    return fDecoder.PositionMs();
}

void Player::Ticker()
{
    while (fRunning) {
        snooze(500000);
        if (!fRunning)
            break;
        if (fState == kStopped)
            continue;
        BMessage message(kMsgPlayerProgress);
        message.AddInt64("position", PositionMs());
        message.AddInt64("duration", fDurationMs);
        message.AddInt64("track", fCurrentTrack);
        fTarget.SendMessage(&message);
    }
}

} // namespace amp
