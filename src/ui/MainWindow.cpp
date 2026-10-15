#include "MainWindow.h"
#include "App.h"
#include "NameDialog.h"
#include "SettingsWindow.h"
#include "Theme.h"
#include "core/ArtistLinks.h"
#include <Alert.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <ScrollView.h>
#include <Url.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <thread>

namespace amp {

namespace {
const uint32 kMsgAbout = 'abou';
const uint32 kMsgReloadNow = 'rlnw';
const uint32 kMsgActivityExpired = 'acex';   // "id": a finished activity leaves the display
}

MainWindow::MainWindow(BRect frame)
    : BWindow(frame, "Amp", B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
    SettingsData settings = App()->GetSettings().Get();
    fViewMode = settings.viewMode;
    fPlaylistViewMode = settings.playlistViewMode;
    fSource = settings.selectedSource.empty() ? "music" : settings.selectedSource;
    if (fSource == "playlist" || fSource == "minidisc")
        fSource = "music";

    fToolbar = new ToolbarView();
    fSidebar = new SidebarView();
    fStatus = new StatusBarView();
    fTrackList = new TrackListView("tracks");
    fGrid = new AlbumGridView();
    fArtists = new ArtistsView();
    fMiniDiscView = new MiniDiscView();
    BScrollView* trackScroll = new BScrollView("track-scroll", fTrackList, 0, false, true, B_NO_BORDER);
    // the column header is a sibling above the scroll view: it never scrolls
    BView* listPane = new BView("list-pane", 0);
    BLayoutBuilder::Group<>(listPane, B_VERTICAL, 0)
        .Add(fTrackList->HeaderView())
        .Add(trackScroll);
    BScrollView* gridScroll = new BScrollView("grid-scroll", fGrid, 0, false, true, B_NO_BORDER);
    BScrollView* sidebarScroll = new BScrollView("sidebar-scroll", fSidebar, 0, false, true, B_NO_BORDER);
    fContent = new BView("content", 0);
    fCards = new BCardLayout();
    fContent->SetLayout(fCards);
    fCards->AddView(listPane);
    fCards->AddView(gridScroll);
    fCards->AddView(fArtists);
    fCards->AddView(fMiniDiscView);
    fSplit = new BSplitView(B_HORIZONTAL, 0);
    fSplit->AddChild(sidebarScroll, 0.0f);
    fSplit->AddChild(fContent, 1.0f);
    fSplit->SetCollapsible(0, false);
    fSplit->SetCollapsible(1, false);
    float sidebarWidth = std::max(200, std::min(420, settings.sidebarWidth));
    fSplit->SetItemWeight(0, sidebarWidth, false);
    fSplit->SetItemWeight(1, frame.Width() - sidebarWidth, true);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fToolbar)
        .Add(fSplit)
        .Add(fStatus);
    BuildMenu();
    fToolbar->SetVolume(settings.volume);
    fToolbar->SetViewMode(fViewMode);
    fStatus->SetShuffle(settings.shuffle);
    fStatus->SetRepeat(settings.repeat);
    BScreen screen(this);
    if (!screen.Frame().Contains(frame.LeftTop()))
        MoveTo(40, 40);
    AddShortcut('F', B_COMMAND_KEY, new BMessage('fsrc'));
    ReloadSidebar();
    fSidebar->Select(fSource.c_str(), 0);
    ReloadContent();
}

void MainWindow::OpenArtistPage(int64_t artistId)
{
    std::string name, album;
    {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        const Artist* artist = library.ArtistById(artistId);
        if (!artist)
            return;
        name = artist->name;
        // the album with the most songs tells apart artists who share the name
        size_t most = 0;
        for (int64_t albumId : artist->albumIds)
            if (const Album* found = library.AlbumById(albumId))
                if (found->trackIds.size() > most) {
                    most = found->trackIds.size();
                    album = found->name;
                }
    }
    if (name.empty())
        return;
    // one lookup at a time: a second click while MusicBrainz is asked does nothing
    static std::atomic<bool> busy{false};
    if (busy.exchange(true))
        return;
    bool known = !ArtistLinks::Shared().Known(name).empty();
    if (!known)
        fStatus->SetTransient(BString("Looking up ") << name.c_str() << " on MusicBrainz…", 4);
    std::thread([name, album] {
        std::string mbid = ArtistLinks::Shared().Resolve(name, album);
        std::string url = ArtistLinks::PageUrl(name, mbid);
        BUrl(url.c_str(), false).OpenWithPreferredApplication(true);
        busy = false;
    }).detach();
}

void MainWindow::LibraryScanProgress(const BString& text, bool done, int processed, int total)
{
    // the scan runs in the background: its page never pushes the playing song aside
    ToolbarView::Activity scan;
    scan.id = "scan";
    scan.kind = ToolbarView::kActivityScan;
    scan.detail = text;
    if (done) {
        scan.done = true;
        bool unchanged = text == "Library up to date";
        scan.headline = unchanged ? text : BString("Library updated");
        if (unchanged)
            scan.detail = total > 0 ? BString() << total << (total == 1 ? " song" : " songs") : BString();
        fToolbar->SetActivity(scan, false);
        ExpireActivity("scan", unchanged ? 2500000 : 6000000);
        return;
    }
    scan.headline = "Updating Library";
    if (total > 0) {
        scan.fraction = std::min(1.0f, (float)processed / total);
        scan.leftLabel << (int)(scan.fraction * 100) << "%";
        scan.detail = BString() << "Checked " << processed << " of " << total << " songs";
    }
    fToolbar->SetActivity(scan, false);
}

void MainWindow::ExpireActivity(const char* id, bigtime_t after)
{
    auto found = fActivityRunners.find(id);
    if (found != fActivityRunners.end())
        delete found->second;
    BMessage expired(kMsgActivityExpired);
    expired.AddString("id", id);
    fActivityRunners[id] = new BMessageRunner(BMessenger(this), &expired, after, 1);
}

void MainWindow::BuildMenu()
{
    // Amp keeps its chrome minimal like iTunes; the menu offers keyboard access to everything.
    BMenuBar* bar = new BMenuBar("menu");
    BMenu* file = new BMenu("File");
    file->AddItem(new BMenuItem("New Playlist…", new BMessage(kMsgNewPlaylist), 'N'));
    file->AddItem(new BMenuItem("Rescan Library", new BMessage(kMsgRescan), 'R'));
    file->AddItem(new BMenuItem("Sync Music Assistant Library", new BMessage(kMsgMAResync)));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Settings…", new BMessage(kMsgShowSettings), ','));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("About Amp", new BMessage(kMsgAbout)));
    file->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
    bar->AddItem(file);
    BMenu* controls = new BMenu("Controls");
    controls->AddItem(new BMenuItem("Play/Pause", new BMessage(kMsgPlayPause), 'P'));
    controls->AddItem(new BMenuItem("Stop", new BMessage(kMsgStop), '.'));
    controls->AddItem(new BMenuItem("Next", new BMessage(kMsgNext), B_RIGHT_ARROW));
    controls->AddItem(new BMenuItem("Previous", new BMessage(kMsgPrevious), B_LEFT_ARROW));
    controls->AddSeparatorItem();
    controls->AddItem(new BMenuItem("Shuffle", new BMessage(kMsgToggleShuffle), 'S'));
    controls->AddItem(new BMenuItem("Repeat", new BMessage(kMsgToggleRepeat), 'T'));
    bar->AddItem(controls);
    BMenu* view = new BMenu("View");
    BMessage* list = new BMessage(kMsgViewMode);
    list->AddInt32("mode", 0);
    view->AddItem(new BMenuItem("as List", list, '1'));
    BMessage* grouped = new BMessage(kMsgViewMode);
    grouped->AddInt32("mode", 1);
    view->AddItem(new BMenuItem("as Album List", grouped, '2'));
    BMessage* grid = new BMessage(kMsgViewMode);
    grid->AddInt32("mode", 2);
    view->AddItem(new BMenuItem("as Grid", grid, '3'));
    bar->AddItem(view);
    // The menu bar is hidden behind the toolbar in the iTunes look; keep it for shortcuts only.
    bar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 0));
    bar->Hide();
    AddChild(bar);
    SetKeyMenuBar(bar);
}

bool MainWindow::QuitRequested()
{
    if (App()->MiniDisc().Busy()) {
        BAlert* alert = new BAlert("Quit Amp",
            "Amp is writing to the MiniDisc.\n\nIf you quit now, the song being written is lost and the recorder "
            "may have to be reconnected.",
            "Keep Writing", "Quit", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        if (alert->Go() != 1)
            return false;
    }
    delete fMDClearRunner;
    fMDClearRunner = nullptr;
    for (auto& runner : fActivityRunners)
        delete runner.second;
    fActivityRunners.clear();
    SaveGeometry();
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}

void MainWindow::SaveGeometry()
{
    BRect frame = Frame();
    App()->GetSettings().Modify([&](SettingsData& d) {
        d.windowX = (int)frame.left;
        d.windowY = (int)frame.top;
        d.windowW = (int)frame.Width();
        d.windowH = (int)frame.Height();
        d.viewMode = fViewMode;
        d.playlistViewMode = fPlaylistViewMode;
        d.selectedSource = fSource == "playlist" || fSource == "minidisc" ? "music" : fSource;
        // the split item is the scroll view (list plus scroll bar), not the list alone
        BView* sidebarItem = fSidebar->Parent() ? fSidebar->Parent() : fSidebar;
        d.sidebarWidth = (int)sidebarItem->Frame().Width();
        d.volume = App()->GetPlayer().Volume();
        d.shuffle = App()->GetPlayer().Shuffle();
        d.repeat = (int)App()->GetPlayer().Repeat();
    });
}

void MainWindow::ReloadSidebar()
{
    std::vector<SidebarPlaylist> lists;
    Library& library = App()->GetLibrary();
    bool playlistGone = fSource == "playlist";
    {
        Library::Locker locker(library);
        for (int64_t id : library.AllPlaylistIds()) {
            const Playlist* p = library.PlaylistById(id);
            if (!p)
                continue;
            lists.push_back({p->id, p->name.c_str(), p->isMA(), p->syncToMA});
            if (p->id == fPlaylistId)
                playlistGone = false;
        }
    }
    bool musicAssistant = App()->MAEnabled();
    fSidebar->SetMusicAssistant(musicAssistant);
    fStatus->SetMAVisible(musicAssistant);
    fSidebar->SetPlaylists(lists);
    // the source on show went away (Music Assistant switched off, a playlist deleted on the server)
    if (playlistGone || (fSource == "ma" && !musicAssistant)) {
        fSource = "music";
        fPlaylistId = 0;
        ClearDrillDown();
        fSidebar->Select("music", 0);
    }
}

std::vector<int64_t> MainWindow::FilterTracks(const std::vector<int64_t>& ids) const
{
    if (fSearch.empty())
        return ids;
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    std::vector<int64_t> result;
    for (int64_t id : ids) {
        const Track* t = library.TrackById(id);
        if (!t)
            continue;
        if (ContainsNoCase(t->title, fSearch) || ContainsNoCase(t->artist, fSearch) || ContainsNoCase(t->album, fSearch)
            || ContainsNoCase(t->albumArtist, fSearch))
            result.push_back(id);
    }
    return result;
}

std::vector<AlbumCell> MainWindow::CellsFromLibrary(bool maOnly) const
{
    std::vector<AlbumCell> cells;
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    for (int64_t id : library.AllAlbumIds()) {
        const Album* a = library.AlbumById(id);
        if (!a || (maOnly && !a->isMA()))
            continue;
        if (!fSearch.empty() && !ContainsNoCase(a->name, fSearch) && !ContainsNoCase(a->artist, fSearch))
            continue;
        AlbumCell cell;
        cell.albumId = a->id;
        cell.name = a->name;
        cell.artist = a->artist;
        cell.artKey = a->art;
        cell.year = a->year;
        cell.isMA = a->isMA();
        cell.trackIds = a->trackIds;
        if (!a->isMA() && !a->trackIds.empty())
            if (const Track* first = library.TrackById(a->trackIds.front()))
                cell.localHint = first->uri;
        cells.push_back(std::move(cell));
    }
    return cells;
}

std::vector<AlbumCell> MainWindow::CellsFromTracks(const std::vector<int64_t>& tracks) const
{
    // one cell per album, in order of first appearance; a cell holds only the songs that are
    // in the given list (a playlist usually has a few songs of an album, not all of it)
    std::vector<AlbumCell> cells;
    std::map<std::string, size_t> byKey;
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    for (int64_t id : tracks) {
        const Track* t = library.TrackById(id);
        if (!t)
            continue;
        std::string artist = t->groupingArtist().empty() ? "Unknown Artist" : t->groupingArtist();
        std::string name = t->album.empty() ? "Unknown Album" : t->album;
        std::string key = t->albumId != 0 ? "#" + std::to_string(t->albumId)
            : (t->isMA() ? "ma|" : "local|") + ToLower(artist) + "|" + ToLower(name);
        auto found = byKey.find(key);
        if (found == byKey.end()) {
            AlbumCell cell;
            const Album* album = library.AlbumById(t->albumId);
            if (album) {
                cell.name = album->name;
                cell.artist = album->artist;
                cell.artKey = album->art;
                cell.year = album->year;
            } else {
                cell.name = name;
                cell.artist = artist;
                cell.artKey = t->art.empty() ? MakeAlbumArtKey(artist, name) : t->art;
                cell.year = t->year;
            }
            cell.isMA = t->isMA();
            if (!t->isMA())
                cell.localHint = t->uri;
            found = byKey.emplace(key, cells.size()).first;
            cells.push_back(std::move(cell));
        }
        cells[found->second].trackIds.push_back(id);
    }
    return cells;
}

void MainWindow::ClearDrillDown()
{
    // called whenever a different list is about to be shown: it starts without a selection
    fShownAlbum = 0;
    fShownAlbumArtist.clear();
    fShownAlbumName.clear();
    fDrillTracks.clear();
    fTrackList->ClearSelection();
}

void MainWindow::ReloadContent()
{
    Library& library = App()->GetLibrary();
    fLastReload = system_time();
    fLibraryDirty = false;
    fToolbar->SetViewMode(CurrentViewMode());
    if (fSource == "minidisc") {
        fCards->SetVisibleItem((int32)3);
        MiniDiscChanged();
        return;
    }
    if (fSource == "artists") {
        std::vector<int64_t> artists;
        {
            Library::Locker locker(library);
            for (int64_t id : library.AllArtistIds()) {
                const Artist* a = library.ArtistById(id);
                if (a && (fSearch.empty() || ContainsNoCase(a->name, fSearch)))
                    artists.push_back(id);
            }
        }
        fArtists->SetArtists(artists);
        fArtists->TrackList()->SetNowPlaying(fNowPlayingTrack, fPlayerState);
        fCards->SetVisibleItem((int32)2);
        UpdateSummary(fArtists->TrackList()->Tracks());
        return;
    }

    // 1. the songs of the current source
    std::vector<int64_t> tracks;
    int64_t editablePlaylist = 0;
    bool drillDown = fShownAlbum != 0 || !fDrillTracks.empty();
    {
        Library::Locker locker(library);
        if (fShownAlbum != 0) {
            if (const Album* album = library.AlbumById(ResolveShownAlbum(library)))
                tracks = album->trackIds;
        } else if (!fDrillTracks.empty()) {
            tracks = fDrillTracks;
        } else if (fSource == "playlist") {
            if (const Playlist* p = library.PlaylistById(fPlaylistId)) {
                tracks = p->trackIds;
                editablePlaylist = p->isMA() ? 0 : p->id;
                if (p->isMA())
                    fTrackList->SetEmptyText("This Music Assistant playlist is empty");
                else
                    fTrackList->SetEmptyText("Drag songs here or use \"Add to Playlist\" to fill this playlist");
            }
        } else if (fSource == "ma") {
            for (int64_t id : library.AllTrackIds())
                if (const Track* t = library.TrackById(id))
                    if (t->isMA())
                        tracks.push_back(id);
            fTrackList->SetEmptyText("No Music Assistant tracks yet. Connect a server in Settings.");
        } else {
            tracks = library.AllTrackIds();
            fTrackList->SetEmptyText("No songs yet. Add library folders in Settings (File > Settings…).");
        }
    }
    bool loading = false;
    BString loadingText;
    if (fSource == "playlist" && !drillDown && tracks.empty() && fLoadingPlaylists.count(fPlaylistId)) {
        loading = true;
        Library::Locker locker(library);
        if (const Playlist* p = library.PlaylistById(fPlaylistId))
            loadingText << "Loading \u201C" << p->name.c_str() << "\u201D from Music Assistant\u2026";
    }

    // 2. search filter; an editable playlist keeps the playlist position of every shown row
    std::vector<int32> positions;
    if (!fSearch.empty() && !drillDown) {
        std::vector<int64_t> filtered;
        Library::Locker locker(library);
        for (size_t i = 0; i < tracks.size(); i++) {
            const Track* t = library.TrackById(tracks[i]);
            if (!t)
                continue;
            if (ContainsNoCase(t->title, fSearch) || ContainsNoCase(t->artist, fSearch) || ContainsNoCase(t->album, fSearch)
                || ContainsNoCase(t->albumArtist, fSearch)) {
                filtered.push_back(tracks[i]);
                positions.push_back((int32)i);
            }
        }
        tracks.swap(filtered);
    }
    if (editablePlaylist == 0 || drillDown)
        positions.clear();

    // 3. the view: covers, album list or plain list
    int viewMode = CurrentViewMode();
    bool grid = !drillDown && !loading && (fSource == "albums" || viewMode == 2);
    if (grid) {
        if (fSource == "playlist")
            fGrid->SetCells(CellsFromTracks(tracks));
        else
            fGrid->SetCells(CellsFromLibrary(fSource == "ma"));
        fCards->SetVisibleItem((int32)1);
        if (fSource == "albums") {
            Library::Locker locker(library);
            tracks = library.AllTrackIds();
        }
        UpdateSummary(tracks);
        return;
    }
    int mode = (drillDown || viewMode != 0) ? TrackListView::kGrouped : TrackListView::kPlain;
    fTrackList->SetLoading(loading, loadingText.String());
    fTrackList->SetTracks(tracks, mode, drillDown ? 0 : editablePlaylist, positions);
    fTrackList->SetNowPlaying(fNowPlayingTrack, fPlayerState);
    fCards->SetVisibleItem((int32)0);
    UpdateSummary(tracks);
}

void MainWindow::UpdateSummary(const std::vector<int64_t>& tracks)
{
    UpdateMiniDiscButton();
    Library& library = App()->GetLibrary();
    size_t count = 0;
    int64_t duration = 0;
    int64_t bytes = 0;
    {
        Library::Locker locker(library);
        for (int64_t id : tracks) {
            const Track* t = library.TrackById(id);
            if (!t)
                continue;
            count++;
            duration += t->durationMs;
            bytes += t->sizeBytes;
        }
    }
    BString text;
    text << (int)count << (count == 1 ? " song" : " songs");
    double hours = duration / 3600000.0;
    if (hours >= 1)
        text << ", " << BString().SetToFormat("%.1f hours", hours);
    else if (duration > 0)
        text << ", " << BString().SetToFormat("%.1f minutes", duration / 60000.0);
    if (bytes > 0)
        text << ", " << (bytes > 1024 * 1024 * 1024 ? BString().SetToFormat("%.2f GB", bytes / 1073741824.0)
            : BString().SetToFormat("%.1f MB", bytes / 1048576.0));
    fStatus->SetSummary(text);
}

void MainWindow::SelectSource(const std::string& source, int64_t playlistId, bool play)
{
    fSource = source;
    fPlaylistId = playlistId;
    ClearDrillDown();
    fSidebar->Select(source.c_str(), playlistId);
    if (source == "playlist") {
        bool load = false;
        {
            Library& library = App()->GetLibrary();
            Library::Locker locker(library);
            if (const Playlist* p = library.PlaylistById(playlistId))
                load = p->isMA(); // cached tracks show at once; a refresh runs in the background
        }
        if (load && App()->LoadMAPlaylist(playlistId))
            fLoadingPlaylists.insert(playlistId);
    }
    if (source == "minidisc")
        App()->MiniDisc().Refresh(true);
    ReloadContent();
    if (play) {
        std::vector<int64_t> tracks = fTrackList->Tracks();
        if (!tracks.empty())
            App()->GetPlayer().PlayTracks(tracks, 0);
    }
}

void MainWindow::ShowAlbum(int64_t albumId)
{
    ClearDrillDown();
    fShownAlbum = albumId;
    {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        const Album* album = library.AlbumById(albumId);
        fShownAlbumArtist = album ? album->artist : std::string();
        fShownAlbumName = album ? album->name : std::string();
        fShownAlbumMA = album && album->isMA();
    }
    if (fSource == "artists" || fSource == "playlist")
        fSource = "music";
    fSidebar->Select(fSource.c_str(), 0);
    ReloadContent();
}

int64_t MainWindow::ResolveShownAlbum(Library& library)
{
    // The index numbers albums afresh whenever it is rebuilt, which a running scan does
    // every few seconds: the id opened from the grid soon names another album (#9). The
    // album is found again by artist and name; while it is not there (its songs are still
    // being read) the list stays empty rather than showing a stranger.
    if (fShownAlbum == 0 || fShownAlbumName.empty())
        return fShownAlbum;
    const Album* album = library.AlbumById(fShownAlbum);
    if (album && album->artist == fShownAlbumArtist && album->name == fShownAlbumName
        && album->isMA() == fShownAlbumMA)
        return fShownAlbum;
    int64_t found = library.AlbumIdFor(fShownAlbumArtist, fShownAlbumName, fShownAlbumMA);
    if (found != 0)
        fShownAlbum = found;
    return found;
}

void MainWindow::ShowArtist(int64_t artistId)
{
    fSource = "artists";
    ClearDrillDown();
    fSidebar->Select("artists", 0);
    ReloadContent();
    fArtists->SelectArtist(artistId);
    UpdateSummary(fArtists->TrackList()->Tracks());
}

void MainWindow::SetViewMode(int mode)
{
    // playlists remember their own view, separate from the library's
    if (fSource == "playlist")
        fPlaylistViewMode = mode;
    else
        fViewMode = mode;
    ClearDrillDown();
    ReloadContent();
}

void MainWindow::UpdateNowPlaying()
{
    Player& player = App()->GetPlayer();
    fPlayerState = player.State();
    fNowPlayingTrack = player.CurrentTrack();
    fToolbar->SetPlayerState(fPlayerState);
    BString title, artist, album;
    bool isMA = false;
    ArtRequest art;
    if (fNowPlayingTrack) {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        if (const Track* t = library.TrackById(fNowPlayingTrack)) {
            title = t->title.c_str();
            artist = t->artist.c_str();
            album = t->album.c_str();
            isMA = t->isMA();
            // the album's cover when the track belongs to a library album, else the track's own
            const Album* owner = library.AlbumById(t->albumId);
            ArtKey key = owner ? owner->art : t->art;
            std::string albumArtist = owner ? owner->artist : t->groupingArtist();
            if (key.empty())
                key = MakeAlbumArtKey(albumArtist, t->album);
            art = App()->Art().RequestFor(key, albumArtist, t->album, isMA ? "" : t->uri, false);
        }
    }
    fToolbar->SetTrackInfo(title, artist, album, isMA);
    fToolbar->SetArtwork(art);
    fShownQuality = player.StreamQuality();
    fToolbar->SetQuality(QualityFor(fNowPlayingTrack, fShownQuality));
    fTrackList->SetNowPlayingQuality(fShownQuality);
    fArtists->TrackList()->SetNowPlayingQuality(fShownQuality);
    fTrackList->SetNowPlaying(fNowPlayingTrack, fPlayerState);
    fArtists->TrackList()->SetNowPlaying(fNowPlayingTrack, fPlayerState);
    if (fPlayerState == kStopped)
        SetTitle("Amp");
    else {
        BString windowTitle(title);
        if (!artist.IsEmpty())
            windowTitle << " — " << artist;
        windowTitle << " — Amp";
        SetTitle(windowTitle.String());
    }
}

BString MainWindow::QualityFor(int64_t trackId, int streamQuality)
{
    // what the server reports for the running stream wins over the library's figures
    if (streamQuality != 0)
        return QualityLabel(streamQuality < 0, streamQuality).c_str();
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    const Track* track = library.TrackById(trackId);
    return track ? QualityLabel(track->lossless, track->bitrate).c_str() : "";
}

void MainWindow::PlaylistEdited(int64_t playlistId)
{
    Library& library = App()->GetLibrary();
    bool sync = false;
    {
        Library::Locker locker(library);
        if (const Playlist* p = library.PlaylistById(playlistId))
            sync = p->syncToMA && !p->isMA();
    }
    if (sync)
        App()->SyncPlaylistToMA(playlistId);
}

void MainWindow::MessageReceived(BMessage* message)
{
    Player& player = App()->GetPlayer();
    Library& library = App()->GetLibrary();
    switch (message->what) {
        case kMsgPlayPause:
            player.PlayPause();
            if (player.State() == kStopped && !player.QueueTracks().empty())
                break;
            if (player.State() == kStopped) {
                // nothing queued: play the current view from the selection
                TrackListView* list = fSource == "artists" ? fArtists->TrackList() : fTrackList;
                std::vector<int64_t> tracks = list->Tracks();
                if (!tracks.empty()) {
                    std::vector<int64_t> selected = list->SelectedTracks();
                    int index = 0;
                    if (!selected.empty())
                        for (size_t i = 0; i < tracks.size(); i++)
                            if (tracks[i] == selected.front())
                                index = (int)i;
                    player.PlayTracks(tracks, index);
                }
            }
            break;
        case kMsgStop:
            player.Stop();
            break;
        case kMsgNext:
            player.Next();
            break;
        case kMsgPrevious:
            player.Previous();
            break;
        case kMsgSeek: {
            int64 position = 0;
            if (message->FindInt64("position", &position) == B_OK)
                player.Seek(position);
            break;
        }
        case kMsgVolumeChanged: {
            float volume = 0.8f;
            if (message->FindFloat("volume", &volume) == B_OK)
                player.SetVolume(volume);
            break;
        }
        case kMsgPlayerVolume: {
            float volume = 0.8f;
            if (message->FindFloat("volume", &volume) == B_OK)
                fToolbar->SetVolume(volume);
            break;
        }
        case kMsgToggleShuffle:
            player.SetShuffle(!player.Shuffle());
            fStatus->SetShuffle(player.Shuffle());
            break;
        case kMsgToggleRepeat: {
            int next = ((int)player.Repeat() + 1) % 3;
            player.SetRepeat((RepeatMode)next);
            fStatus->SetRepeat(next);
            break;
        }
        case kMsgViewMode: {
            int32 mode = 0;
            if (message->FindInt32("mode", &mode) == B_OK)
                SetViewMode(mode);
            break;
        }
        case 'fsrc':
            fToolbar->SearchField()->MakeFocus(true);
            break;
        case kMsgSearch: {
            std::string text = fToolbar->SearchField()->Text();
            if (text != fSearch) {
                fSearch = text;
                ClearDrillDown();
                ReloadContent();
            }
            break;
        }
        case kMsgSourceSelected: {
            const char* source = nullptr;
            int64 playlistId = 0;
            bool play = false;
            message->FindString("source", &source);
            message->FindInt64("playlist", &playlistId);
            message->FindBool("play", &play);
            if (source)
                SelectSource(source, playlistId, play);
            break;
        }
        case kMsgPlayTracks: {
            std::vector<int64_t> tracks;
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                tracks.push_back(id);
            if (tracks.empty())
                break;
            bool next = false, append = false;
            message->FindBool("next", &next);
            message->FindBool("append", &append);
            if (next)
                player.EnqueueNext(tracks);
            else if (append)
                player.EnqueueLast(tracks);
            else {
                int32 index = 0;
                message->FindInt32("index", &index);
                player.PlayTracks(tracks, index);
            }
            break;
        }
        case kMsgShowAlbum: {
            int64 albumId = 0;
            if (message->FindInt64("album", &albumId) == B_OK && albumId)
                ShowAlbum(albumId);
            break;
        }
        case kMsgShowTracks: {
            // the songs of one album inside the current playlist, opened from the cover grid
            ClearDrillDown();
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                fDrillTracks.push_back(id);
            ReloadContent();
            break;
        }
        case kMsgShowArtist: {
            int64 artistId = 0;
            if (message->FindInt64("artist", &artistId) == B_OK && artistId)
                ShowArtist(artistId);
            break;
        }
        case kMsgOpenArtistPage:
            OpenArtistPage(message->GetInt64("artist", 0));
            break;
        case kMsgNewPlaylist: {
            BMessage* templ = new BMessage(kMsgNewPlaylistNamed);
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                templ->AddInt64("tracks", id);
            NameDialog* dialog = new NameDialog(this, "New Playlist", "Name:", "Untitled Playlist", templ, BMessenger(this));
            dialog->Show();
            break;
        }
        case kMsgNewPlaylistNamed: {
            const char* name = nullptr;
            if (message->FindString("name", &name) != B_OK)
                break;
            int64_t playlistId = library.CreatePlaylist(name);
            std::vector<int64_t> tracks;
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                tracks.push_back(id);
            if (!tracks.empty())
                library.AddToPlaylist(playlistId, tracks);
            ReloadSidebar();
            SelectSource("playlist", playlistId);
            break;
        }
        case kMsgRenamePlaylist: {
            int64 playlistId = 0;
            message->FindInt64("playlist", &playlistId);
            const char* name = nullptr;
            if (message->FindString("name", &name) == B_OK) {
                library.RenamePlaylist(playlistId, name);
                ReloadSidebar();
                PlaylistEdited(playlistId);
            } else {
                std::string current;
                {
                    Library::Locker locker(library);
                    if (const Playlist* p = library.PlaylistById(playlistId))
                        current = p->name;
                }
                BMessage* templ = new BMessage(kMsgRenamePlaylist);
                templ->AddInt64("playlist", playlistId);
                (new NameDialog(this, "Rename Playlist", "Name:", current.c_str(), templ, BMessenger(this)))->Show();
            }
            break;
        }
        case kMsgDeletePlaylist: {
            int64 playlistId = 0;
            message->FindInt64("playlist", &playlistId);
            std::string name, maItemId;
            bool isMA = false;
            {
                Library::Locker locker(library);
                if (const Playlist* p = library.PlaylistById(playlistId)) {
                    name = p->name;
                    isMA = p->isMA();
                    maItemId = p->maItemId;
                }
            }
            BString text;
            text << "Delete the playlist \"" << name.c_str() << "\"?";
            if (isMA)
                text << "\n\nThis removes it from the Music Assistant server as well.";
            BAlert* alert = new BAlert("Delete Playlist", text.String(), "Cancel", "Delete", nullptr,
                B_WIDTH_AS_USUAL, B_WARNING_ALERT);
            alert->SetShortcut(0, B_ESCAPE);
            if (alert->Go() == 1) {
                library.DeletePlaylist(playlistId);
                if (isMA && !maItemId.empty()) {
                    std::thread([maItemId] {
                        std::string error;
                        App()->MA().DeletePlaylist(maItemId, error);
                    }).detach();
                }
                bool shown = fSource == "playlist" && fPlaylistId == playlistId;
                ReloadSidebar();
                if (shown)
                    SelectSource("music", 0);
            }
            break;
        }
        case kMsgAddToPlaylist: {
            int64 playlistId = 0;
            message->FindInt64("playlist", &playlistId);
            std::vector<int64_t> tracks;
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                tracks.push_back(id);
            int32 position = -1;
            message->FindInt32("position", &position);
            if (playlistId && !tracks.empty()) {
                library.AddToPlaylist(playlistId, tracks, position);
                PlaylistEdited(playlistId);
                BString text;
                text << "Added " << (int)tracks.size() << (tracks.size() == 1 ? " song" : " songs") << " to the playlist";
                fStatus->SetTransient(text);
            }
            break;
        }
        case kMsgRemoveFromPlaylist: {
            int64 playlistId = 0;
            message->FindInt64("playlist", &playlistId);
            std::vector<int> positions;
            int32 position;
            for (int32 i = 0; message->FindInt32("positions", i, &position) == B_OK; i++)
                positions.push_back(position);
            if (playlistId && !positions.empty()) {
                library.RemoveFromPlaylist(playlistId, positions);
                PlaylistEdited(playlistId);
            }
            break;
        }
        case kMsgMovePlaylistTracks: {
            int64 playlistId = 0;
            message->FindInt64("playlist", &playlistId);
            std::vector<int> positions;
            int32 position;
            for (int32 i = 0; message->FindInt32("positions", i, &position) == B_OK; i++)
                positions.push_back(position);
            int32 target = 0;
            message->FindInt32("target", &target);
            if (playlistId && !positions.empty()) {
                library.MoveInPlaylist(playlistId, positions, target);
                PlaylistEdited(playlistId);
            }
            break;
        }
        case kMsgSyncPlaylistToMA: {
            int64 playlistId = 0;
            bool enable = false;
            message->FindInt64("playlist", &playlistId);
            message->FindBool("enable", &enable);
            std::string maItemId, maUri;
            {
                Library::Locker locker(library);
                if (const Playlist* p = library.PlaylistById(playlistId)) {
                    maItemId = p->maItemId;
                    maUri = p->maUri;
                }
            }
            if (enable && !App()->MAEnabled()) {
                fStatus->SetTransient("Enable Music Assistant in Settings to sync playlists");
                break;
            }
            library.LinkPlaylistToMA(playlistId, maItemId, maUri, enable);
            ReloadSidebar();
            if (enable)
                App()->SyncPlaylistToMA(playlistId);
            break;
        }
        case kMsgShowSettings:
            (new SettingsWindow(this))->Show();
            break;
        case kMsgRescan:
        case kMsgMAResync:
            be_app->PostMessage(message);
            break;
        case kMsgAbout: {
            BAlert* alert = new BAlert("About Amp",
                "Amp 0.3.2\n\nA native music player for Haiku with local libraries, Music Assistant streaming and MiniDisc (NetMD) writing.\n"
                "Icons from Font Awesome Free 6.7.2. Artwork from embedded tags, folder art, MusicBrainz/Cover Art Archive, TheAudioDB and Discogs.",
                "OK");
            alert->Go(nullptr);
            break;
        }
        // ---- notifications from the player and background workers
        case kMsgPlayerStateChanged:
            UpdateNowPlaying();
            fToolbar->SetProgress(player.PositionMs(), player.DurationMs());
            break;
        case kMsgPlayerProgress: {
            int64 position = 0, duration = 0, track = 0;
            message->FindInt64("position", &position);
            message->FindInt64("duration", &duration);
            message->FindInt64("track", &track);
            if (track != fNowPlayingTrack || player.State() != fPlayerState)
                UpdateNowPlaying();
            int32 quality = 0;
            message->FindInt32("quality", &quality);
            if (quality != fShownQuality && fNowPlayingTrack) {
                fShownQuality = quality;
                fToolbar->SetQuality(QualityFor(fNowPlayingTrack, quality));
                fTrackList->SetNowPlayingQuality(quality);
                fArtists->TrackList()->SetNowPlayingQuality(quality);
            }
            fToolbar->SetProgress(position, duration);
            break;
        }
        case kMsgPlayerError: {
            const char* error = nullptr;
            if (message->FindString("error", &error) == B_OK)
                fStatus->SetTransient(error);
            UpdateNowPlaying();
            break;
        }
        case kMsgMAStatus: {
            bool connected = false;
            const char* text = nullptr;
            message->FindBool("connected", &connected);
            message->FindString("message", &text);
            fStatus->SetMAStatus(connected, text ? text : "");
            if (message->GetBool("syncing", false)) {
                ToolbarView::Activity sync;
                sync.id = "ma-sync";
                sync.kind = ToolbarView::kActivitySync;
                sync.headline = "Syncing Music Assistant";
                BString detail(text ? text : "");
                detail.RemoveFirst("Music Assistant: ");
                sync.detail = detail;
                fToolbar->SetActivity(sync, false);
            } else if (!connected)
                fToolbar->RemoveActivity("ma-sync"); // switched off or the connection went
            break;
        }
        case kMsgMASyncDone: {
            ReloadSidebar();
            ReloadContent();
            if (fToolbar->FindActivity("ma-sync")) {
                BString error = message->GetString("error", "");
                ToolbarView::Activity sync;
                sync.id = "ma-sync";
                sync.kind = ToolbarView::kActivitySync;
                sync.done = true;
                sync.failed = !error.IsEmpty();
                sync.headline = error.IsEmpty() ? "Music Assistant synced" : "Music Assistant sync failed";
                sync.detail = error;
                fToolbar->SetActivity(sync, false);
                ExpireActivity("ma-sync", 6000000);
            }
            break;
        }
        case kMsgActivityExpired: {
            BString id = message->GetString("id", "");
            auto runner = fActivityRunners.find(id.String());
            if (runner != fActivityRunners.end()) {
                delete runner->second;
                fActivityRunners.erase(runner);
            }
            // a job that started again in the meantime stays
            const ToolbarView::Activity* activity = fToolbar->FindActivity(id.String());
            if (activity && activity->done)
                fToolbar->RemoveActivity(id.String());
            break;
        }
        case kMsgMACleared:
            // album ids were dealt anew and the streamed songs are gone: back to the plain source
            ClearDrillDown();
            fLoadingPlaylists.clear();
            ReloadSidebar();
            ReloadContent();
            UpdateNowPlaying();
            break;
        case kMsgPlaylistLoading: {
            int64 playlistId = 0;
            bool loading = false;
            message->FindInt64("playlist", &playlistId);
            message->FindBool("loading", &loading);
            if (loading)
                fLoadingPlaylists.insert(playlistId);
            else
                fLoadingPlaylists.erase(playlistId);
            if (fSource == "playlist" && fPlaylistId == playlistId)
                ReloadContent();
            break;
        }
        case kMsgScanProgress: {
            const char* text = nullptr;
            bool done = false;
            message->FindString("text", &text);
            message->FindBool("done", &done);
            fStatus->SetTransient(text ? text : "", done ? 6 : 0);
            if (message->GetBool("scan", false))
                LibraryScanProgress(text ? text : "", done, message->GetInt32("processed", 0),
                    message->GetInt32("total", 0));
            break;
        }
        case kMsgLibraryChanged:
            // coalesce bursts of changes from the scanner
            if (system_time() - fLastReload > 700000) {
                ReloadSidebar();
                ReloadContent();
            } else if (!fLibraryDirty) {
                fLibraryDirty = true;
                BMessage reload(kMsgReloadNow);
                BMessenger(this).SendMessage(&reload); // handled below after a short delay
            }
            break;
        case kMsgReloadNow:
            if (fLibraryDirty) {
                snooze(300000);
                ReloadSidebar();
                ReloadContent();
            }
            break;
        case kMsgArtReady:
            fToolbar->ArtworkChanged();
            fTrackList->Refresh();
            fGrid->Refresh();
            fArtists->Refresh();
            break;
        case kMsgSelectionChanged:
            if (fSource == "artists")
                UpdateSummary(fArtists->TrackList()->Tracks());
            break;
        case B_REFS_RECEIVED:
        case B_SIMPLE_DATA:
            be_app->PostMessage(message);
            break;
        // ---- MiniDisc
        case kMsgMDState:
            MiniDiscChanged();
            break;
        case kMsgMDProgress:
            MiniDiscProgress(message);
            break;
        case kMsgMDFinished:
            MiniDiscFinished(message);
            break;
        case kMsgWriteToMiniDisc:
            StartMiniDiscWrite(message);
            break;
        case kMsgMDCancel: {
            MiniDiscManager& manager = App()->MiniDisc();
            if (!manager.Busy())
                break;
            manager.Cancel();
            if (const ToolbarView::Activity* shown = fToolbar->FindActivity("minidisc")) {
                ToolbarView::Activity status = *shown;
                status.cancelCommand = 0;
                status.detail = "Stopping after the current song…";
                fToolbar->SetActivity(status);
            }
            break;
        }
        case kMsgMDErase:
            ConfirmMiniDiscErase();
            break;
        case kMsgMDRefresh:
            App()->MiniDisc().Refresh(true);
            break;
        case kMsgMDClearStatus:
            delete fMDClearRunner;
            fMDClearRunner = nullptr;
            if (!App()->MiniDisc().Busy())
                fToolbar->RemoveActivity("minidisc");
            break;
        default:
            BWindow::MessageReceived(message);
    }
}

} // namespace amp
