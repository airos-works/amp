// Playback controller: owns the audio output, the decoder and the play queue.
#pragma once
#include "AudioOutput.h"
#include "LocalDecoder.h"
#include "Messages.h"
#include "SonosOutput.h"
#include "core/Queue.h"
#include <Messenger.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace amp {

class Library;
class MediaServer;

class Player {
public:
    explicit Player(Library& library);
    ~Player();

    void SetTarget(const BMessenger& target) { fTarget = target; }
    void SetControl(const BMessenger& control) { fControl = control; }
    // Called by the application looper when a kMsgTrackFinished message arrives.
    void HandleTrackFinished(int generation);
    void Start();
    void Shutdown();

    void PlayTracks(const std::vector<int64_t>& trackIds, int startIndex);
    void PlayPause();
    void Stop();
    void Next();
    void Previous();
    void Seek(int64_t positionMs);
    // The volume of where the sound goes: this computer, or the Sonos room playing.
    void SetVolume(float volume);
    // This computer's volume (the one the settings keep).
    float Volume() const { return fOutput.Volume(); }
    // What the volume slider shows: the room's volume while one plays.
    float OutputVolume();
    void SetShuffle(bool shuffle);
    bool Shuffle();
    void SetRepeat(RepeatMode mode);
    RepeatMode Repeat();
    void EnqueueNext(const std::vector<int64_t>& trackIds);
    void EnqueueLast(const std::vector<int64_t>& trackIds);

    PlayerState State() const { return fState; }
    int64_t CurrentTrack() const { return fCurrentTrack; }
    int64_t PositionMs();
    int64_t DurationMs() const { return fDurationMs; }
    std::vector<int64_t> QueueTracks();

    // Sonos: plays on the room once it answers (the song playing carries on there from where
    // it was); until then this computer plays on. Disconnecting plays the song on here from
    // where the room was. kMsgOutputChanged tells the window of each step.
    void ConnectSonos(const SonosDevice& device);
    void DisconnectSonos();
    // kMsgSonosEvent and the room's kMsgPlayerError, from the application looper.
    void HandleSonosEvent(BMessage* message);
    OutputState Output() const { return fOutputState; }
    std::string OutputUuid();
    std::string OutputName();
    // The picture file a speaker shows for a song (set by the application: the artwork cache).
    std::function<std::string(int64_t trackId)> artPathFor;

private:
    void StartCurrent();
    void StartLocal(int64_t trackId, const std::string& path, int64_t startMs = 0, bool play = true);
    void StartRemote(int64_t trackId, int64_t startMs, bool play, bool initial);
    void DisconnectLocked(bool resume, const std::string& error);
    // `error` and `left`: the room just left, and why when it was not asked to.
    void PostOutput(const std::string& error = std::string(), const std::string& left = std::string());
    void StopEngines();
    void TrackFinished(int generation);
    void SetState(PlayerState state);
    void PostState();
    void PostError(const std::string& error);
    void Ticker();

    Library& fLibrary;
    BMessenger fTarget;
    BMessenger fControl;
    AudioOutput fOutput;
    LocalDecoder fDecoder;
    PlayQueue fQueue;
    std::recursive_mutex fMutex;
    std::atomic<PlayerState> fState{kStopped};
    std::atomic<int64_t> fCurrentTrack{0};
    std::atomic<int64_t> fDurationMs{0};
    std::atomic<int> fGeneration{0};
    std::atomic<bool> fRunning{false};
    std::thread fTicker;
    // Sonos
    std::shared_ptr<MediaServer> fServer;
    std::unique_ptr<SonosOutput> fRemote;
    std::atomic<OutputState> fOutputState{kOutputLocal};
    bool fRemoteActive = false;       // the room plays; the local engines are stopped
    int32 fSession = 0;
    bigtime_t fRemoteCommandAt = 0;   // our last play or pause: the room's reports lag it
};

} // namespace amp
