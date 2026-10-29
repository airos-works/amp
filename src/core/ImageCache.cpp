#include "ImageCache.h"
#include "ArtProviders.h"
#include "Http.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

namespace amp {

namespace {

const int kWorkers = 3;                        // MusicBrainz is throttled globally; the rest overlap
const size_t kMaxQueued = 300;                 // older, no longer visible requests are dropped
const int64_t kMissingRetrySeconds = 7 * 24 * 3600;

// pictures that came from a Music Assistant server ("ma:", "mau:", "map:"), which Amp no
// longer talks to
bool IsServerKey(const std::string& key)
{
    return key.compare(0, 3, "ma:") == 0 || key.compare(0, 4, "mau:") == 0 || key.compare(0, 4, "map:") == 0;
}

uint64_t Fnv1a(const std::string& text)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string ExtensionForMime(const std::string& mime, const std::string& bytes)
{
    if (bytes.size() > 4) {
        if ((unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8)
            return ".jpg";
        if ((unsigned char)bytes[0] == 0x89 && bytes.compare(1, 3, "PNG") == 0)
            return ".png";
        if (bytes.compare(0, 4, "RIFF") == 0 && bytes.size() > 12 && bytes.compare(8, 4, "WEBP") == 0)
            return ".webp";
        if (bytes.compare(0, 3, "GIF") == 0)
            return ".gif";
    }
    if (mime.find("png") != std::string::npos)
        return ".png";
    if (mime.find("webp") != std::string::npos)
        return ".webp";
    if (mime.find("gif") != std::string::npos)
        return ".gif";
    return ".jpg";
}

bool LooksLikeImage(const std::string& bytes)
{
    if (bytes.size() < 64)
        return false;
    unsigned char b0 = bytes[0], b1 = bytes[1];
    return (b0 == 0xFF && b1 == 0xD8) || (b0 == 0x89 && bytes.compare(1, 3, "PNG") == 0)
        || bytes.compare(0, 4, "RIFF") == 0 || bytes.compare(0, 3, "GIF") == 0;
}

} // namespace

ImageCache::ImageCache(const std::string& cacheDir, Settings& settings)
    : fDir(cacheDir), fSettings(settings)
{
}

ImageCache::~ImageCache()
{
    Stop();
    if (fDb)
        sqlite3_close(fDb);
}

bool ImageCache::Open()
{
    mkdir(fDir.c_str(), 0755);
    std::string dbPath = fDir + "/index.db";
    if (sqlite3_open(dbPath.c_str(), &fDb) != SQLITE_OK)
        return false;
    sqlite3_exec(fDb, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS images (key TEXT PRIMARY KEY, file TEXT, updated INTEGER);"
        "CREATE TABLE IF NOT EXISTS missing (key TEXT PRIMARY KEY, updated INTEGER);", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(fDb, "SELECT key, file FROM images", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* key = sqlite3_column_text(stmt, 0);
            const unsigned char* file = sqlite3_column_text(stmt, 1);
            if (key && file)
                fIndex[(const char*)key] = fDir + "/" + (const char*)file;
        }
        sqlite3_finalize(stmt);
    }
    // what Music Assistant served earlier versions is never asked for again
    for (auto it = fIndex.begin(); it != fIndex.end();) {
        if (IsServerKey(it->first)) {
            unlink(it->second.c_str());
            it = fIndex.erase(it);
        } else
            ++it;
    }
    sqlite3_exec(fDb, "DELETE FROM images WHERE key LIKE 'ma:%' OR key LIKE 'mau:%' OR key LIKE 'map:%';"
        "DELETE FROM missing WHERE key LIKE 'ma:%' OR key LIKE 'mau:%' OR key LIKE 'map:%';", nullptr, nullptr, nullptr);
    if (sqlite3_prepare_v2(fDb, "SELECT key, updated FROM missing", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* key = sqlite3_column_text(stmt, 0);
            if (key)
                fMissing[(const char*)key] = sqlite3_column_int64(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    return true;
}

void ImageCache::Start()
{
    if (fRunning)
        return;
    fRunning = true;
    for (int i = 0; i < kWorkers; i++)
        fThreads.emplace_back([this] { Run(); });
}

void ImageCache::Stop()
{
    fRunning = false;
    fCondition.notify_all();
    for (std::thread& thread : fThreads)
        if (thread.joinable())
            thread.join();
    fThreads.clear();
}

std::string ImageCache::PathFor(const ArtKey& key)
{
    std::lock_guard<std::mutex> lock(fMutex);
    auto it = fIndex.find(key);
    return it == fIndex.end() ? "" : it->second;
}

bool ImageCache::Has(const ArtKey& key)
{
    std::lock_guard<std::mutex> lock(fMutex);
    return fIndex.count(key) != 0;
}

bool ImageCache::MissingLocked(const ArtKey& key)
{
    auto it = fMissing.find(key);
    if (it == fMissing.end())
        return false;
    return time(nullptr) - it->second < kMissingRetrySeconds;
}

bool ImageCache::KnownMissing(const ArtKey& key)
{
    std::lock_guard<std::mutex> lock(fMutex);
    return MissingLocked(key);
}

std::string ImageCache::FileNameFor(const ArtKey& key, const std::string& mime)
{
    char name[32];
    snprintf(name, sizeof(name), "%016llx", (unsigned long long)Fnv1a(key));
    return std::string(name) + mime;
}

bool ImageCache::Store(const ArtKey& key, const std::string& bytes, const std::string& mime)
{
    if (key.empty() || !LooksLikeImage(bytes))
        return false;
    std::string file = FileNameFor(key, ExtensionForMime(mime, bytes));
    std::string path = fDir + "/" + file;
    {
        std::ofstream out(path + ".tmp", std::ios::binary);
        if (!out)
            return false;
        out.write(bytes.data(), (std::streamsize)bytes.size());
    }
    if (rename((path + ".tmp").c_str(), path.c_str()) != 0)
        return false;
    std::lock_guard<std::mutex> lock(fMutex);
    fIndex[key] = path;
    fMissing.erase(key);
    if (fDb) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(fDb, "INSERT OR REPLACE INTO images (key, file, updated) VALUES (?, ?, ?)", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, file.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, (int64_t)time(nullptr));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(fDb, "DELETE FROM missing WHERE key = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return true;
}

void ImageCache::MarkMissing(const ArtKey& key)
{
    std::lock_guard<std::mutex> lock(fMutex);
    fMissing[key] = (int64_t)time(nullptr);
    if (fDb) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(fDb, "INSERT OR REPLACE INTO missing (key, updated) VALUES (?, ?)", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, (int64_t)time(nullptr));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void ImageCache::Enqueue(const ArtRequest& request, bool front)
{
    if (front)
        fQueue.push_front(request);
    else
        fQueue.push_back(request);
    fQueued.insert(request.key);
    // forget the oldest requests: they belong to items scrolled out of view long ago and
    // will simply be requested again when they become visible
    while (fQueue.size() > kMaxQueued) {
        fQueued.erase(fQueue.back().key);
        fQueue.pop_back();
    }
}

void ImageCache::Request(const ArtRequest& request, bool front)
{
    if (request.key.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        if (fIndex.count(request.key) || fActive.count(request.key) || MissingLocked(request.key))
            return;
        if (fQueued.count(request.key)) {
            if (!front)
                return;
            // already waiting: a visible item jumps the queue
            for (auto it = fQueue.begin(); it != fQueue.end(); ++it) {
                if (it->key != request.key)
                    continue;
                if (it != fQueue.begin()) {
                    ArtRequest moved = *it;
                    fQueue.erase(it);
                    fQueue.push_front(moved);
                }
                return;
            }
            return;
        }
        if (!OnlineLookupPossible(request))
            return;
        Enqueue(request, front);
    }
    fCondition.notify_all();
}

bool ImageCache::OnlineLookupPossible(const ArtRequest& request)
{
    if (request.artist.empty() || (!request.artistImage && request.album.empty()))
        return false;
    return fSettings.Get().fetchOnlineArt;
}

bool ImageCache::NextRequest(ArtRequest& request)
{
    std::unique_lock<std::mutex> lock(fMutex);
    fCondition.wait(lock, [&] { return !fRunning || !fQueue.empty(); });
    if (!fRunning)
        return false;
    request = fQueue.front();
    fQueue.pop_front();
    fQueued.erase(request.key);
    fActive.insert(request.key);
    return true;
}

void ImageCache::Finish(const ArtRequest& request, bool ok)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fActive.erase(request.key);
    }
    if (!ok)
        MarkMissing(request.key);
    if (onReady)
        onReady(request.key, ok ? PathFor(request.key) : "");
}

void ImageCache::Run()
{
    ArtRequest request;
    while (NextRequest(request))
        Finish(request, Has(request.key) || FetchOnline(request));
}

bool ImageCache::StoreDownloaded(const ArtKey& key, const std::string& url, const std::vector<std::string>& headers)
{
    if (url.empty())
        return false;
    HttpResponse response = Http::Get(url, headers, 30);
    if (!response.ok() || response.body.empty())
        return false;
    return Store(key, response.body, response.contentType);
}

bool ImageCache::FetchOnline(const ArtRequest& request)
{
    SettingsData settings = fSettings.Get();
    for (const ArtSource& source : settings.artSources) {
        if (!source.enabled)
            continue;
        std::vector<std::string> headers;
        std::vector<std::string> urls = ArtProviders::Lookup(source, request, headers);
        size_t comma = request.artist.find(", ");
        if (urls.empty() && comma != std::string::npos) {
            // "Artist A, Artist B" rarely matches anything: retry with the first artist only
            ArtRequest first = request;
            first.artist = request.artist.substr(0, comma);
            urls = ArtProviders::Lookup(source, first, headers);
        }
        for (const std::string& url : urls)
            if (StoreDownloaded(request.key, url, headers))
                return true;
        if (!fRunning)
            return false;
    }
    return false;
}

void ImageCache::ClearAll()
{
    std::lock_guard<std::mutex> lock(fMutex);
    for (auto& entry : fIndex)
        unlink(entry.second.c_str());
    fIndex.clear();
    fMissing.clear();
    if (fDb)
        sqlite3_exec(fDb, "DELETE FROM images; DELETE FROM missing;", nullptr, nullptr, nullptr);
}

int64_t ImageCache::CacheSizeBytes()
{
    std::lock_guard<std::mutex> lock(fMutex);
    int64_t total = 0;
    for (auto& entry : fIndex) {
        struct stat st;
        if (stat(entry.second.c_str(), &st) == 0)
            total += st.st_size;
    }
    return total;
}

} // namespace amp
