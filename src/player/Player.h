// Playback controller: owns the audio output, the decoder and the play queue.
#pragma once
#include "AudioOutput.h"
#include "LocalDecoder.h"
#include "Messages.h"
#include "core/Queue.h"
#include <Messenger.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace amp {

class Library;

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
    void SetVolume(float volume);
    float Volume() const { return fOutput.Volume(); }
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

private:
    void StartCurrent();
    void StartLocal(int64_t trackId, const std::string& path);
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
};

} // namespace amp
