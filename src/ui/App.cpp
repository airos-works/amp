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
#include <thread>

namespace amp {

namespace {
const char* kSignature = "application/x-vnd.Amp";
const int kScannerVersion = 3; // 2: lossless flag and estimated bit rates; 3: FLAC in MP4 is lossless

// The app used to be called TasAmp. Move the old user data over once so the
// Music Assistant account, library and artwork cache survive the rename.
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

// The Music Assistant player name was stored in the settings file: keep it in
// step with the new application name.
bool RenamePlayerName(SettingsData& data)
{
    size_t found = data.maPlayerName.find("TasAmp");
    if (found == std::string::npos)
        return false;
    std::string name;
    while (found != std::string::npos) {
        name.append(data.maPlayerName, 0, found);
        name.append("Amp");
        data.maPlayerName.erase(0, found + 6);
        found = data.maPlayerName.find("TasAmp");
    }
    name.append(data.maPlayerName);
    data.maPlayerName = name;
    return true;
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
    bool settingsChanged = RenamePlayerName(data);
    if (data.maPlayerId.empty()) {
        data.maPlayerId = GenerateClientId();
        settingsChanged = true;
    }
    if (settingsChanged)
        fSettings->Update(data);
    fLibrary.reset(new Library(fSettingsDir + "/library.db"));
    std::string error;
    if (!fLibrary->Open(error))
        fprintf(stderr, "Amp: cannot open library: %s\n", error.c_str());
    // Music Assistant is off: whatever an earlier version left behind goes before it is shown
    fMAEnabled = data.maEnabled && !data.maHost.empty();
    if (!fMAEnabled && fLibrary->HasMusicAssistantData())
        fLibrary->ClearMusicAssistantData();
    fImages.reset(new ImageCache(fCacheDir, *fSettings));
    fImages->Open();
    fMA.reset(new MusicAssistant());
    fMA->Configure(data.maHost, data.maPort, data.maUsername, data.maPassword, data.maToken);
    fArt.reset(new ArtStore(*fImages, *fMA));
    fPlayer.reset(new Player(*fLibrary, *fMA));
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
    if (data.maEnabled && !data.maHost.empty())
        ConnectMusicAssistant(true);
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
        case kMsgMAResync:
            if (fMAEnabled)
                ConnectMusicAssistant(true);
            else
                PostStatus("Music Assistant is switched off in Settings");
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
    SettingsData data = fSettings->Get();
    fMA->Configure(data.maHost, data.maPort, data.maUsername, data.maPassword, data.maToken);
    if (data.maEnabled && !data.maHost.empty())
        ConnectMusicAssistant(true);
    else
        DisconnectMusicAssistant();
    StartScan();
    if (fWindow)
        BMessenger(fWindow).SendMessage(kMsgLibraryChanged);
}

bool AmpApp::WhileMAEnabled(const std::function<void()>& change)
{
    std::lock_guard<std::mutex> lock(fMAMutex);
    if (!fMAEnabled)
        return false;
    change();
    return true;
}

void AmpApp::ConnectMusicAssistant(bool resync)
{
    {
        std::lock_guard<std::mutex> lock(fMAMutex);
        fMAEnabled = true;
    }
    if (fMASyncRunning)
        return;
    fMASyncRunning = true;
    std::thread([this, resync] { MAConnectWorker(resync); }).detach();
}

void AmpApp::MAConnectWorker(bool resync)
{
    // `syncing`: the library is being fetched; the display follows the sync until kMsgMASyncDone
    auto status = [this](bool connected, const std::string& text, bool syncing = false) {
        BMessage message(kMsgMAStatus);
        message.AddBool("connected", connected);
        message.AddString("message", text.c_str());
        message.AddBool("syncing", syncing);
        if (fWindow)
            BMessenger(fWindow).SendMessage(&message);
    };
    status(false, "Connecting to Music Assistant…");
    std::string error;
    if (!fMA->EnsureAuthenticated(error)) {
        status(false, "Music Assistant login failed: " + error);
        fMASyncRunning = false;
        return;
    }
    if (fMA->Token() != fSettings->Get().maToken)
        fSettings->Modify([this](SettingsData& d) { d.maToken = fMA->Token(); });
    SettingsData data = fSettings->Get();
    bool enabled = WhileMAEnabled([&] {
        fPlayer->EnableMusicAssistant(fMA->Host(), fMA->Port(), fMA->Token(), data.maPlayerId, data.maPlayerName);
        fMAConnected = true;
    });
    if (!enabled) {
        // switched off while the login was under way
        fMASyncRunning = false;
        return;
    }
    if (resync) {
        MASyncResult result;
        status(true, "Music Assistant: fetching the library", true);
        bool ok = fMA->FetchLibrary(result, [&](const std::string& text) {
            if (fMAEnabled)
                status(true, "Music Assistant: " + text, true);
        });
        if (!fMAEnabled) {
            fMASyncRunning = false;
            return;
        }
        if (ok) {
            if (!WhileMAEnabled([&] { fLibrary->ApplyMASync(result); })) {
                fMASyncRunning = false;
                return;
            }
            status(true, "Music Assistant library synced: " + std::to_string(result.tracks.size()) + " tracks");
        } else
            status(true, "Music Assistant sync failed: " + result.error);
        BMessage done(kMsgMASyncDone);
        done.AddString("error", ok ? "" : result.error.c_str());
        if (fWindow)
            BMessenger(fWindow).SendMessage(&done);
    }
    fMASyncRunning = false;
}

void AmpApp::DisconnectMusicAssistant()
{
    std::lock_guard<std::mutex> lock(fMAMutex);
    fMAEnabled = false;
    // a streamed song stops while the server can still be told
    fPlayer->StopMusicAssistantTrack();
    fPlayer->DisableMusicAssistant();
    fMAConnected = false;
    RemoveMusicAssistantContent();
    BMessage message(kMsgMAStatus);
    message.AddBool("connected", false);
    message.AddString("message", "Music Assistant disabled");
    if (fWindow)
        BMessenger(fWindow).SendMessage(&message);
}

void AmpApp::RemoveMusicAssistantContent()
{
    if (!fLibrary->HasMusicAssistantData())
        return;
    fLibrary->ClearMusicAssistantData();
    fPlayer->DropMissingTracks();
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fPlaylistSyncQueue.clear();
    }
    if (fWindow)
        BMessenger(fWindow).SendMessage(kMsgMACleared);
}

void AmpApp::SyncPlaylistToMA(int64_t playlistId)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fPlaylistSyncQueue.insert(playlistId);
        if (fPlaylistSyncRunning)
            return;
        fPlaylistSyncRunning = true;
    }
    std::thread([this] {
        while (true) {
            int64_t id;
            {
                std::lock_guard<std::mutex> lock(fMutex);
                if (fPlaylistSyncQueue.empty()) {
                    fPlaylistSyncRunning = false;
                    return;
                }
                id = *fPlaylistSyncQueue.begin();
                fPlaylistSyncQueue.erase(fPlaylistSyncQueue.begin());
            }
            PlaylistSyncWorker(id);
        }
    }).detach();
}

bool AmpApp::LoadMAPlaylist(int64_t playlistId)
{
    std::string itemId, name;
    {
        Library::Locker locker(*fLibrary);
        const Playlist* playlist = fLibrary->PlaylistById(playlistId);
        if (!playlist || !playlist->isMA() || playlist->maItemId.empty())
            return false;
        itemId = playlist->maItemId;
        name = playlist->name;
    }
    {
        std::lock_guard<std::mutex> lock(fMutex);
        if (!fPlaylistLoads.insert(playlistId).second)
            return true; // already loading
    }
    std::thread([this, playlistId, itemId, name] {
        auto status = [this](const std::string& text) {
            BMessage message(kMsgScanProgress);
            message.AddString("text", text.c_str());
            message.AddBool("done", true);
            if (fWindow)
                BMessenger(fWindow).SendMessage(&message);
        };
        status("Loading playlist \"" + name + "\" from Music Assistant…");
        std::vector<Track> tracks;
        std::string error;
        if (fMA->EnsureAuthenticated(error) && fMA->FetchLibraryPlaylistTracks(itemId, tracks, error)) {
            size_t count = 0;
            bool stored = WhileMAEnabled([&] {
                {
                    // switched off and on again in the meantime: the playlist is another one now
                    Library::Locker locker(*fLibrary);
                    if (fLibrary->PlaylistById(playlistId) == nullptr)
                        return;
                }
                std::vector<int64_t> ids = fLibrary->UpsertTracks(tracks, false);
                count = ids.size();
                bool changed;
                {
                    Library::Locker locker(*fLibrary);
                    const Playlist* playlist = fLibrary->PlaylistById(playlistId);
                    changed = !playlist || playlist->trackIds != ids;
                }
                if (changed) {
                    fLibrary->SetPlaylistTracks(playlistId, ids);
                    fLibrary->RebuildIndex();
                }
            });
            if (stored)
                status("Playlist \"" + name + "\": " + std::to_string(count) + " tracks");
        } else if (fMAEnabled)
            status("Cannot load playlist \"" + name + "\": " + error);
        {
            std::lock_guard<std::mutex> lock(fMutex);
            fPlaylistLoads.erase(playlistId);
        }
        BMessage finished(kMsgPlaylistLoading);
        finished.AddInt64("playlist", playlistId);
        finished.AddBool("loading", false);
        if (fWindow)
            BMessenger(fWindow).SendMessage(&finished);
    }).detach();
    return true;
}

void AmpApp::PlaylistSyncWorker(int64_t playlistId)
{
    auto status = [this](const std::string& text) {
        BMessage message(kMsgScanProgress);
        message.AddString("text", text.c_str());
        message.AddBool("done", true);
        if (fWindow)
            BMessenger(fWindow).SendMessage(&message);
    };
    std::string name, maItemId;
    std::vector<std::string> uris;
    int skipped = 0;
    {
        Library::Locker locker(*fLibrary);
        const Playlist* playlist = fLibrary->PlaylistById(playlistId);
        if (!playlist || !playlist->syncToMA || !fMAEnabled)
            return;
        name = playlist->name;
        maItemId = playlist->maItemId;
        for (int64_t trackId : playlist->trackIds) {
            const Track* track = fLibrary->TrackById(trackId);
            if (track && track->isMA())
                uris.push_back(track->uri);
            else
                skipped++;
        }
    }
    std::string error;
    if (!fMA->EnsureAuthenticated(error)) {
        status("Playlist sync failed: " + error);
        return;
    }
    if (maItemId.empty()) {
        Playlist created;
        if (!fMA->CreatePlaylist(name, created, error)) {
            status("Cannot create playlist on Music Assistant: " + error);
            return;
        }
        maItemId = created.maItemId;
        if (!WhileMAEnabled([&] { fLibrary->LinkPlaylistToMA(playlistId, created.maItemId, created.maUri, true); }))
            return;
    }
    // Replace the server-side content: remove everything, then add in order.
    std::vector<Track> current;
    if (fMA->FetchLibraryPlaylistTracks(maItemId, current, error) && !current.empty()) {
        std::vector<int> positions;
        for (int i = 0; i < (int)current.size(); i++)
            positions.push_back(i);
        fMA->RemovePlaylistTracks(maItemId, positions, error);
        snooze(500000); // the removal runs as a background task on the server
    }
    error.clear();
    if (!fMA->AddPlaylistTracks(maItemId, uris, error)) {
        status("Playlist sync failed: " + error);
        return;
    }
    status("Playlist \"" + name + "\" synced to Music Assistant (" + std::to_string(uris.size()) + " tracks"
        + (skipped ? ", " + std::to_string(skipped) + " local tracks skipped)" : ")"));
}

} // namespace amp
