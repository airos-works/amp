// Amp's main window: toolbar, sidebar, content views and status bar in the iTunes 8 layout.
#pragma once
#include "AlbumGridView.h"
#include "ArtistsView.h"
#include "MiniDiscView.h"
#include "OutputPicker.h"
#include "SidebarView.h"
#include "StatusBarView.h"
#include "ToolbarView.h"
#include "TrackListView.h"
#include "core/Model.h"
#include "player/Messages.h"
#include <Button.h>
#include <GroupView.h>
#include <CardLayout.h>
#include <SplitView.h>
#include <Slider.h>
#include <String.h>
#include <Window.h>
#include <map>
#include <string>

namespace BMobileKit {
class MetalBar;
class MetalButton;
class MetalSlider;
class NowPlayingDisplay;
class SearchField;
class SegmentedControl;
class WoodFrame;
}

namespace amp {

class NowPlayingCover;
class MobileOutputButton;

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
    // the touch library lists songs plainly: its albums and artists have tabs of their own
    int CurrentViewMode() const { return fMobile ? 0 : fSource == "playlist" ? fPlaylistViewMode : fViewMode; }
    // the view buttons pick how songs are listed: the albums, artists and MiniDisc views have
    // a layout of their own
    bool ViewModeApplies() const { return fSource != "albums" && fSource != "artists" && fSource != "minidisc"; }
    std::vector<AlbumCell> CellsFromLibrary() const;
    std::vector<AlbumCell> CellsFromTracks(const std::vector<int64_t>& tracks) const;
    void ClearDrillDown();
    void ClearSearch();
    void UpdateNowPlaying();
    void SetViewMode(int mode);
    std::vector<int64_t> FilterTracks(const std::vector<int64_t>& ids) const;
    void SaveGeometry();
    // MiniDisc: keeps the sidebar, the disc view and the Write button in step with the recorder
    void MiniDiscChanged();
    void UpdateMiniDiscButton();
    void StartMiniDiscWrite(BMessage* request);
    void MiniDiscProgress(BMessage* message);
    void MiniDiscFinished(BMessage* message);
    // the display, the sidebar and the disc view for a write that has just started
    void BeginMiniDiscDisplay(const std::string& jobName, const std::vector<BString>& titles,
        const std::vector<int64_t>& durations, bool erase);
    // after a failed write: offers the songs it did not get to, and writes them once the recorder
    // is back (MiniDiscChanged calls it while one is waiting)
    void OfferMiniDiscRetry(const BString& text, bool askAgain);
    void RetryMiniDiscWrite();
    void ConfirmMiniDiscErase();
    void ClearMiniDiscStatus(bigtime_t after);
    // Print Label…: an album's, a playlist's or the MiniDisc's label (MainWindowLabel.cpp)
    void OpenLabelWindow(BMessage* request);
    // Sonos: the output button's list, the rooms found and where the sound goes
    void ShowOutputs(BRect where);
    void ChooseOutput(const BString& uuid);
    void OutputChanged(BMessage* message);
    OutputChoices Choices() const;
    void UpdateOutputPicker();
    std::vector<SonosDevice> fSonosDevices;
    bool fSonosSearching = false;
    BString fSonosError;
    BMessenger fOutputPicker;
    bigtime_t fOutputPickerClosedAt = 0;
    OutputState fOutputState = kOutputLocal;
    BString fOutputUuid;
    MobileOutputButton* fMobileOutput;
    // the library scan as a page of the display
    void LibraryScanProgress(const BString& text, bool done, int processed, int total);
    // takes a finished activity off the display after a while
    void ExpireActivity(const char* id, bigtime_t after);

    // The touch presentation, after the iPad's Music app: an aluminium bar with the
    // transport, the display and the volume above the library, which lies between wooden
    // rails, and a bar with Back or Settings, the library's tabs and the search field below.
    // The display's cover opens the playing song's cover on a black stage.
    BMobileKit::MetalBar* fMobileTop;
    BMobileKit::MetalBar* fMobileBottom;
    BMobileKit::SegmentedControl* fMobileTransport;
    BMobileKit::NowPlayingDisplay* fMobileDisplay;
    BMobileKit::MetalSlider* fMobileVolume;
    BMobileKit::SegmentedControl* fMobileTabs;
    BMobileKit::MetalButton* fMobileBack;
    BMobileKit::MetalButton* fMobileSettings;
    BMobileKit::SearchField* fMobileSearch;
    BMobileKit::WoodFrame* fFrame;
    NowPlayingCover* fCover;
    ArtRequest fNowPlayingArt;
    // the tab the touch library shows; drilling into an album, an artist or a playlist
    // keeps it, and Back returns to it
    std::string fMobileHome = "music";
    bool fMobileMiniDisc = false;      // the MiniDisc tab is there (a recorder is connected)
    bool fMobile = false;
    BRect fDesktopFrame;
    void SetMobile(bool mobile);
    void ShowPlayerPage(bool show);
    bool PlayerPageShown() const;
    void UpdateMobileChrome();
    void UpdateMobileTabs();
    void UpdateMobileArtwork();
    void ShowArtistSongs(int64_t artistId);
    std::vector<AlbumCell> CellsFromArtists() const;
    std::vector<AlbumCell> CellsFromPlaylists() const;

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
    BString QualityFor(int64_t trackId);
    // the running MiniDisc write, as the display shows it
    std::string fMDJobName;
    std::vector<BString> fMDTitles;       // library titles of the songs being written
    std::vector<int64_t> fMDDurations;
    // the songs a failed write did not get to
    std::vector<BString> fMDRetryTitles;
    std::vector<int64_t> fMDRetryDurations;
    std::string fMDRetryName;
    bool fMDRetryPending = false;         // the user said yes: write them when the recorder is back
    float fMDFraction = -1;
    class BMessageRunner* fMDClearRunner = nullptr;
    std::map<std::string, class BMessageRunner*> fActivityRunners;
};

} // namespace amp
