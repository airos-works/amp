// Grid of album covers (the iTunes "grid" view).
#pragma once
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
    bool isMA = false;
    std::vector<int64_t> trackIds;   // the tracks this cell stands for, in play order
};

class AlbumGridView : public BView {
public:
    AlbumGridView();
    void SetCells(std::vector<AlbumCell> cells);
    void Refresh() { Invalidate(); }

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void FrameResized(float width, float height) override;
    void KeyDown(const char* bytes, int32 numBytes) override;
    void WindowActivated(bool active) override { Invalidate(); }

private:
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
