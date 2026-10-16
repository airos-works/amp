// Top toolbar: transport buttons, volume slider, LCD-style display, view switcher and search.
#pragma once
#include "core/Model.h"
#include "player/Messages.h"
#include <String.h>
#include <TextControl.h>
#include <View.h>
#include <vector>

namespace amp {

class ToolbarView : public BView {
public:
    // Something besides the playing song that the display follows: a MiniDisc write, a
    // library scan, a Music Assistant sync. With more than one thing to show, the display
    // gets pages (iTunes' status display): the arrow next to the artwork, the dots below it
    // or a click on the text turns to another.
    enum ActivityKind { kActivityMiniDisc, kActivityScan, kActivitySync };
    struct Activity {
        BString id;                    // "minidisc", "scan", "ma-sync"
        ActivityKind kind = kActivityMiniDisc; // the picture on the left
        BString headline, detail;
        float fraction = -1;           // overall progress; below zero the bar animates
        BString leftLabel, rightLabel; // either side of the bar
        uint32 cancelCommand = 0;      // shows a cancel button that sends this to the window
        bool done = false;             // finished: a check mark instead of the cancel button
        bool failed = false;
    };

    ToolbarView();
    ~ToolbarView() override;
    // Adds the activity, or updates the one with its id. A new one is shown at once when
    // `show` is set; otherwise the display stays on its page and the dots tell of the new one.
    void SetActivity(const Activity& activity, bool show = true);
    void RemoveActivity(const char* id);
    const Activity* FindActivity(const char* id) const;
    // Turns the display to the activity (or to the playing song with an empty id).
    void ShowPage(const char* id);

    void SetPlayerState(PlayerState state);
    void SetTrackInfo(const BString& title, const BString& artist, const BString& album, bool isMA);
    // Artwork of the playing track (key plus the request used to fetch it when missing).
    void SetArtwork(const ArtRequest& request);
    // "Lossless", "320 kbps" or empty.
    void SetQuality(const BString& quality);
    void ArtworkChanged() { Invalidate(fLcdRect); }
    void SetProgress(int64_t positionMs, int64_t durationMs);
    void SetVolume(float volume);
    void SetViewMode(int mode);
    // The view buttons pick how songs are listed; the albums, artists and MiniDisc views have
    // a layout of their own and hide them.
    void SetViewButtonsShown(bool shown);
    void SetLoadingText(const BString& text);
    BTextControl* SearchField() { return fSearch; }

    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void FrameResized(float width, float height) override;
    void MessageReceived(BMessage* message) override;
    void DetachedFromWindow() override;

private:
    enum Hot { kNone, kPrev, kPlay, kNext, kVolume, kProgress, kViewList, kViewGrouped, kViewGrid, kCancelActivity,
        kPager, kPageDot, kPageText, kArtwork };
    void DrawActivity(const Activity& activity, BRect lcd);
    void DrawPager(BRect lcd);
    BRect CancelRect() const;
    // The pages the display turns through: the playing song (nullptr) first, then the activities.
    std::vector<const Activity*> Pages() const;
    // The page on show, an index into Pages(); -1 when there is nothing to show (the Amp mark).
    int ShownIndex() const;
    bool HasSong() const { return fState != kStopped && !fTitle.IsEmpty(); }
    void SongMayHaveAppeared();
    void ShowIndex(int index);
    BString PageName(int index) const;
    BRect PagerRect() const;
    BRect PageDotRect(int index, int count) const;
    BRect TextRect() const;       // the two lines of text: a click there turns the page
    int PageDotAt(BPoint where) const;
    void UpdatePulse();
    void Layout();
    void DrawRoundButton(BRect rect, Hot which);
    void DrawLcd();
    void DrawProgress(BRect lcd);
    void DrawVolume();
    void DrawViewButtons();
    Hot HitTest(BPoint where) const;
    float VolumeFromPoint(BPoint where) const;

    BTextControl* fSearch;
    PlayerState fState = kStopped;
    BString fTitle, fArtist, fAlbum, fLoading, fQuality;
    ArtRequest fArt;
    BRect fArtRect;
    bool fIsMA = false;
    int64_t fPosition = 0, fDuration = 0;
    float fVolume = 0.8f;
    int fViewMode = 0;
    bool fViewButtons = true;
    Hot fPressed = kNone;
    bool fTracking = false;
    BRect fPrevRect, fPlayRect, fNextRect, fVolumeRect, fLcdRect, fProgressRect, fViewRect, fSearchRect;
    std::vector<Activity> fActivities;
    BString fShown;               // id of the activity on show; empty: the playing song
    bool fHadSong = false;
    int fPressedDot = -1;
    BString fToolTip;             // what the tool tip says: the pager's next page, the artwork's album
    bool fOverArtwork = false;    // the pointer is a link cursor over the playing song's artwork
    float fStripePhase = 0;
    class BMessageRunner* fPulse = nullptr;
};

} // namespace amp
