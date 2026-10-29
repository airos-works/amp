// Amp library data model: plain structs shared by the store, the UI and the player.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace amp {

// Identifies an image in the artwork cache. Empty key means "no artwork known".
// Keys are stable strings such as "album:<artist>|<album>" or "artist:<name>".
using ArtKey = std::string;

struct Track {
    int64_t id = 0;               // library row id
    std::string uri;              // absolute path
    std::string title;
    std::string artist;           // display artist(s)
    std::string albumArtist;      // grouping artist (falls back to artist)
    std::string album;
    std::string genre;
    int year = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int64_t durationMs = 0;
    int64_t sizeBytes = 0;
    int64_t modifiedTime = 0;     // mtime (seconds)
    int bitrate = 0;              // kbit/s (0 when unknown)
    bool lossless = false;        // FLAC, WAV, ALAC, AIFF ...
    ArtKey art;                   // album art key
    int64_t albumId = 0;          // computed album row
    int64_t dateAdded = 0;

    const std::string& groupingArtist() const { return albumArtist.empty() ? artist : albumArtist; }
};

struct Album {
    int64_t id = 0;
    std::string name;
    std::string artist;           // album artist
    std::string sortKey;
    int year = 0;
    ArtKey art;
    int trackCount = 0;
    int64_t durationMs = 0;
    std::vector<int64_t> trackIds; // in disc/track order (filled by the in-memory index)
};

struct Artist {
    int64_t id = 0;
    std::string name;
    std::string sortKey;
    ArtKey art;                   // artist image key
    int albumCount = 0;
    int trackCount = 0;
    std::vector<int64_t> albumIds;
    std::vector<int64_t> trackIds;
};

struct Playlist {
    int64_t id = 0;
    std::string name;
    std::vector<int64_t> trackIds;   // ordered
    int64_t modified = 0;
};

// Where a piece of artwork can be obtained; used by the image cache workers.
struct ArtRequest {
    ArtKey key;
    std::string artist;
    std::string album;         // empty for artist images
    std::string localHintPath; // a track path whose folder/tags may contain art
    bool artistImage = false;
};

std::string MakeAlbumArtKey(const std::string& artist, const std::string& album);
std::string MakeArtistArtKey(const std::string& artist);
std::string FormatDuration(int64_t ms);
std::string QualityLabel(bool lossless, int bitrateKbps); // "Lossless", "320 kbps" or ""
std::string SortKeyFor(const std::string& name); // strips "The ", lowercases
std::string ToLower(const std::string& text);
bool ContainsNoCase(const std::string& haystack, const std::string& needle);

} // namespace amp
