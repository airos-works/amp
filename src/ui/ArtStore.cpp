#include "ArtStore.h"
#include "player/Messages.h"
#include <Message.h>
#include <TranslationUtils.h>
#include <View.h>
#include <cstdio>

namespace amp {

namespace {
const size_t kMaxBitmaps = 900;
std::string CacheKey(const ArtKey& key, int size) { return key + "@" + std::to_string(size); }
}

ArtStore::ArtStore(ImageCache& cache)
    : fCache(cache)
{
    fCache.onReady = [this](const ArtKey& key, const std::string& path) { OnCacheReady(key, path); };
}

ArtStore::~ArtStore()
{
    Stop();
    for (auto& entry : fBitmaps)
        delete entry.second.bitmap;
}

void ArtStore::Start()
{
    if (fRunning)
        return;
    fRunning = true;
    fThread = std::thread([this] { Run(); });
}

void ArtStore::Stop()
{
    fRunning = false;
    fCondition.notify_all();
    if (fThread.joinable())
        fThread.join();
}

ArtRequest ArtStore::RequestFor(const ArtKey& key, const std::string& artist, const std::string& album,
    const std::string& localHint, bool artistImage)
{
    ArtRequest request;
    request.key = key;
    request.artist = artist;
    request.album = album;
    request.localHintPath = localHint;
    request.artistImage = artistImage;
    return request;
}

BBitmap* ArtStore::Get(const ArtKey& key, int size, const ArtRequest* request)
{
    if (key.empty())
        return nullptr;
    std::string cacheKey = CacheKey(key, size);
    {
        std::lock_guard<std::mutex> lock(fMutex);
        auto it = fBitmaps.find(cacheKey);
        if (it != fBitmaps.end()) {
            fLru.erase(it->second.lru);
            fLru.push_front(cacheKey);
            it->second.lru = fLru.begin();
            return it->second.bitmap;
        }
        if (fFailed.count(cacheKey))
            return nullptr;
        if (fLoading.count(cacheKey))
            return nullptr;
    }
    std::string path = fCache.PathFor(key);
    if (!path.empty()) {
        std::lock_guard<std::mutex> lock(fMutex);
        fLoading.insert(cacheKey);
        fQueue.push_front({key, size});
        fCondition.notify_one();
        return nullptr;
    }
    if (request && !fCache.KnownMissing(key))
        fCache.Request(*request, true);
    return nullptr;
}

void ArtStore::Invalidate(const ArtKey& key)
{
    std::lock_guard<std::mutex> lock(fMutex);
    for (auto it = fBitmaps.begin(); it != fBitmaps.end();) {
        if (it->first.compare(0, key.size() + 1, key + "@") == 0) {
            delete it->second.bitmap;
            fLru.erase(it->second.lru);
            it = fBitmaps.erase(it);
        } else
            ++it;
    }
    for (auto it = fFailed.begin(); it != fFailed.end();) {
        if (it->compare(0, key.size() + 1, key + "@") == 0)
            it = fFailed.erase(it);
        else
            ++it;
    }
}

void ArtStore::OnCacheReady(const ArtKey& key, const std::string& path)
{
    if (path.empty())
        return; // nothing found; views keep the placeholder
    Invalidate(key);
    BMessage message(kMsgArtReady);
    message.AddString("key", key.c_str());
    fTarget.SendMessage(&message);
}

BBitmap* ArtStore::LoadScaled(const std::string& path, int size)
{
    BBitmap* source = BTranslationUtils::GetBitmap(path.c_str());
    if (!source)
        return nullptr;
    BRect bounds = source->Bounds();
    float scale = std::min((float)size / (bounds.Width() + 1), (float)size / (bounds.Height() + 1));
    if (scale > 1.0f)
        scale = 1.0f;
    int w = std::max(1, (int)((bounds.Width() + 1) * scale));
    int h = std::max(1, (int)((bounds.Height() + 1) * scale));
    // Always render into an opaque RGB32 bitmap: translators may leave the alpha byte of
    // B_RGB32 output undefined, which would make an alpha-aware draw show nothing.
    BBitmap* scaled = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGB32, true);
    if (scaled->InitCheck() != B_OK) {
        delete scaled;
        delete source;
        return nullptr;
    }
    BView* view = new BView(scaled->Bounds(), "scaler", B_FOLLOW_NONE, B_WILL_DRAW);
    scaled->AddChild(view);
    if (scaled->Lock()) {
        view->SetHighColor(255, 255, 255);
        view->FillRect(scaled->Bounds());
        view->SetDrawingMode(source->ColorSpace() == B_RGBA32 ? B_OP_ALPHA : B_OP_COPY);
        view->DrawBitmap(source, bounds, scaled->Bounds(), B_FILTER_BITMAP_BILINEAR);
        view->Sync();
        scaled->Unlock();
    }
    scaled->RemoveChild(view);
    delete view;
    delete source;
    return scaled;
}

void ArtStore::Insert(const std::string& cacheKey, BBitmap* bitmap)
{
    std::lock_guard<std::mutex> lock(fMutex);
    fLru.push_front(cacheKey);
    fBitmaps[cacheKey] = {bitmap, fLru.begin()};
    while (fBitmaps.size() > kMaxBitmaps && !fLru.empty()) {
        std::string oldest = fLru.back();
        fLru.pop_back();
        auto it = fBitmaps.find(oldest);
        if (it != fBitmaps.end()) {
            delete it->second.bitmap;
            fBitmaps.erase(it);
        }
    }
}

void ArtStore::Run()
{
    while (fRunning) {
        std::pair<ArtKey, int> job;
        {
            std::unique_lock<std::mutex> lock(fMutex);
            fCondition.wait(lock, [this] { return !fRunning || !fQueue.empty(); });
            if (!fRunning)
                break;
            job = fQueue.front();
            fQueue.pop_front();
        }
        std::string cacheKey = CacheKey(job.first, job.second);
        std::string path = fCache.PathFor(job.first);
        BBitmap* bitmap = path.empty() ? nullptr : LoadScaled(path, job.second);
        if (bitmap)
            Insert(cacheKey, bitmap);
        else
            fprintf(stderr, "ArtStore: cannot load %s (%s) for %s\n", path.c_str(), path.empty() ? "no file" : "decode failed", job.first.c_str());
        {
            std::lock_guard<std::mutex> lock(fMutex);
            fLoading.erase(cacheKey);
            if (!bitmap)
                fFailed.insert(cacheKey);
        }
        BMessage message(kMsgArtReady);
        message.AddString("key", job.first.c_str());
        fTarget.SendMessage(&message);
    }
}

} // namespace amp
