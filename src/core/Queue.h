// Playback queue: an ordered list of track ids with shuffle and repeat handling.
#pragma once
#include <cstdint>
#include <vector>

namespace amp {

enum class RepeatMode { Off = 0, All = 1, One = 2 };

class PlayQueue {
public:
    void Set(const std::vector<int64_t>& trackIds, int startIndex);
    void Clear();
    bool Empty() const { return fTracks.empty(); }
    size_t Size() const { return fTracks.size(); }
    int64_t Current() const;
    int CurrentIndex() const { return fPosition; }
    const std::vector<int64_t>& Tracks() const { return fTracks; }

    // Move to the next/previous track; returns false at the end (respecting repeat).
    bool Next(bool manual);
    bool Previous();
    bool JumpTo(int index);
    void SetShuffle(bool shuffle);
    bool Shuffle() const { return fShuffle; }
    void SetRepeat(RepeatMode mode) { fRepeat = mode; }
    RepeatMode Repeat() const { return fRepeat; }
    void Append(const std::vector<int64_t>& trackIds);
    void InsertNext(const std::vector<int64_t>& trackIds);
    void RemoveTrack(int64_t trackId);

private:
    void Reshuffle();
    std::vector<int64_t> fTracks;
    std::vector<int> fOrder;     // indices into fTracks, in play order
    int fPosition = -1;          // index into fOrder
    bool fShuffle = false;
    RepeatMode fRepeat = RepeatMode::Off;
};

} // namespace amp
