#include "App.h"
#include "Icons.h"
#include "MainWindow.h"
#include "core/ArtistLinks.h"
#include "core/Http.h"
#include "player/Messages.h"
#include "player/MediaKeys.h"
#include <Alert.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <cstdio>
#include <cstdlib>

namespace amp {

namespace {
const char* kSignature = "application/x-vnd.Amp";
const int kScannerVersion = 3; // 2: lossless flag and estimated bit rates; 3: FLAC in MP4 is lossless

// The app used to be called TasAmp. Move the old user data over once so the
// settings, library and artwork cache survive the rename.
void MigrateRenamedDirectory(const BPath& parent, const char* oldName, const char* newName)
{
    BPath target(parent);
    target.Append(newName);
    BEntry targetEntry(target.Path());
    if (targetEntry.Exists())
        return; // there is already data under the new name
    BPath source(parent);
    source.Append(oldName);
    BEntry sourceEntry(source.Path());
    if (!sourceEntry.Exists())
        return;
    if (rename(source.Path(), target.Path()) != B_OK)
        fprintf(stderr, "Amp: cannot move %s to %s\n", source.Path(), target.Path());
}
}

AmpApp* AmpApp::Instance()
{
    return static_cast<AmpApp*>(be_app);
}

AmpApp::AmpApp()
    : BApplication(kSignature)
{
    BPath settingsBase, cacheBase;
    // AMP_DATA_DIR keeps settings, library and artwork in a folder of its own: a build under
    // test can run next to the installed Amp without touching the library in use
    const char* dataDir = getenv("AMP_DATA_DIR");
    if (dataDir && *dataDir) {
        settingsBase.SetTo(dataDir, "settings");
        cacheBase.SetTo(dataDir, "cache");
        create_directory(settingsBase.Path(), 0755);
        create_directory(cacheBase.Path(), 0755);
    } else {
        find_directory(B_USER_SETTINGS_DIRECTORY, &settingsBase, true);
        find_directory(B_USER_CACHE_DIRECTORY, &cacheBase, true);
        MigrateRenamedDirectory(settingsBase, "TasAmp", "Amp");
        MigrateRenamedDirectory(cacheBase, "TasAmp", "Amp");
    }
    BPath path(settingsBase);
    path.Append("Amp");
    create_directory(path.Path(), 0755);
    fSettingsDir = path.Path();
    BPath cache(cacheBase);
    cache.Append("Amp");
    create_directory(cache.Path(), 0755);
    ArtistLinks::Shared().Open(cache.Path());
    cache.Append("art");
    create_directory(cache.Path(), 0755);
    fCacheDir = cache.Path();

    Http::GlobalInit();
    icons::Init();
    fSettings.reset(new Settings(fSettingsDir + "/settings.json"));
    fSettings->Load();
    SettingsData data = fSettings->Get();
    fLibrary.reset(new Library(fSettingsDir + "/library.db"));
    std::string error;
    if (!fLibrary->Open(error))
        fprintf(stderr, "Amp: cannot open library: %s\n", error.c_str());
    fImages.reset(new ImageCache(fCacheDir, *fSettings));
    fImages->Open();
    fArt.reset(new ArtStore(*fImages));
    fPlayer.reset(new Player(*fLibrary));
    fPlayer->SetVolume(data.volume);
    fPlayer->SetShuffle(data.shuffle);
    fPlayer->SetRepeat((RepeatMode)data.repeat);
    fScanner.reset(new Scanner(*fLibrary, *fImages));
    fMiniDisc.reset(new MiniDiscManager());
    fScanner->onProgress = [this](const std::string& text, bool done) {
        BMessage message(kMsgScanProgress);
        message.AddString("text", text.c_str());
        message.AddBool("done", done);
        // the library scan itself: the display follows it (one-off lines come from PostStatus)
        message.AddBool("scan", true);
        message.AddInt32("processed", fScanner->Processed());
        message.AddInt32("total", fScanner->Total());
        if (fWindow)
            BMessenger(fWindow).SendMessage(&message);
    };
    fScanner->durationProbe = [](const std::string& path) -> int64_t {
        entry_ref ref;
        if (get_ref_for_path(path.c_str(), &ref) != B_OK)
            return 0;
        BMediaFile file(&ref);
        if (file.InitCheck() != B_OK)
            return 0;
        for (int32 i = 0; i < file.CountTracks(); i++) {
            BMediaTrack* track = file.TrackAt(i);
            if (!track)
                continue;
            media_format format;
            bigtime_t duration = 0;
            if (track->EncodedFormat(&format) == B_OK && format.IsAudio())
                duration = track->Duration();
            file.ReleaseTrack(track);
            if (duration > 0)
                return duration / 1000;
        }
        return 0;
    };
    fLibrary->onChanged = [this] {
        if (fWindow)
            BMessenger(fWindow).SendMessage(kMsgLibraryChanged);
    };
}

AmpApp::~AmpApp()
{
}

void AmpApp::ReadyToRun()
{
    SettingsData data = fSettings->Get();
    BRect frame(data.windowX, data.windowY, data.windowX + data.windowW, data.windowY + data.windowH);
    fWindow = new MainWindow(frame);
    BMessenger target(fWindow);
    fArt->SetTarget(target);
    fPlayer->SetTarget(target);
    fPlayer->SetControl(BMessenger(this));
    fWindow->Show();
    fImages->Start();
    fArt->Start();
    fPlayer->Start();
    fMiniDisc->Start(target);
    if (!data.libraryFolders.empty())
        StartScan();
}

bool AmpApp::QuitRequested()
{
    fScanner->Stop();
    fMiniDisc->Stop();
    fPlayer->Shutdown();
    fArt->Stop();
    fImages->Stop();
    fSettings->Save();
    return BApplication::QuitRequested();
}

void AmpApp::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgSettingsChanged:
            ApplySettingsChanged();
            break;
        case kMsgRescan:
            StartScan();
            break;
        case kMsgTrackFinished: {
            int32 generation = 0;
            message->FindInt32("generation", &generation);
            fPlayer->HandleTrackFinished(generation);
            break;
        }
        case kMsgMediaKey:
            HandleMediaKey(message);
            break;
        default:
            BApplication::MessageReceived(message);
    }
}

// A keyboard's media key, from the system's mediakey command (see MediaKeys.h): a query asks
// whether Amp plays right now; the key itself goes to the main window like its menu items.
void AmpApp::HandleMediaKey(BMessage* message)
{
    int32 key = message->GetInt32("key", -1);
    bool playing = fPlayer->State() == kPlaying;
    bool canHandle = fWindow != nullptr && key >= B_MEDIA_KEY_PLAY_PAUSE && key <= B_MEDIA_KEY_PREVIOUS;
    if (message->GetBool("query", false)) {
        reply_media_key_query(message, canHandle, playing);
        return;
    }
    if (canHandle) {
        switch (key) {
            case B_MEDIA_KEY_PLAY_PAUSE:
                fWindow->PostMessage(kMsgPlayPause);
                break;
            case B_MEDIA_KEY_PLAY:
                if (!playing)
                    fWindow->PostMessage(kMsgPlayPause);
                break;
            case B_MEDIA_KEY_PAUSE:
                if (playing)
                    fWindow->PostMessage(kMsgPlayPause);
                break;
            case B_MEDIA_KEY_STOP:
                fWindow->PostMessage(kMsgStop);
                break;
            case B_MEDIA_KEY_NEXT:
                fWindow->PostMessage(kMsgNext);
                break;
            case B_MEDIA_KEY_PREVIOUS:
                fWindow->PostMessage(kMsgPrevious);
                break;
        }
    }
    reply_media_key_handled(message, canHandle);
}

void AmpApp::RefsReceived(BMessage* message)
{
    // Files dropped on the app or opened from Tracker: play them directly.
    std::vector<Track> tracks;
    entry_ref ref;
    for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
        BPath path(&ref);
        Track track;
        if (Scanner::ReadTrack(path.Path(), track, nullptr, nullptr))
            tracks.push_back(track);
    }
    if (tracks.empty())
        return;
    std::vector<int64_t> ids = fLibrary->UpsertTracks(tracks, true);
    fPlayer->PlayTracks(ids, 0);
}

void AmpApp::ArgvReceived(int32 argc, char** argv)
{
    BMessage refs(B_REFS_RECEIVED);
    for (int32 i = 1; i < argc; i++) {
        entry_ref ref;
        if (get_ref_for_path(argv[i], &ref) == B_OK)
            refs.AddRef("refs", &ref);
    }
    if (refs.HasRef("refs"))
        RefsReceived(&refs);
}

void AmpApp::PostStatus(const std::string& text)
{
    BMessage message(kMsgScanProgress);
    message.AddString("text", text.c_str());
    message.AddBool("done", true);
    if (fWindow)
        BMessenger(fWindow).SendMessage(&message);
}

void AmpApp::StartScan()
{
    SettingsData data = fSettings->Get();
    if (data.libraryFolders.empty()) {
        PostStatus("No library folders configured");
        return;
    }
    // a newer scanner reads fields older versions did not store: read every file once more
    bool force = data.scannerVersion < kScannerVersion;
    if (force)
        fSettings->Modify([](SettingsData& d) { d.scannerVersion = kScannerVersion; });
    fScanner->Start(data.libraryFolders, force);
}

void AmpApp::ApplySettingsChanged()
{
    StartScan();
    if (fWindow)
        BMessenger(fWindow).SendMessage(kMsgLibraryChanged);
}

} // namespace amp
