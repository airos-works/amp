// The Amp application: owns the library, settings, caches and the player; runs background jobs.
#pragma once
#include "ArtStore.h"
#include "core/ImageCache.h"
#include "core/Library.h"
#include "core/Scanner.h"
#include "core/Settings.h"
#include "player/MiniDisc.h"
#include "player/Player.h"
#include <Application.h>
#include <memory>
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
    MiniDiscManager& MiniDisc() { return *fMiniDisc; }
    MainWindow* Window() { return fWindow; }
    void HandleMediaKey(BMessage* message);

    void StartScan();
    void ApplySettingsChanged();
    std::string SettingsDirectory() const { return fSettingsDir; }

    static AmpApp* Instance();

private:
    // A one-off line for the status bar (kMsgScanProgress without "scan").
    void PostStatus(const std::string& text);

    std::string fSettingsDir;
    std::string fCacheDir;
    std::unique_ptr<Settings> fSettings;
    std::unique_ptr<Library> fLibrary;
    std::unique_ptr<ImageCache> fImages;
    std::unique_ptr<ArtStore> fArt;
    std::unique_ptr<Player> fPlayer;
    std::unique_ptr<Scanner> fScanner;
    std::unique_ptr<MiniDiscManager> fMiniDisc;
    MainWindow* fWindow = nullptr;
};

inline AmpApp* App() { return AmpApp::Instance(); }

} // namespace amp
