// The Amp application: owns the library, settings, caches and the player; runs background jobs.
#pragma once
#include "ArtStore.h"
#include "core/ImageCache.h"
#include "core/Library.h"
#include "core/MusicAssistant.h"
#include "core/Scanner.h"
#include "core/Settings.h"
#include "player/MiniDisc.h"
#include "player/Player.h"
#include <Application.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace amp {

class MainWindow;

class AmpApp : public BApplication {
public:
    AmpApp();
    virtual ~AmpApp();

    void ReadyToRun() override;
    bool QuitRequested() override;
    void MessageReceived(BMessage* message) override;
    void RefsReceived(BMessage* message) override;
    void ArgvReceived(int32 argc, char** argv) override;

    Library& GetLibrary() { return *fLibrary; }
    Settings& GetSettings() { return *fSettings; }
    ImageCache& Images() { return *fImages; }
    ArtStore& Art() { return *fArt; }
    Player& GetPlayer() { return *fPlayer; }
    MusicAssistant& MA() { return *fMA; }
    MiniDiscManager& MiniDisc() { return *fMiniDisc; }
    MainWindow* Window() { return fWindow; }
    void HandleMediaKey(BMessage* message);

    void StartScan();
    void ConnectMusicAssistant(bool resync);
    void DisconnectMusicAssistant();
    void SyncPlaylistToMA(int64_t playlistId);
    // Fetches an MA playlist's tracks in the background; false when nothing was started.
    bool LoadMAPlaylist(int64_t playlistId);
    void ApplySettingsChanged();
    std::string SettingsDirectory() const { return fSettingsDir; }
    bool MASyncRunning() const { return fMASyncRunning; }
    bool MAEnabled() const { return fMAEnabled; }

    static AmpApp* Instance();

private:
    // A one-off line for the status bar (kMsgScanProgress without "scan").
    void PostStatus(const std::string& text);
    void MAConnectWorker(bool resync);
    // Runs `change` unless Music Assistant was switched off in the meantime: what a worker
    // fetched from the server must not come back into a library that was just cleared.
    bool WhileMAEnabled(const std::function<void()>& change);
    void RemoveMusicAssistantContent();
    void PlaylistSyncWorker(int64_t playlistId);

    std::string fSettingsDir;
    std::string fCacheDir;
    std::unique_ptr<Settings> fSettings;
    std::unique_ptr<Library> fLibrary;
    std::unique_ptr<ImageCache> fImages;
    std::unique_ptr<MusicAssistant> fMA;
    std::unique_ptr<ArtStore> fArt;
    std::unique_ptr<Player> fPlayer;
    std::unique_ptr<Scanner> fScanner;
    std::unique_ptr<MiniDiscManager> fMiniDisc;
    MainWindow* fWindow = nullptr;
    std::atomic<bool> fMASyncRunning{false};
    std::atomic<bool> fMAConnected{false};
    std::atomic<bool> fMAEnabled{false};
    std::mutex fMAMutex;                       // held while fMAEnabled changes or is acted on
    std::vector<std::string> fScanFolders;
    std::mutex fMutex;
    std::set<int64_t> fPlaylistSyncQueue;
    std::set<int64_t> fPlaylistLoads;
    std::atomic<bool> fPlaylistSyncRunning{false};
};

inline AmpApp* App() { return AmpApp::Instance(); }

} // namespace amp
