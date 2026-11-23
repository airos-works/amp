// Grid of album covers (the iTunes "grid" view).
#pragma once
#include <String.h>
#include <View.h>
#include <cstdint>
#include <string>
#include <vector>

namespace amp {

// One cover in the grid. Cells describe themselves so the grid can show library albums as
// well as the albums found inside a playlist (whose tracks may not be library items).
struct AlbumCell {
    int64_t albumId = 0;             // library album, or 0 for a playlist-only album
    std::string name, artist, artKey, localHint;
    int year = 0;
    std::vector<int64_t> trackIds;   // the tracks this cell stands for, in play order
    // The touch library's tiles stand for artists and playlists too: they open those
    // instead of an album, and say what they hold in `caption`.
    int64_t artistId = 0;
    int64_t playlistId = 0;
    std::string caption;
    // a playlist tile shows the covers of its first four albums
    struct Cover {
        std::string key, artist, album, hint;
    };
    std::vector<Cover> mosaic;
};

class AlbumGridView : public BView {
public:
    // The touch presentation: larger tiles on a paper page, a tap opens a tile and a drag
    // scrolls the page.
    void SetMobile(bool mobile);
    AlbumGridView();
    void SetCells(std::vector<AlbumCell> cells);
    // what the touch page says while it has no tiles
    void SetEmptyText(const char* text) { fEmptyText = text; }
    void Refresh() { Invalidate(); }

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void MouseUp(BPoint where) override;
    void FrameResized(float width, float height) override;
    void KeyDown(const char* bytes, int32 numBytes) override;
    void WindowActivated(bool active) override { Invalidate(); }

private:
    bool fMobile = false;
    BString fEmptyText = "No Albums";
    float CellWidth() const;
    float CellHeight() const;
    float ArtSize() const;
    float Padding() const;
    void DrawMobileCell(const AlbumCell& cell, BRect rect, bool pressed);
    void DrawMosaic(const AlbumCell& cell, BRect art);
    void Open(int index);
    // a finger on the page: a tap opens the tile under it, a drag scrolls
    int fTouchCell = -1;
    bool fTouchScrolling = false;
    BPoint fTouchStart;
    float fTouchScroll = 0;
    void Relayout();
    float ContentHeight() const;
    float MaxScroll() const;
    void UpdateScrollBar();
    int CellAt(BPoint where) const;
    BRect CellRect(int index) const;
    void ShowContextMenu(int index, BPoint where);
    void StartDrag(int index, BPoint where);

    std::vector<AlbumCell> fAlbums;
    BMessage* ShowSongsMessage(const AlbumCell& cell) const;
    int fColumns = 1;
    int fSelected = -1;
    BPoint fClickPoint;
    bool fMaybeDrag = false;
};

} // namespace amp
