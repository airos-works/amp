// BSoundPlayer-backed audio output with a lock-free-ish ring buffer of 16-bit stereo frames.
#pragma once
#include <Looper.h>
#include <MediaNode.h>
#include <SoundPlayer.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace amp {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    // (Re)creates the sound player when the format changes. Always stereo 16-bit input.
    bool Open(int sampleRate);
    void Close();
    bool IsOpen() const { return fPlayer != nullptr; }
    int SampleRate() const { return fSampleRate; }

    void SetVolume(float volume);   // 0..1 linear amplitude
    float Volume() const { return fVolume; }
    void SetMuted(bool muted) { fMuted = muted; }
    bool Muted() const { return fMuted; }

    // When false the output plays silence without consuming the buffer (pause).
    void SetPlaying(bool playing);
    bool Playing() const { return fPlaying; }

    // Blocking producer call: returns false when aborted by Flush()/Close() while waiting.
    bool Write(const int16_t* frames, size_t frameCount);
    void Flush();
    void AbortWriters();
    size_t BufferedFrames();
    size_t CapacityFrames() const { return fCapacity; }
    int64_t FramesConsumed() const { return fConsumed; }
    void ResetConsumed() { fConsumed = 0; }
    bigtime_t Latency() const;
    bool Underrun() const { return fUnderrun; }
    // How many times the sound player was made anew after the system mixer went away.
    int Reopens() const { return fReopens; }

private:
    class Watcher;
    friend class Watcher;

    static void PlayBuffer(void* cookie, void* buffer, size_t size, const media_raw_audio_format& format);
    void Fill(float* out, size_t frames);
    bool CreatePlayer();
    void MixerGone(media_node_id node);
    void Reopen();

    BSoundPlayer* fPlayer = nullptr;
    mutable std::mutex fPlayerMutex;
    Watcher* fWatcher = nullptr;
    media_node_id fMixer = -1;        // the system mixer the player is connected to
    std::atomic<bool> fReopening{false};
    std::atomic<int> fReopens{0};
    int fSampleRate = 0;
    std::vector<int16_t> fRing;   // interleaved stereo
    size_t fCapacity = 0;         // frames
    size_t fReadPos = 0;
    size_t fWritePos = 0;
    std::atomic<size_t> fCount{0};
    std::mutex fMutex;
    std::condition_variable fSpace;
    std::atomic<bool> fAbort{false};
    std::atomic<bool> fPlaying{false};
    std::atomic<bool> fMuted{false};
    std::atomic<bool> fUnderrun{false};
    std::atomic<int64_t> fConsumed{0};
    float fVolume = 0.8f;
    float fCurrentGain = 0.8f;
};

} // namespace amp
