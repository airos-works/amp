// Persistent user settings stored as JSON in the user's settings directory.
#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace amp {

struct ArtSource {
    std::string id;      // "deezer", "musicbrainz", "theaudiodb", "discogs"
    bool enabled = true;
    std::string apiKey;  // TheAudioDB key / Discogs token (optional)
};

struct SettingsData {
    std::vector<std::string> libraryFolders;
    // Artwork
    std::vector<ArtSource> artSources;
    bool fetchOnlineArt = true;
    // UI / playback
    float volume = 0.8f;
    int viewMode = 0;             // library views: 0 list, 1 grouped, 2 grid
    int playlistViewMode = 0;     // playlists remember their own mode
    std::string selectedSource = "music";
    bool shuffle = false;
    int repeat = 0;               // 0 off, 1 all, 2 one
    int windowX = 60, windowY = 60, windowW = 1180, windowH = 720;
    int sidebarWidth = 200;
    int scannerVersion = 0;       // bumped when the scanner learns new fields: forces one full rescan
};

class Settings {
public:
    explicit Settings(const std::string& path);
    bool Load();
    bool Save();

    // Returns a copy; call Update() to change.
    SettingsData Get() const;
    void Update(const SettingsData& data);
    template<typename F> void Modify(F f) { { std::lock_guard<std::mutex> lock(fMutex); f(fData); } Save(); }

    static std::vector<ArtSource> DefaultArtSources();

private:
    std::string fPath;
    mutable std::mutex fMutex;
    SettingsData fData;
};

} // namespace amp
