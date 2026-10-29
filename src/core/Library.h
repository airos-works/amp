// SQLite-backed music library with an in-memory index for instant browsing.
// All public methods are thread-safe. Pointers returned by the *ById accessors stay valid
// while the caller holds a Locker (the index is node-stable and only mutated under the lock).
#pragma once
#include "Model.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace amp {

class Library {
public:
    explicit Library(const std::string& dbPath);
    ~Library();

    bool Open(std::string& error);
    void Close();

    class Locker {
    public:
        explicit Locker(Library& lib) : fLib(lib) { fLib.fMutex.lock(); }
        ~Locker() { fLib.fMutex.unlock(); }
    private:
        Library& fLib;
    };

    // Notified (from whatever thread changed the data) after the index was rebuilt.
    std::function<void()> onChanged;

    // ---- read access (hold a Locker while using returned pointers) ----
    const Track* TrackById(int64_t id) const;
    const Album* AlbumById(int64_t id) const;
    const Artist* ArtistById(int64_t id) const;
    const Playlist* PlaylistById(int64_t id) const;
    const Track* TrackByUri(const std::string& uri) const;
    std::vector<int64_t> AllTrackIds() const;      // sorted by album artist / album / disc / track
    std::vector<int64_t> AllAlbumIds() const;      // sorted by artist / album
    std::vector<int64_t> AllArtistIds() const;     // sorted by name
    std::vector<int64_t> AllPlaylistIds() const;   // by name
    int64_t AlbumIdFor(const Track& track) const;
    // The album of that artist and name, or 0. Album and artist ids are numbered afresh by
    // every index rebuild: a view that keeps one across a library change finds its album
    // again with this.
    int64_t AlbumIdFor(const std::string& artist, const std::string& name) const;
    int64_t ArtistIdFor(const std::string& name) const;
    std::vector<int64_t> SearchTracks(const std::string& query) const;
    size_t TrackCount() const;
    int64_t TotalDurationMs() const;
    int64_t TotalBytes() const;

    // ---- track maintenance (scanner) ----
    // Inserts or updates by uri; returns ids. Rebuilds the index when rebuild is true.
    std::vector<int64_t> UpsertTracks(std::vector<Track>& tracks, bool rebuild);
    void RemoveTracksByUri(const std::vector<std::string>& uris, bool rebuild);
    std::map<std::string, std::pair<int64_t, int64_t>> LocalFileIndex() const; // uri -> (mtime, size)
    void RebuildIndex();
    void SetAlbumArt(const std::string& artist, const std::string& album, const ArtKey& key);

    // ---- playlists ----
    int64_t CreatePlaylist(const std::string& name);
    bool RenamePlaylist(int64_t id, const std::string& name);
    bool DeletePlaylist(int64_t id);
    bool AddToPlaylist(int64_t id, const std::vector<int64_t>& trackIds, int position = -1);
    bool RemoveFromPlaylist(int64_t id, const std::vector<int>& positions);
    bool MoveInPlaylist(int64_t id, const std::vector<int>& positions, int target);

private:
    bool Exec(const char* sql, std::string* error = nullptr);
    void LoadAll();
    void SavePlaylist(const Playlist& playlist);
    void IndexTrack(Track& track);

    std::string fPath;
    sqlite3* fDb = nullptr;
    mutable std::recursive_mutex fMutex;

    std::unordered_map<int64_t, Track> fTracks;
    std::unordered_map<std::string, int64_t> fTrackByUri;
    std::unordered_map<int64_t, Album> fAlbums;
    std::unordered_map<std::string, int64_t> fAlbumByKey;    // "artist|album" lowercased
    std::unordered_map<int64_t, Artist> fArtists;
    std::unordered_map<std::string, int64_t> fArtistByKey;
    std::unordered_map<int64_t, Playlist> fPlaylists;
    std::unordered_map<std::string, ArtKey> fAlbumArtOverride; // key -> art (local scanner findings)
    std::vector<int64_t> fTrackOrder, fAlbumOrder, fArtistOrder;
    int64_t fNextAlbumId = 1;
    int64_t fNextArtistId = 1;
    int64_t fTotalDuration = 0;
    int64_t fTotalBytes = 0;
};

} // namespace amp
