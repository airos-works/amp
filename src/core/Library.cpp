#include "Library.h"
#include "MusicAssistant.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <sqlite3.h>

namespace amp {

namespace {

const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS tracks (
    id INTEGER PRIMARY KEY, source INTEGER NOT NULL DEFAULT 0, uri TEXT NOT NULL UNIQUE,
    title TEXT, artist TEXT, album_artist TEXT, album TEXT, genre TEXT, year INTEGER,
    track INTEGER, disc INTEGER, duration_ms INTEGER, size INTEGER, mtime INTEGER, bitrate INTEGER,
    ma_item_id TEXT, ma_album_uri TEXT, ma_artist_uri TEXT, art TEXT, date_added INTEGER);
CREATE INDEX IF NOT EXISTS tracks_source ON tracks(source);
CREATE TABLE IF NOT EXISTS playlists (
    id INTEGER PRIMARY KEY, source INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL,
    ma_uri TEXT, ma_item_id TEXT, sync_ma INTEGER DEFAULT 0, editable INTEGER DEFAULT 1, modified INTEGER);
CREATE TABLE IF NOT EXISTS playlist_tracks (
    playlist_id INTEGER NOT NULL, position INTEGER NOT NULL, track_id INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, position));
CREATE TABLE IF NOT EXISTS ma_albums (uri TEXT PRIMARY KEY, item_id TEXT, name TEXT, artist TEXT, year INTEGER, art TEXT);
CREATE TABLE IF NOT EXISTS ma_artists (uri TEXT PRIMARY KEY, item_id TEXT, name TEXT, art TEXT);
CREATE TABLE IF NOT EXISTS album_art (key TEXT PRIMARY KEY, art TEXT);
)SQL";

std::string GroupKey(const std::string& artist, const std::string& album)
{
    return ToLower(artist) + "|" + ToLower(album);
}

std::string ColumnText(sqlite3_stmt* stmt, int column)
{
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    Statement(sqlite3* db, const char* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            fprintf(stderr, "sqlite prepare failed: %s\n%s\n", sqlite3_errmsg(db), sql);
            stmt = nullptr;
        }
    }
    ~Statement() { if (stmt) sqlite3_finalize(stmt); }
    void Text(int index, const std::string& value) { if (stmt) sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT); }
    void Int(int index, int64_t value) { if (stmt) sqlite3_bind_int64(stmt, index, value); }
    bool Step() { return stmt && sqlite3_step(stmt) == SQLITE_ROW; }
    bool Done() { return stmt && sqlite3_step(stmt) == SQLITE_DONE; }
    void Reset() { if (stmt) { sqlite3_reset(stmt); sqlite3_clear_bindings(stmt); } }
};

} // namespace

Library::Library(const std::string& dbPath)
    : fPath(dbPath)
{
}

Library::~Library()
{
    Close();
}

bool Library::Exec(const char* sql, std::string* error)
{
    char* message = nullptr;
    if (sqlite3_exec(fDb, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        if (error)
            *error = message ? message : "sqlite error";
        fprintf(stderr, "sqlite: %s\n", message ? message : "error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool Library::Open(std::string& error)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (sqlite3_open(fPath.c_str(), &fDb) != SQLITE_OK) {
        error = sqlite3_errmsg(fDb);
        return false;
    }
    Exec("PRAGMA journal_mode=WAL;");
    Exec("PRAGMA synchronous=NORMAL;");
    if (!Exec(kSchema, &error))
        return false;
    {
        // older databases lack the column; the error for an existing column is expected
        char* message = nullptr;
        sqlite3_exec(fDb, "ALTER TABLE playlists ADD COLUMN dynamic INTEGER DEFAULT 0", nullptr, nullptr, &message);
        sqlite3_free(message);
        message = nullptr;
        sqlite3_exec(fDb, "ALTER TABLE tracks ADD COLUMN in_library INTEGER DEFAULT 1", nullptr, nullptr, &message);
        sqlite3_free(message);
        message = nullptr;
        sqlite3_exec(fDb, "ALTER TABLE tracks ADD COLUMN lossless INTEGER DEFAULT 0", nullptr, nullptr, &message);
        sqlite3_free(message);
    }
    LoadAll();
    RebuildIndex();
    return true;
}

void Library::Close()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (fDb) {
        sqlite3_close(fDb);
        fDb = nullptr;
    }
}

void Library::LoadAll()
{
    fTracks.clear();
    fTrackByUri.clear();
    fPlaylists.clear();
    fMAAlbums.clear();
    fMAArtists.clear();
    fAlbumArtOverride.clear();
    {
        Statement s(fDb, "SELECT id, source, uri, title, artist, album_artist, album, genre, year, track, disc, "
            "duration_ms, size, mtime, bitrate, ma_item_id, ma_album_uri, ma_artist_uri, art, date_added, in_library, lossless FROM tracks");
        while (s.Step()) {
            Track t;
            t.id = sqlite3_column_int64(s.stmt, 0);
            t.source = (Source)sqlite3_column_int(s.stmt, 1);
            t.uri = ColumnText(s.stmt, 2);
            t.title = ColumnText(s.stmt, 3);
            t.artist = ColumnText(s.stmt, 4);
            t.albumArtist = ColumnText(s.stmt, 5);
            t.album = ColumnText(s.stmt, 6);
            t.genre = ColumnText(s.stmt, 7);
            t.year = sqlite3_column_int(s.stmt, 8);
            t.trackNumber = sqlite3_column_int(s.stmt, 9);
            t.discNumber = sqlite3_column_int(s.stmt, 10);
            t.durationMs = sqlite3_column_int64(s.stmt, 11);
            t.sizeBytes = sqlite3_column_int64(s.stmt, 12);
            t.modifiedTime = sqlite3_column_int64(s.stmt, 13);
            t.bitrate = sqlite3_column_int(s.stmt, 14);
            t.maItemId = ColumnText(s.stmt, 15);
            t.maAlbumUri = ColumnText(s.stmt, 16);
            t.maArtistUri = ColumnText(s.stmt, 17);
            t.art = ColumnText(s.stmt, 18);
            t.dateAdded = sqlite3_column_int64(s.stmt, 19);
            t.inLibrary = sqlite3_column_type(s.stmt, 20) == SQLITE_NULL || sqlite3_column_int(s.stmt, 20) != 0;
            t.lossless = sqlite3_column_int(s.stmt, 21) != 0;
            fTrackByUri[t.uri] = t.id;
            fTracks[t.id] = std::move(t);
        }
    }
    {
        Statement s(fDb, "SELECT id, source, name, ma_uri, ma_item_id, sync_ma, editable, modified, dynamic FROM playlists");
        while (s.Step()) {
            Playlist p;
            p.id = sqlite3_column_int64(s.stmt, 0);
            p.source = (Source)sqlite3_column_int(s.stmt, 1);
            p.name = ColumnText(s.stmt, 2);
            p.maUri = ColumnText(s.stmt, 3);
            p.maItemId = ColumnText(s.stmt, 4);
            p.syncToMA = sqlite3_column_int(s.stmt, 5) != 0;
            p.editable = sqlite3_column_int(s.stmt, 6) != 0;
            p.modified = sqlite3_column_int64(s.stmt, 7);
            p.dynamic = sqlite3_column_int(s.stmt, 8) != 0;
            fPlaylists[p.id] = p;
        }
        Statement pt(fDb, "SELECT playlist_id, track_id FROM playlist_tracks ORDER BY playlist_id, position");
        while (pt.Step()) {
            auto it = fPlaylists.find(sqlite3_column_int64(pt.stmt, 0));
            if (it != fPlaylists.end())
                it->second.trackIds.push_back(sqlite3_column_int64(pt.stmt, 1));
        }
    }
    {
        Statement s(fDb, "SELECT uri, item_id, name, artist, year, art FROM ma_albums");
        while (s.Step())
            fMAAlbums[ColumnText(s.stmt, 0)] = {ColumnText(s.stmt, 1), ColumnText(s.stmt, 2), ColumnText(s.stmt, 3),
                sqlite3_column_int(s.stmt, 4), ColumnText(s.stmt, 5)};
        Statement a(fDb, "SELECT uri, item_id, name, art FROM ma_artists");
        while (a.Step())
            fMAArtists[ColumnText(a.stmt, 0)] = {ColumnText(a.stmt, 1), ColumnText(a.stmt, 2), ColumnText(a.stmt, 3)};
        Statement art(fDb, "SELECT key, art FROM album_art");
        while (art.Step())
            fAlbumArtOverride[ColumnText(art.stmt, 0)] = ColumnText(art.stmt, 1);
    }
}

void Library::RebuildIndex()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    fAlbums.clear();
    fAlbumByKey.clear();
    fArtists.clear();
    fArtistByKey.clear();
    fNextAlbumId = 1;
    fNextArtistId = 1;
    fTotalDuration = 0;
    fTotalBytes = 0;
    for (auto& entry : fTracks)
        if (entry.second.inLibrary)
            IndexTrack(entry.second);
    // sort album track lists and compute derived fields
    for (auto& entry : fAlbums) {
        Album& album = entry.second;
        std::sort(album.trackIds.begin(), album.trackIds.end(), [this](int64_t a, int64_t b) {
            const Track& ta = fTracks[a];
            const Track& tb = fTracks[b];
            if (ta.discNumber != tb.discNumber)
                return ta.discNumber < tb.discNumber;
            if (ta.trackNumber != tb.trackNumber)
                return ta.trackNumber < tb.trackNumber;
            return ToLower(ta.title) < ToLower(tb.title);
        });
        album.trackCount = (int)album.trackIds.size();
    }
    fAlbumOrder.clear();
    for (auto& entry : fAlbums)
        fAlbumOrder.push_back(entry.first);
    std::sort(fAlbumOrder.begin(), fAlbumOrder.end(), [this](int64_t a, int64_t b) {
        const Album& aa = fAlbums[a];
        const Album& ab = fAlbums[b];
        if (aa.sortKey != ab.sortKey)
            return aa.sortKey < ab.sortKey;
        return aa.id < ab.id;
    });
    fTrackOrder.clear();
    for (int64_t albumId : fAlbumOrder)
        for (int64_t trackId : fAlbums[albumId].trackIds)
            fTrackOrder.push_back(trackId);
    for (auto& entry : fArtists) {
        Artist& artist = entry.second;
        std::sort(artist.albumIds.begin(), artist.albumIds.end(), [this](int64_t a, int64_t b) {
            const Album& aa = fAlbums[a];
            const Album& ab = fAlbums[b];
            if (aa.year != ab.year)
                return aa.year < ab.year;
            return aa.sortKey < ab.sortKey;
        });
        artist.trackIds.clear();
        for (int64_t albumId : artist.albumIds)
            for (int64_t trackId : fAlbums[albumId].trackIds)
                artist.trackIds.push_back(trackId);
        artist.albumCount = (int)artist.albumIds.size();
        artist.trackCount = (int)artist.trackIds.size();
    }
    fArtistOrder.clear();
    for (auto& entry : fArtists)
        fArtistOrder.push_back(entry.first);
    std::sort(fArtistOrder.begin(), fArtistOrder.end(), [this](int64_t a, int64_t b) {
        return fArtists[a].sortKey < fArtists[b].sortKey;
    });
    if (onChanged)
        onChanged();
}

void Library::IndexTrack(Track& track)
{
    fTotalDuration += track.durationMs;
    fTotalBytes += track.sizeBytes;
    std::string artistName = track.groupingArtist();
    if (artistName.empty())
        artistName = "Unknown Artist";
    std::string albumName = track.album.empty() ? "Unknown Album" : track.album;
    std::string albumKey = GroupKey(artistName, albumName) + (track.isMA() ? "|ma" : "");
    auto found = fAlbumByKey.find(albumKey);
    int64_t albumId;
    if (found == fAlbumByKey.end()) {
        Album album;
        album.id = fNextAlbumId++;
        album.source = track.source;
        album.name = albumName;
        album.artist = artistName;
        album.year = track.year;
        album.sortKey = SortKeyFor(artistName) + "|" + SortKeyFor(albumName);
        if (track.isMA()) {
            album.maUri = track.maAlbumUri;
            auto ma = fMAAlbums.find(track.maAlbumUri);
            if (ma != fMAAlbums.end()) {
                album.maItemId = ma->second.itemId;
                if (!ma->second.art.empty())
                    album.art = ma->second.art;
                if (album.year == 0)
                    album.year = ma->second.year;
            }
        } else {
            auto over = fAlbumArtOverride.find(GroupKey(artistName, albumName));
            if (over != fAlbumArtOverride.end())
                album.art = over->second;
        }
        if (album.art.empty())
            album.art = track.art.empty() ? MakeAlbumArtKey(artistName, albumName) : track.art;
        albumId = album.id;
        fAlbums[albumId] = album;
        fAlbumByKey[albumKey] = albumId;
    } else {
        albumId = found->second;
        Album& album = fAlbums[albumId];
        if (album.year == 0 && track.year)
            album.year = track.year;
        if (album.art.compare(0, 6, "album:") == 0 && !track.art.empty() && track.art.compare(0, 3, "ma:") == 0)
            album.art = track.art;
    }
    Album& album = fAlbums[albumId];
    album.trackIds.push_back(track.id);
    album.durationMs += track.durationMs;
    track.albumId = albumId;

    std::string artistKey = ToLower(artistName) + (track.isMA() ? "|ma" : "");
    auto artistFound = fArtistByKey.find(artistKey);
    int64_t artistId;
    if (artistFound == fArtistByKey.end()) {
        Artist artist;
        artist.id = fNextArtistId++;
        artist.source = track.source;
        artist.name = artistName;
        artist.sortKey = SortKeyFor(artistName);
        if (track.isMA()) {
            auto ma = fMAArtists.find(track.maArtistUri);
            if (ma != fMAArtists.end() && ToLower(ma->second.name) == ToLower(artistName)) {
                artist.maUri = track.maArtistUri;
                artist.maItemId = ma->second.itemId;
                artist.art = ma->second.art;
            } else {
                for (auto& entry : fMAArtists)
                    if (ToLower(entry.second.name) == ToLower(artistName)) {
                        artist.maUri = entry.first;
                        artist.maItemId = entry.second.itemId;
                        artist.art = entry.second.art;
                        break;
                    }
            }
        }
        if (artist.art.empty())
            artist.art = MakeArtistArtKey(artistName);
        artistId = artist.id;
        fArtists[artistId] = artist;
        fArtistByKey[artistKey] = artistId;
    } else
        artistId = artistFound->second;
    Artist& artist = fArtists[artistId];
    if (std::find(artist.albumIds.begin(), artist.albumIds.end(), albumId) == artist.albumIds.end())
        artist.albumIds.push_back(albumId);
}

const Track* Library::TrackById(int64_t id) const
{
    auto it = fTracks.find(id);
    return it == fTracks.end() ? nullptr : &it->second;
}

const Album* Library::AlbumById(int64_t id) const
{
    auto it = fAlbums.find(id);
    return it == fAlbums.end() ? nullptr : &it->second;
}

const Artist* Library::ArtistById(int64_t id) const
{
    auto it = fArtists.find(id);
    return it == fArtists.end() ? nullptr : &it->second;
}

const Playlist* Library::PlaylistById(int64_t id) const
{
    auto it = fPlaylists.find(id);
    return it == fPlaylists.end() ? nullptr : &it->second;
}

const Track* Library::TrackByUri(const std::string& uri) const
{
    auto it = fTrackByUri.find(uri);
    return it == fTrackByUri.end() ? nullptr : TrackById(it->second);
}

std::vector<int64_t> Library::AllTrackIds() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fTrackOrder;
}

std::vector<int64_t> Library::AllAlbumIds() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fAlbumOrder;
}

std::vector<int64_t> Library::AllArtistIds() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fArtistOrder;
}

std::vector<int64_t> Library::AllPlaylistIds() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    std::vector<int64_t> ids;
    for (auto& entry : fPlaylists)
        ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end(), [this](int64_t a, int64_t b) {
        const Playlist& pa = fPlaylists.at(a);
        const Playlist& pb = fPlaylists.at(b);
        if (pa.source != pb.source)
            return pa.source == Source::Local;
        return ToLower(pa.name) < ToLower(pb.name);
    });
    return ids;
}

int64_t Library::AlbumIdFor(const Track& track) const
{
    return track.albumId;
}

int64_t Library::AlbumIdFor(const std::string& artist, const std::string& name, bool ma) const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fAlbumByKey.find(GroupKey(artist, name) + (ma ? "|ma" : ""));
    return it == fAlbumByKey.end() ? 0 : it->second;
}

int64_t Library::ArtistIdFor(const std::string& name) const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fArtistByKey.find(ToLower(name));
    if (it != fArtistByKey.end())
        return it->second;
    it = fArtistByKey.find(ToLower(name) + "|ma");
    return it == fArtistByKey.end() ? 0 : it->second;
}

std::vector<int64_t> Library::SearchTracks(const std::string& query) const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    std::vector<int64_t> result;
    if (query.empty())
        return fTrackOrder;
    std::string q = ToLower(query);
    for (int64_t id : fTrackOrder) {
        const Track& t = fTracks.at(id);
        if (ToLower(t.title).find(q) != std::string::npos || ToLower(t.artist).find(q) != std::string::npos
            || ToLower(t.album).find(q) != std::string::npos || ToLower(t.albumArtist).find(q) != std::string::npos)
            result.push_back(id);
    }
    return result;
}

size_t Library::TrackCount() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fTracks.size();
}

int64_t Library::TotalDurationMs() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fTotalDuration;
}

int64_t Library::TotalBytes() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    return fTotalBytes;
}

std::vector<int64_t> Library::UpsertTracks(std::vector<Track>& tracks, bool rebuild)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    std::vector<int64_t> ids;
    Exec("BEGIN");
    Statement insert(fDb, "INSERT INTO tracks (source, uri, title, artist, album_artist, album, genre, year, track, disc, "
        "duration_ms, size, mtime, bitrate, ma_item_id, ma_album_uri, ma_artist_uri, art, date_added, in_library, lossless) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(uri) DO UPDATE SET source=excluded.source, "
        "title=excluded.title, artist=excluded.artist, album_artist=excluded.album_artist, album=excluded.album, "
        "genre=excluded.genre, year=excluded.year, track=excluded.track, disc=excluded.disc, duration_ms=excluded.duration_ms, "
        "size=excluded.size, mtime=excluded.mtime, bitrate=excluded.bitrate, ma_item_id=excluded.ma_item_id, "
        "ma_album_uri=excluded.ma_album_uri, ma_artist_uri=excluded.ma_artist_uri, art=excluded.art, in_library=excluded.in_library, "
        "lossless=excluded.lossless");
    int64_t now = (int64_t)time(nullptr);
    for (Track& t : tracks) {
        auto existing = fTrackByUri.find(t.uri);
        if (existing != fTrackByUri.end()) {
            t.id = existing->second;
            t.dateAdded = fTracks[t.id].dateAdded;
        } else
            t.dateAdded = now;
        insert.Reset();
        insert.Int(1, (int)t.source);
        insert.Text(2, t.uri);
        insert.Text(3, t.title);
        insert.Text(4, t.artist);
        insert.Text(5, t.albumArtist);
        insert.Text(6, t.album);
        insert.Text(7, t.genre);
        insert.Int(8, t.year);
        insert.Int(9, t.trackNumber);
        insert.Int(10, t.discNumber);
        insert.Int(11, t.durationMs);
        insert.Int(12, t.sizeBytes);
        insert.Int(13, t.modifiedTime);
        insert.Int(14, t.bitrate);
        insert.Text(15, t.maItemId);
        insert.Text(16, t.maAlbumUri);
        insert.Text(17, t.maArtistUri);
        insert.Text(18, t.art);
        insert.Int(19, t.dateAdded);
        insert.Int(20, t.inLibrary ? 1 : 0);
        insert.Int(21, t.lossless ? 1 : 0);
        insert.Done();
        if (t.id == 0)
            t.id = sqlite3_last_insert_rowid(fDb);
        fTrackByUri[t.uri] = t.id;
        fTracks[t.id] = t;
        ids.push_back(t.id);
    }
    Exec("COMMIT");
    if (rebuild)
        RebuildIndex();
    return ids;
}

void Library::RemoveTracksByUri(const std::vector<std::string>& uris, bool rebuild)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (uris.empty())
        return;
    Exec("BEGIN");
    Statement del(fDb, "DELETE FROM tracks WHERE uri = ?");
    Statement delPl(fDb, "DELETE FROM playlist_tracks WHERE track_id = ?");
    for (const std::string& uri : uris) {
        auto it = fTrackByUri.find(uri);
        if (it == fTrackByUri.end())
            continue;
        int64_t id = it->second;
        del.Reset();
        del.Text(1, uri);
        del.Done();
        delPl.Reset();
        delPl.Int(1, id);
        delPl.Done();
        fTracks.erase(id);
        fTrackByUri.erase(it);
        for (auto& entry : fPlaylists) {
            auto& ids = entry.second.trackIds;
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        }
    }
    Exec("COMMIT");
    if (rebuild)
        RebuildIndex();
}

std::vector<Track> Library::TracksForSource(Source source) const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    std::vector<Track> result;
    for (auto& entry : fTracks)
        if (entry.second.source == source)
            result.push_back(entry.second);
    return result;
}

std::map<std::string, std::pair<int64_t, int64_t>> Library::LocalFileIndex() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    std::map<std::string, std::pair<int64_t, int64_t>> index;
    for (auto& entry : fTracks)
        if (entry.second.source == Source::Local)
            index[entry.second.uri] = {entry.second.modifiedTime, entry.second.sizeBytes};
    return index;
}

void Library::SetAlbumArt(const std::string& artist, const std::string& album, const ArtKey& key)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    std::string groupKey = GroupKey(artist.empty() ? "Unknown Artist" : artist, album.empty() ? "Unknown Album" : album);
    fAlbumArtOverride[groupKey] = key;
    Statement s(fDb, "INSERT OR REPLACE INTO album_art (key, art) VALUES (?, ?)");
    s.Text(1, groupKey);
    s.Text(2, key);
    s.Done();
}

void Library::ApplyMASync(const MASyncResult& result)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    Exec("BEGIN");
    Exec("DELETE FROM ma_albums");
    Exec("DELETE FROM ma_artists");
    fMAAlbums.clear();
    fMAArtists.clear();
    {
        Statement s(fDb, "INSERT OR REPLACE INTO ma_albums (uri, item_id, name, artist, year, art) VALUES (?,?,?,?,?,?)");
        for (const Album& a : result.albums) {
            if (a.maUri.empty())
                continue;
            s.Reset();
            s.Text(1, a.maUri);
            s.Text(2, a.maItemId);
            s.Text(3, a.name);
            s.Text(4, a.artist);
            s.Int(5, a.year);
            s.Text(6, a.art);
            s.Done();
            fMAAlbums[a.maUri] = {a.maItemId, a.name, a.artist, a.year, a.art};
        }
        Statement t(fDb, "INSERT OR REPLACE INTO ma_artists (uri, item_id, name, art) VALUES (?,?,?,?)");
        for (const Artist& a : result.artists) {
            if (a.maUri.empty())
                continue;
            t.Reset();
            t.Text(1, a.maUri);
            t.Text(2, a.maItemId);
            t.Text(3, a.name);
            t.Text(4, a.art);
            t.Done();
            fMAArtists[a.maUri] = {a.maItemId, a.name, a.art};
        }
    }
    Exec("COMMIT");
    // tracks: upsert all, remove the MA tracks that vanished
    std::vector<Track> tracks;
    std::map<std::string, bool> seen;
    for (const Track& t : result.tracks) {
        if (t.uri.empty() || seen.count(t.uri))
            continue;
        seen[t.uri] = true;
        Track copy = t;
        auto ma = fMAAlbums.find(copy.maAlbumUri);
        if (ma != fMAAlbums.end()) {
            if (copy.art.empty())
                copy.art = ma->second.art;
            // track summaries carry no album artist; without it compilations would be split
            // into one album per track artist
            if (!ma->second.artist.empty())
                copy.albumArtist = ma->second.artist;
            if (copy.year == 0)
                copy.year = ma->second.year;
        }
        tracks.push_back(copy);
    }
    std::vector<std::string> stale;
    for (auto& entry : fTracks)
        if (entry.second.source == Source::MusicAssistant && !seen.count(entry.second.uri))
            stale.push_back(entry.second.uri);
    UpsertTracks(tracks, false);
    RemoveTracksByUri(stale, false);
    // playlists from the server
    std::map<std::string, int64_t> existingByItemId;
    for (auto& entry : fPlaylists)
        if (!entry.second.maItemId.empty())
            existingByItemId[entry.second.maItemId] = entry.first;
    std::map<std::string, bool> seenPlaylists;
    for (size_t i = 0; i < result.playlists.size(); i++) {
        const Playlist& remote = result.playlists[i];
        seenPlaylists[remote.maItemId] = true;
        std::vector<int64_t> ids;
        for (const std::string& uri : result.playlistTrackUris[i]) {
            auto it = fTrackByUri.find(uri);
            if (it != fTrackByUri.end())
                ids.push_back(it->second);
        }
        auto found = existingByItemId.find(remote.maItemId);
        if (found != existingByItemId.end()) {
            Playlist& local = fPlaylists[found->second];
            if (local.source == Source::MusicAssistant) {
                local.name = remote.name;
                if (!ids.empty())
                    local.trackIds = ids; // otherwise keep what was loaded on demand
                local.editable = remote.editable;
                local.dynamic = remote.dynamic;
                local.maUri = remote.maUri;
                SavePlaylist(local);
            }
            // a local playlist mirrored to MA keeps its local content as the source of truth
        } else {
            Playlist p = remote;
            p.id = 0;
            p.trackIds = ids;
            p.modified = (int64_t)time(nullptr);
            Statement s(fDb, "INSERT INTO playlists (source, name, ma_uri, ma_item_id, sync_ma, editable, modified) VALUES (?,?,?,?,?,?,?)");
            s.Int(1, (int)p.source);
            s.Text(2, p.name);
            s.Text(3, p.maUri);
            s.Text(4, p.maItemId);
            s.Int(5, 0);
            s.Int(6, p.editable ? 1 : 0);
            s.Int(7, p.modified);
            s.Done();
            p.id = sqlite3_last_insert_rowid(fDb);
            fPlaylists[p.id] = p;
            SavePlaylist(p);
        }
    }
    std::vector<int64_t> removed;
    for (auto& entry : fPlaylists)
        if (entry.second.source == Source::MusicAssistant && !seenPlaylists.count(entry.second.maItemId))
            removed.push_back(entry.first);
    for (int64_t id : removed)
        DeletePlaylist(id);
    RebuildIndex();
}

bool Library::HasMusicAssistantData() const
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    if (!fMAAlbums.empty() || !fMAArtists.empty())
        return true;
    for (auto& entry : fPlaylists)
        if (entry.second.isMA())
            return true;
    for (auto& entry : fTracks)
        if (entry.second.isMA())
            return true;
    return false;
}

void Library::ClearMusicAssistantData()
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    // Local playlists lose their streamed songs and their link to the server: a mirror that
    // stayed linked would replace the server's copy with what is left here on its next sync.
    std::vector<int64_t> changed;
    for (auto& entry : fPlaylists) {
        Playlist& playlist = entry.second;
        if (playlist.isMA())
            continue;
        size_t before = playlist.trackIds.size();
        playlist.trackIds.erase(std::remove_if(playlist.trackIds.begin(), playlist.trackIds.end(), [this](int64_t id) {
            auto track = fTracks.find(id);
            return track != fTracks.end() && track->second.isMA();
        }), playlist.trackIds.end());
        bool linked = playlist.syncToMA || !playlist.maItemId.empty() || !playlist.maUri.empty();
        if (playlist.trackIds.size() == before && !linked)
            continue;
        playlist.syncToMA = false;
        playlist.maItemId.clear();
        playlist.maUri.clear();
        changed.push_back(entry.first);
    }
    // Whole tables at once: a library of tens of thousands of streamed tracks would take
    // minutes when every track is looked up in the playlists on its own.
    const std::string source = std::to_string((int)Source::MusicAssistant);
    Exec("BEGIN");
    Exec(("DELETE FROM playlist_tracks WHERE playlist_id IN (SELECT id FROM playlists WHERE source = " + source + ")").c_str());
    Exec(("DELETE FROM playlists WHERE source = " + source).c_str());
    Exec(("DELETE FROM tracks WHERE source = " + source).c_str());
    Exec("DELETE FROM ma_albums");
    Exec("DELETE FROM ma_artists");
    Exec("COMMIT");
    for (int64_t id : changed)
        SavePlaylist(fPlaylists[id]);
    for (auto it = fPlaylists.begin(); it != fPlaylists.end();)
        it = it->second.isMA() ? fPlaylists.erase(it) : std::next(it);
    for (auto it = fTracks.begin(); it != fTracks.end();) {
        if (it->second.isMA()) {
            fTrackByUri.erase(it->second.uri);
            it = fTracks.erase(it);
        } else
            ++it;
    }
    fMAAlbums.clear();
    fMAArtists.clear();
    RebuildIndex();
}

// ---- playlists --------------------------------------------------------------

void Library::SavePlaylist(const Playlist& playlist)
{
    Exec("BEGIN");
    Statement upd(fDb, "UPDATE playlists SET source=?, name=?, ma_uri=?, ma_item_id=?, sync_ma=?, editable=?, modified=?, dynamic=? WHERE id=?");
    upd.Int(1, (int)playlist.source);
    upd.Text(2, playlist.name);
    upd.Text(3, playlist.maUri);
    upd.Text(4, playlist.maItemId);
    upd.Int(5, playlist.syncToMA ? 1 : 0);
    upd.Int(6, playlist.editable ? 1 : 0);
    upd.Int(7, playlist.modified);
    upd.Int(8, playlist.dynamic ? 1 : 0);
    upd.Int(9, playlist.id);
    upd.Done();
    Statement del(fDb, "DELETE FROM playlist_tracks WHERE playlist_id = ?");
    del.Int(1, playlist.id);
    del.Done();
    Statement ins(fDb, "INSERT INTO playlist_tracks (playlist_id, position, track_id) VALUES (?,?,?)");
    for (size_t i = 0; i < playlist.trackIds.size(); i++) {
        ins.Reset();
        ins.Int(1, playlist.id);
        ins.Int(2, (int64_t)i);
        ins.Int(3, playlist.trackIds[i]);
        ins.Done();
    }
    Exec("COMMIT");
}

int64_t Library::CreatePlaylist(const std::string& name, Source source)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    Playlist p;
    p.source = source;
    p.name = name;
    p.modified = (int64_t)time(nullptr);
    Statement s(fDb, "INSERT INTO playlists (source, name, ma_uri, ma_item_id, sync_ma, editable, modified) VALUES (?,?,?,?,?,?,?)");
    s.Int(1, (int)source);
    s.Text(2, name);
    s.Text(3, "");
    s.Text(4, "");
    s.Int(5, 0);
    s.Int(6, 1);
    s.Int(7, p.modified);
    s.Done();
    p.id = sqlite3_last_insert_rowid(fDb);
    fPlaylists[p.id] = p;
    if (onChanged)
        onChanged();
    return p.id;
}

bool Library::RenamePlaylist(int64_t id, const std::string& name)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    it->second.name = name;
    it->second.modified = (int64_t)time(nullptr);
    SavePlaylist(it->second);
    if (onChanged)
        onChanged();
    return true;
}

bool Library::DeletePlaylist(int64_t id)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    Statement del(fDb, "DELETE FROM playlists WHERE id = ?");
    del.Int(1, id);
    del.Done();
    Statement delTracks(fDb, "DELETE FROM playlist_tracks WHERE playlist_id = ?");
    delTracks.Int(1, id);
    delTracks.Done();
    fPlaylists.erase(it);
    if (onChanged)
        onChanged();
    return true;
}

bool Library::AddToPlaylist(int64_t id, const std::vector<int64_t>& trackIds, int position)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    Playlist& p = it->second;
    std::vector<int64_t> valid;
    for (int64_t trackId : trackIds)
        if (fTracks.count(trackId))
            valid.push_back(trackId);
    if (position < 0 || position > (int)p.trackIds.size())
        p.trackIds.insert(p.trackIds.end(), valid.begin(), valid.end());
    else
        p.trackIds.insert(p.trackIds.begin() + position, valid.begin(), valid.end());
    p.modified = (int64_t)time(nullptr);
    SavePlaylist(p);
    if (onChanged)
        onChanged();
    return true;
}

bool Library::RemoveFromPlaylist(int64_t id, const std::vector<int>& positions)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    Playlist& p = it->second;
    std::vector<int> sorted = positions;
    std::sort(sorted.begin(), sorted.end());
    for (auto rit = sorted.rbegin(); rit != sorted.rend(); ++rit)
        if (*rit >= 0 && *rit < (int)p.trackIds.size())
            p.trackIds.erase(p.trackIds.begin() + *rit);
    p.modified = (int64_t)time(nullptr);
    SavePlaylist(p);
    if (onChanged)
        onChanged();
    return true;
}

bool Library::MoveInPlaylist(int64_t id, const std::vector<int>& positions, int target)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    Playlist& p = it->second;
    std::vector<int> sorted = positions;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int64_t> moving;
    std::vector<int64_t> remaining;
    int insertAt = target;
    for (int i = 0; i < (int)p.trackIds.size(); i++) {
        if (std::binary_search(sorted.begin(), sorted.end(), i)) {
            moving.push_back(p.trackIds[i]);
            if (i < target)
                insertAt--;
        } else
            remaining.push_back(p.trackIds[i]);
    }
    if (insertAt < 0)
        insertAt = 0;
    if (insertAt > (int)remaining.size())
        insertAt = (int)remaining.size();
    remaining.insert(remaining.begin() + insertAt, moving.begin(), moving.end());
    p.trackIds = remaining;
    p.modified = (int64_t)time(nullptr);
    SavePlaylist(p);
    if (onChanged)
        onChanged();
    return true;
}

bool Library::SetPlaylistTracks(int64_t id, const std::vector<int64_t>& trackIds)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    it->second.trackIds = trackIds;
    it->second.modified = (int64_t)time(nullptr);
    SavePlaylist(it->second);
    if (onChanged)
        onChanged();
    return true;
}

bool Library::LinkPlaylistToMA(int64_t id, const std::string& maItemId, const std::string& maUri, bool sync)
{
    std::lock_guard<std::recursive_mutex> lock(fMutex);
    auto it = fPlaylists.find(id);
    if (it == fPlaylists.end())
        return false;
    it->second.maItemId = maItemId;
    it->second.maUri = maUri;
    it->second.syncToMA = sync;
    SavePlaylist(it->second);
    if (onChanged)
        onChanged();
    return true;
}

} // namespace amp
