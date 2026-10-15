// Artists browser: artist list with images on the left, the artist's albums and songs on the right.
#pragma once
#include "TrackListView.h"
#include <String.h>
#include <View.h>
#include <string>
#include <vector>

namespace amp {

const uint32 kMsgArtistSelected = 'arsl';

class ArtistListView : public BView {
public:
    ArtistListView();
    void SetOwner(BHandler* owner) { fOwner = owner; }
    void SetArtists(const std::vector<int64_t>& ids);
    void Select(int64_t artistId, bool notify);
    int64_t Selected() const { return fSelected; }

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void KeyDown(const char* bytes, int32 numBytes) override;
    void FrameResized(float width, float height) override;
    void WindowActivated(bool active) override { Invalidate(); }

private:
    void UpdateScrollBar();
    int IndexAt(BPoint where) const;
    void ScrollToIndex(int index);

    std::vector<int64_t> fArtists;
    int64_t fSelected = 0;
    BHandler* fOwner = nullptr;
};

// The selected artist's picture, name and figures. The picture and the name link to the
// artist on MusicBrainz (kMsgOpenArtistPage to the window).
class ArtistHeaderView : public BView {
public:
    ArtistHeaderView();
    void SetArtist(int64_t artistId);
    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;

private:
    bool OverLink(BPoint where) const;
    void SetHot(bool hot);

    int64_t fArtist = 0;
    BRect fArtRect, fNameRect;   // where the last Draw put the picture and the name
    bool fHot = false;           // the pointer is over one of them
};

class ArtistsView : public BView {
public:
    ArtistsView();
    void SetArtists(const std::vector<int64_t>& ids);
    void SelectArtist(int64_t artistId);
    int64_t SelectedArtist() const { return fList->Selected(); }
    TrackListView* TrackList() { return fTracks; }
    void Refresh();
    void ReloadTracks();
    void MessageReceived(BMessage* message) override;

private:
    void RememberSelection();

    ArtistListView* fList;
    ArtistHeaderView* fHeader;
    TrackListView* fTracks;
    std::string fSelectedName;   // the selected artist by name: ids change with every index rebuild
};

} // namespace amp
