// Amp's main window: toolbar, sidebar, content views and status bar in the iTunes 8 layout.
#pragma once
#include "AlbumGridView.h"
#include "ArtistsView.h"
#include "MiniDiscView.h"
#include "SidebarView.h"
#include "StatusBarView.h"
#include "ToolbarView.h"
#include "TrackListView.h"
#include "player/Messages.h"
#include <CardLayout.h>
#include <SplitView.h>
#include <String.h>
#include <Window.h>
#include <map>
#include <set>
#include <string>

namespace amp {

class MainWindow : public BWindow {
public:
    explicit MainWindow(BRect frame);
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;

    void SelectSource(const std::string& source, int64_t playlistId, bool play = false);
    void ShowAlbum(int64_t albumId);
    // The playing song, selected in its album (a click on the display's artwork).
    void ShowNowPlaying();
    void ShowArtist(int64_t artistId);
    // Opens the artist's MusicBrainz page (a search when the artist is not known there) in
    // the preferred web browser.
    void OpenArtistPage(int64_t artistId);

private:
    void BuildMenu();
    void ReloadContent();
    void ReloadSidebar();
    void UpdateSummary(const std::vector<int64_t>& tracks);
    int CurrentViewMode() const { return fSource == "playlist" ? fPlaylistViewMode : fViewMode; }
    // the view buttons pick how songs are listed: the albums, artists and MiniDisc views have
    // a layout of their own
    bool ViewModeApplies() const { return fSource != "albums" && fSource != "artists" && fSource != "minidisc"; }
    std::vector<AlbumCell> CellsFromLibrary(bool maOnly) const;
    std::vector<AlbumCell> CellsFromTracks(const std::vector<int64_t>& tracks) const;
    void ClearDrillDown();
    void UpdateNowPlaying();
    void SetViewMode(int mode);
    void PlaylistEdited(int64_t playlistId);
    std::vector<int64_t> FilterTracks(const std::vector<int64_t>& ids) const;
    void SaveGeometry();
    // MiniDisc: keeps the sidebar, the disc view and the Write button in step with the recorder
    void MiniDiscChanged();
    void UpdateMiniDiscButton();
    void StartMiniDiscWrite(BMessage* request);
    void MiniDiscProgress(BMessage* message);
    void MiniDiscFinished(BMessage* message);
    void ConfirmMiniDiscErase();
    void ClearMiniDiscStatus(bigtime_t after);
    // the library scan as a page of the display
    void LibraryScanProgress(const BString& text, bool done, int processed, int total);
    // takes a finished activity off the display after a while
    void ExpireActivity(const char* id, bigtime_t after);

    ToolbarView* fToolbar;
    SidebarView* fSidebar;
    BSplitView* fSplit;
    BView* fContent;
    BCardLayout* fCards;
    TrackListView* fTrackList;
    AlbumGridView* fGrid;
    ArtistsView* fArtists;
    MiniDiscView* fMiniDiscView;
    StatusBarView* fStatus;
    std::string fSource = "music";
    int64_t fPlaylistId = 0;
    int64_t fShownAlbum = 0;
    // what names the shown album: its id changes with every index rebuild while a scan runs
    std::string fShownAlbumArtist, fShownAlbumName;
    bool fShownAlbumMA = false;
    int64_t ResolveShownAlbum(class Library& library);
    int fViewMode = 0;
    int fPlaylistViewMode = 0;
    std::vector<int64_t> fDrillTracks;     // songs of a playlist-only album opened from the grid
    std::string fSearch;
    int64_t fNowPlayingTrack = 0;
    PlayerState fPlayerState = kStopped;
    bool fLibraryDirty = false;
    bigtime_t fLastReload = 0;
    BString fTransientStatus;
    std::set<int64_t> fLoadingPlaylists;   // MA playlists whose tracks are being fetched
    int fShownQuality = 0;                 // last stream quality pushed to the display
    BString QualityFor(int64_t trackId, int streamQuality);
    // the running MiniDisc write, as the display shows it
    std::string fMDJobName;
    std::vector<BString> fMDTitles;       // library titles of the songs being written
    float fMDFraction = -1;
    class BMessageRunner* fMDClearRunner = nullptr;
    std::map<std::string, class BMessageRunner*> fActivityRunners;
};

} // namespace amp
