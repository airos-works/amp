#include "Queue.h"
#include <algorithm>
#include <random>

namespace amp {

void PlayQueue::Set(const std::vector<int64_t>& trackIds, int startIndex)
{
    fTracks = trackIds;
    fOrder.clear();
    for (size_t i = 0; i < fTracks.size(); i++)
        fOrder.push_back((int)i);
    if (fShuffle) {
        Reshuffle();
        // make the chosen start track the first in shuffled order
        if (startIndex >= 0 && startIndex < (int)fTracks.size()) {
            auto it = std::find(fOrder.begin(), fOrder.end(), startIndex);
            if (it != fOrder.end())
                std::iter_swap(fOrder.begin(), it);
        }
        fPosition = fTracks.empty() ? -1 : 0;
    } else
        fPosition = (startIndex >= 0 && startIndex < (int)fTracks.size()) ? startIndex : (fTracks.empty() ? -1 : 0);
}

void PlayQueue::Clear()
{
    fTracks.clear();
    fOrder.clear();
    fPosition = -1;
}

int64_t PlayQueue::Current() const
{
    if (fPosition < 0 || fPosition >= (int)fOrder.size())
        return 0;
    return fTracks[fOrder[fPosition]];
}

bool PlayQueue::Next(bool manual)
{
    if (fOrder.empty())
        return false;
    if (fRepeat == RepeatMode::One && !manual)
        return true;
    if (fPosition + 1 < (int)fOrder.size()) {
        fPosition++;
        return true;
    }
    if (fRepeat != RepeatMode::Off || manual) {
        if (fRepeat == RepeatMode::Off)
            return false;
        if (fShuffle)
            Reshuffle();
        fPosition = 0;
        return true;
    }
    return false;
}

bool PlayQueue::Previous()
{
    if (fOrder.empty())
        return false;
    if (fPosition > 0)
        fPosition--;
    return true;
}

bool PlayQueue::JumpTo(int index)
{
    if (index < 0 || index >= (int)fTracks.size())
        return false;
    auto it = std::find(fOrder.begin(), fOrder.end(), index);
    if (it == fOrder.end())
        return false;
    fPosition = (int)(it - fOrder.begin());
    return true;
}

void PlayQueue::SetShuffle(bool shuffle)
{
    if (shuffle == fShuffle)
        return;
    int current = (fPosition >= 0 && fPosition < (int)fOrder.size()) ? fOrder[fPosition] : -1;
    fShuffle = shuffle;
    fOrder.clear();
    for (size_t i = 0; i < fTracks.size(); i++)
        fOrder.push_back((int)i);
    if (fShuffle) {
        Reshuffle();
        if (current >= 0) {
            auto it = std::find(fOrder.begin(), fOrder.end(), current);
            if (it != fOrder.end())
                std::iter_swap(fOrder.begin(), it);
            fPosition = 0;
        }
    } else
        fPosition = current;
}

void PlayQueue::Reshuffle()
{
    static std::mt19937 rng(std::random_device{}());
    std::shuffle(fOrder.begin(), fOrder.end(), rng);
}

void PlayQueue::Append(const std::vector<int64_t>& trackIds)
{
    for (int64_t id : trackIds) {
        fTracks.push_back(id);
        fOrder.push_back((int)fTracks.size() - 1);
    }
    if (fPosition < 0 && !fOrder.empty())
        fPosition = 0;
}

void PlayQueue::InsertNext(const std::vector<int64_t>& trackIds)
{
    int insertAt = fPosition + 1;
    if (insertAt > (int)fOrder.size())
        insertAt = (int)fOrder.size();
    for (size_t i = 0; i < trackIds.size(); i++) {
        fTracks.push_back(trackIds[i]);
        fOrder.insert(fOrder.begin() + insertAt + (int)i, (int)fTracks.size() - 1);
    }
    if (fPosition < 0 && !fOrder.empty())
        fPosition = 0;
}

void PlayQueue::RemoveTrack(int64_t trackId)
{
    for (int i = (int)fTracks.size() - 1; i >= 0; i--) {
        if (fTracks[i] != trackId)
            continue;
        auto it = std::find(fOrder.begin(), fOrder.end(), i);
        if (it != fOrder.end()) {
            int orderIndex = (int)(it - fOrder.begin());
            fOrder.erase(it);
            if (orderIndex < fPosition)
                fPosition--;
        }
        for (int& o : fOrder)
            if (o > i)
                o--;
        fTracks.erase(fTracks.begin() + i);
    }
    if (fPosition >= (int)fOrder.size())
        fPosition = (int)fOrder.size() - 1;
}

} // namespace amp
