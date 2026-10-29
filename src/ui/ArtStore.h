// Scaled BBitmap cache in front of the ImageCache; loads and scales on a worker thread.
#pragma once
#include "core/ImageCache.h"
#include "core/Model.h"
#include <Bitmap.h>
#include <Messenger.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace amp {

class ArtStore {
public:
    explicit ArtStore(ImageCache& cache);
    ~ArtStore();

    void SetTarget(const BMessenger& target) { fTarget = target; }
    void Start();
    void Stop();

    // Returns a cached scaled bitmap or nullptr. When nullptr and `request` is given, the image
    // is fetched/loaded in the background and kMsgArtReady is posted when it becomes available.
    BBitmap* Get(const ArtKey& key, int size, const ArtRequest* request);
    // Builds the fetch request for an album/artist key.
    ArtRequest RequestFor(const ArtKey& key, const std::string& artist, const std::string& album,
        const std::string& localHint, bool artistImage);
    void Invalidate(const ArtKey& key);

private:
    struct Entry {
        BBitmap* bitmap;
        std::list<std::string>::iterator lru;
    };
    void Run();
    BBitmap* LoadScaled(const std::string& path, int size);
    void Insert(const std::string& cacheKey, BBitmap* bitmap);
    void OnCacheReady(const ArtKey& key, const std::string& path);

    ImageCache& fCache;
    BMessenger fTarget;
    std::mutex fMutex;
    std::map<std::string, Entry> fBitmaps;      // "key@size"
    std::list<std::string> fLru;
    std::set<std::string> fLoading;
    std::set<std::string> fFailed;              // "key@size" whose file could not be decoded
    std::deque<std::pair<ArtKey, int>> fQueue;
    std::condition_variable fCondition;
    std::thread fThread;
    std::atomic<bool> fRunning{false};
};

} // namespace amp
