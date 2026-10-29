// The track table: plain rows or iTunes-style album groups with artwork, sorting, multi-selection,
// keyboard navigation, drag & drop to playlists and reordering inside a playlist.
#pragma once
#include "core/Model.h"
#include "player/Messages.h"
#include <String.h>
#include <View.h>
#include <set>
#include <string>
#include <vector>

namespace amp {

class TrackHeaderView;

class TrackListView : public BView {
public:
    enum Mode { kPlain = 0, kGrouped = 1 };

    TrackListView(const char* name = "tracklist");

    // The column header is its own view, placed above the scroll view: it is
    // painted once and stays put while the rows scroll. The returned view is
    // owned by the list.
    TrackHeaderView* HeaderView();

    // positions: the playlist position of every row when the rows are a filtered subset of an
    // editable playlist (empty = rows are the playlist itself, which also allows reordering).
    void SetTracks(const std::vector<int64_t>& ids, int mode, int64_t playlistId,
        const std::vector<int32>& positions = std::vector<int32>());
    void SetNowPlaying(int64_t trackId, PlayerState state);
    std::vector<int64_t> SelectedTracks() const;
    std::vector<int32> SelectedPositions() const;
    void SelectTrack(int64_t trackId, bool scrollTo);
    void ClearSelection();
    const std::vector<int64_t>& Tracks() const { return fTracks; }
    int64_t PlaylistId() const { return fPlaylistId; }
    void Refresh() { Invalidate(); }
    void SetEmptyText(const char* text) { fEmptyText = text; }

    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void KeyDown(const char* bytes, int32 numBytes) override;
    void FrameResized(float width, float height) override;
    void MessageReceived(BMessage* message) override;
    void WindowActivated(bool active) override { Invalidate(); }
    void MakeFocus(bool focus) override;

private:
    friend class TrackHeaderView;

    struct Column {
        const char* title;
        float width;
        int field;         // see kField* constants
        alignment align;
        bool flexible;
    };
    // A run of consecutive rows from one album. Groups carry their own display data because
    // playlist tracks need not belong to a library album.
    struct Group {
        int64_t albumId;
        int firstRow;
        int rowCount;
        float top;
        float height;
        std::string name, artist, artKey, localHint;
        int year;
    };
    void Relayout();
    void UpdateScrollBar();
    float ContentHeight() const;
    BRect RowRect(int row) const;
    int RowAt(BPoint where) const;
    int GroupAt(BPoint where) const;
    float RowsLeft() const;
    void LayoutColumns();
    int ColumnAt(float x, float* leftOut) const;
    void DrawHeader(BView* target, BRect bounds);
    void InvalidateHeader();
    void HeaderMouseDown(BPoint where, BView* target);
    void HeaderMouseMoved(BPoint where);
    void HeaderMouseUp();
    void DrawRow(int row, BRect rect, bool selected, bool active);
    void DrawGroup(const Group& group, BRect rect);
    void SortTracks();
    void PlayRow(int row);
    void ShowContextMenu(int row, BPoint where);
    void StartDrag(BPoint where);
    int DropIndexAt(BPoint where) const;
    void ScrollToRow(int row);
    void PostSelectionChanged();
    BString CellText(const Track& track, int field) const;

    std::vector<int64_t> fTracks;
    std::vector<int> fGroupOfRow;
    std::vector<Group> fGroups;
    std::vector<Column> fColumns;
    int fMode = kPlain;
    int64_t fPlaylistId = 0;
    std::set<int> fSelection;
    int fAnchor = -1;
    int fFocusRow = -1;
    int64_t fNowPlaying = 0;
    PlayerState fNowState = kStopped;
    int fSortField = -1;
    bool fSortAscending = true;
    BPoint fClickPoint;
    int fClickRow = -1;
    bool fMaybeDrag = false;
    int fDropIndex = -1;
    int fResizeColumn = -1;
    float fResizeStartX = 0;
    float fResizeStartWidth = 0;
    BString fEmptyText;
    std::vector<int32> fPositions;
    int32 PositionOfRow(int row) const;
    bool CanReorder() const { return fPlaylistId != 0 && fMode == kPlain && fPositions.empty(); }

    TrackHeaderView* fHeaderView = nullptr;
};

// The fixed column header above a TrackListView's scroll view.
class TrackHeaderView : public BView {
public:
    explicit TrackHeaderView(TrackListView* list, const char* name = "trackheader");
    ~TrackHeaderView() override;

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void MouseUp(BPoint where) override;

private:
    TrackListView* fList;
};

} // namespace amp
