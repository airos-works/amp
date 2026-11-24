#include "Player.h"
#include "core/Library.h"
#include "core/MediaServer.h"
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
    {
        std::lock_guard<std::recursive_mutex> lock(fMutex);
        if (fRemote) {
            // the room stops with us: the songs it fetches go away
            fRemote->Release(fRemoteActive, true);
            fRemote.reset();
            fRemoteActive = false;
            fOutputState = kOutputLocal;
        }
    }
    if (fServer)
        fServer->Stop();
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

void Player::PostOutput(const std::string& error, const std::string& left)
{
    BMessage message(kMsgOutputChanged);
    message.AddInt32("state", fOutputState);
    message.AddString("uuid", fRemote ? fRemote->Device().uuid.c_str() : "");
    message.AddString("name", fRemote ? fRemote->Device().name.c_str() : left.c_str());
    message.AddFloat("volume", OutputVolume());
    if (!error.empty())
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
        if (fRemoteActive)
            fRemote->Stop();
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
    if (fRemoteActive)
        StartRemote(trackId, 0, true, false);
    else
        StartLocal(trackId, path);
}

void Player::StartLocal(int64_t trackId, const std::string& path, int64_t startMs, bool play)
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
    if (startMs > 0)
        fDecoder.Seek(startMs);
    fOutput.SetPlaying(play);
    fDecoder.Start();
    fState = play ? kPlaying : kPaused;
    PostState();
}

void Player::StartRemote(int64_t trackId, int64_t startMs, bool play, bool initial)
{
    RemoteTrack remote;
    {
        Library::Locker locker(fLibrary);
        const Track* track = fLibrary.TrackById(trackId);
        if (!track)
            return;
        remote.path = track->uri;
        remote.title = track->title;
        remote.artist = track->artist;
        remote.album = track->album;
        remote.durationMs = fDurationMs > 0 ? (int64_t)fDurationMs : track->durationMs;
    }
    if (artPathFor)
        remote.artPath = artPathFor(trackId);
    fRemote->Load(fGeneration, remote, startMs, play, initial);
    fRemoteCommandAt = system_time();
    fState = play ? kPlaying : kPaused;
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
            if (fRemoteActive) {
                fRemote->Pause();
                fRemoteCommandAt = system_time();
            } else
                fOutput.SetPlaying(false);
            SetState(kPaused);
            break;
        case kPaused:
            if (fRemoteActive) {
                fRemote->Play();
                fRemoteCommandAt = system_time();
            } else
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
    if (fRemoteActive)
        fRemote->Stop();
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
    if (fRemoteActive)
        fRemote->Seek(positionMs);
    else
        fDecoder.Seek(positionMs);
}

void Player::SetVolume(float volume)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fRemoteActive)
        fRemote->SetVolume(volume);
    else
        fOutput.SetVolume(volume);
}

float Player::OutputVolume()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fRemote && fOutputState == kOutputRemote && fRemote->Volume() >= 0)
        return fRemote->Volume();
    return fOutput.Volume();
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
    {
        std::lock_guard<std::recursive_mutex> lock(fMutex);
        if (fRemoteActive)
            return fRemote->PositionMs();
    }
    return fDecoder.PositionMs();
}

// #pragma mark - Sonos

std::string Player::OutputUuid()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fRemote ? fRemote->Device().uuid : std::string();
}

std::string Player::OutputName()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fRemote ? fRemote->Device().name : std::string();
}

void Player::ConnectSonos(const SonosDevice& device)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fRemote) {
        if (fRemote->Device().uuid == device.uuid)
            return;
        // another room: the song comes back here while the new one answers
        DisconnectLocked(true, std::string());
    }
    if (!fServer)
        fServer = std::make_shared<MediaServer>();
    std::string error;
    if (!fServer->Start(error)) {
        PostError("Cannot share songs with " + device.name + ": " + error);
        return;
    }
    fRemote.reset(new SonosOutput(device, fServer, fControl, ++fSession));
    fRemoteActive = false;
    fOutputState = kOutputConnecting;
    PostOutput();
    fRemote->Connect();
}

void Player::DisconnectSonos()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    DisconnectLocked(true, std::string());
}

void Player::DisconnectLocked(bool resume, const std::string& error)
{
    if (!fRemote)
        return;
    bool wasActive = fRemoteActive;
    std::string left = fRemote->Device().name;
    int64_t position = wasActive ? fRemote->PositionMs() : 0;
    PlayerState state = fState;
    // the room stops when we leave it on purpose; a room that is gone cannot be told
    fRemote->Release(wasActive && error.empty(), false);
    fRemote.reset();
    fRemoteActive = false;
    fOutputState = kOutputLocal;
    if (wasActive) {
        StopEngines(); // the room's late reports belong to an old generation now
        std::string path;
        if (fCurrentTrack != 0) {
            Library::Locker locker(fLibrary);
            if (const Track* track = fLibrary.TrackById(fCurrentTrack))
                path = track->uri;
        }
        if (resume && !path.empty() && state != kStopped)
            StartLocal(fCurrentTrack, path, position, state == kPlaying);
        else if (state != kStopped) {
            fCurrentTrack = 0;
            SetState(kStopped);
        }
    }
    PostOutput(error, left);
}

void Player::HandleSonosEvent(BMessage* message)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (!fRemote || message->GetInt32("session", -1) != fRemote->Session())
        return; // a room we have left
    std::string name = fRemote->Device().name;
    switch (message->GetInt32("event", -1)) {
        case kSonosReady: {
            // the room answered: the song moves over from where it is here
            int64_t track = fCurrentTrack;
            PlayerState state = fState;
            int64_t position = state == kStopped ? 0 : fDecoder.PositionMs();
            StopEngines();
            fRemoteActive = true;
            if (track != 0 && state != kStopped)
                StartRemote(track, position, state == kPlaying, true);
            else {
                fOutputState = kOutputRemote;
                PostOutput();
            }
            break;
        }
        case kSonosLoaded: {
            int32 generation = message->GetInt32("generation", -1);
            if (message->GetBool("initial", false) && fOutputState == kOutputConnecting) {
                fOutputState = kOutputRemote;
                PostOutput();
            }
            if (message->GetBool("ok", false) || generation != fGeneration)
                break;
            std::string path;
            {
                Library::Locker locker(fLibrary);
                if (const Track* track = fLibrary.TrackById(fCurrentTrack))
                    path = track->uri;
            }
            PostError(name + " cannot play \"" + path + "\": " + message->GetString("error", ""));
            // on to the next song, as with a file this computer cannot play
            if (fQueue.Next(false))
                StartCurrent();
            else {
                StopEngines();
                fCurrentTrack = 0;
                SetState(kStopped);
            }
            break;
        }
        case kSonosFailed: {
            std::string error = message->GetString("error", "no answer");
            PostError((fOutputState == kOutputConnecting ? "Cannot connect to " : "Lost the connection to ")
                + name + ": " + error);
            DisconnectLocked(true, error);
            break;
        }
        case kSonosTransport: {
            // paused, resumed or taken over from the Sonos app; our own commands echo back
            // a little late and are not news (a stop never echoes one: the room has let go)
            if (message->GetInt32("generation", -1) != fGeneration)
                break;
            PlayerState state = (PlayerState)message->GetInt32("state", kStopped);
            if (state != kStopped && system_time() - fRemoteCommandAt < 2000000)
                break;
            if (fState != kStopped)
                SetState(state);
            break;
        }
        case kSonosVolume:
            PostOutput();
            break;
        case kSonosError:
            PostError(name + ": " + message->GetString("error", ""));
            break;
    }
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
