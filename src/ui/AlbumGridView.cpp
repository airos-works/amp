#include "AlbumGridView.h"
#include "App.h"
#include "Theme.h"
#include <Bitmap.h>
#include <Font.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Window.h>
#include <cmath>

namespace amp {

namespace {
const float kCellWidth = 172.0f;
const float kCellHeight = 216.0f;
const float kArt = 140.0f;
const float kPadding = 12.0f;
}

AlbumGridView::AlbumGridView()
    : BView("albumgrid", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE | B_SCROLL_VIEW_AWARE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
}

void AlbumGridView::SetCells(std::vector<AlbumCell> cells)
{
    fAlbums = std::move(cells);
    if (fSelected >= (int)fAlbums.size())
        fSelected = -1;
    Relayout();
    if (Bounds().top > MaxScroll())
        ScrollTo(0, MaxScroll());
    Invalidate();
}

BMessage* AlbumGridView::ShowSongsMessage(const AlbumCell& cell) const
{
    // library albums open by id (and follow library changes); playlist albums by their tracks
    if (cell.albumId != 0) {
        BMessage* message = new BMessage(kMsgShowAlbum);
        message->AddInt64("album", cell.albumId);
        return message;
    }
    BMessage* message = new BMessage(kMsgShowTracks);
    for (int64_t id : cell.trackIds)
        message->AddInt64("tracks", id);
    return message;
}

void AlbumGridView::Relayout()
{
    fColumns = std::max(1, (int)((Bounds().Width() - kPadding) / kCellWidth));
    UpdateScrollBar();
}

float AlbumGridView::ContentHeight() const
{
    int rows = ((int)fAlbums.size() + fColumns - 1) / fColumns;
    return kPadding + rows * kCellHeight + kPadding;
}

float AlbumGridView::MaxScroll() const
{
    return std::max(0.0f, ContentHeight() - Bounds().Height());
}

void AlbumGridView::UpdateScrollBar()
{
    BScrollBar* bar = ScrollBar(B_VERTICAL);
    if (!bar)
        return;
    float content = ContentHeight();
    float visible = Bounds().Height();
    bar->SetRange(0, MaxScroll());
    bar->SetProportion(content > 0 ? std::min(1.0f, visible / content) : 1);
    bar->SetSteps(kCellHeight / 4, visible - kCellHeight / 2);
}

void AlbumGridView::FrameResized(float width, float height)
{
    // a change in the number of columns moves every album to another row: keep the row at
    // the top of the view, at the same offset, on the album that began it
    float top = Bounds().top;
    int row = std::max(0, (int)((top - kPadding) / kCellHeight));
    float offset = top - (kPadding + row * kCellHeight);
    int first = row * fColumns;
    int columns = fColumns;
    Relayout();
    if (fColumns != columns && top > 0)
        ScrollTo(0, std::min(MaxScroll(), std::max(0.0f, kPadding + first / fColumns * kCellHeight + offset)));
    Invalidate();
}

BRect AlbumGridView::CellRect(int index) const
{
    int col = index % fColumns;
    int row = index / fColumns;
    float extra = std::max(0.0f, (Bounds().Width() - kPadding - fColumns * kCellWidth) / fColumns);
    float left = kPadding + col * (kCellWidth + extra);
    float top = kPadding + row * kCellHeight;
    return BRect(left, top, left + kCellWidth - 1, top + kCellHeight - 1);
}

int AlbumGridView::CellAt(BPoint where) const
{
    int row = (int)((where.y - kPadding) / kCellHeight);
    if (where.y < kPadding || row < 0)
        return -1;
    for (int col = 0; col < fColumns; col++) {
        int index = row * fColumns + col;
        if (index < (int)fAlbums.size() && CellRect(index).Contains(where))
            return index;
    }
    return -1;
}

void AlbumGridView::Draw(BRect updateRect)
{
    SetHighColor(theme::kListBackground);
    FillRect(updateRect);
    bool active = Window() && Window()->IsActive() && IsFocus();
    if (fAlbums.empty()) {
        BFont font(be_plain_font);
        font.SetSize(14);
        SetFont(&font);
        SetHighColor(theme::kListSecondaryText);
        DrawTruncated(this, "No albums", BRect(Bounds().left, Bounds().top + 40, Bounds().right, Bounds().top + 70), B_ALIGN_CENTER, 0);
        return;
    }
    BFont bold(be_bold_font);
    bold.SetSize(11);
    BFont plain(be_plain_font);
    plain.SetSize(11);
    // only the rows that intersect the update rectangle
    int firstRow = std::max(0, (int)((updateRect.top - kPadding) / kCellHeight));
    int lastRow = (int)((updateRect.bottom - kPadding) / kCellHeight);
    size_t first = (size_t)firstRow * fColumns;
    size_t last = std::min(fAlbums.size(), (size_t)(lastRow + 1) * fColumns);
    for (size_t i = first; i < last; i++) {
        BRect cell = CellRect((int)i);
        if (!cell.Intersects(updateRect))
            continue;
        const AlbumCell& album = fAlbums[i];
        bool selected = (int)i == fSelected;
        BRect art(cell.left + (kCellWidth - kArt) / 2, cell.top + 8, cell.left + (kCellWidth - kArt) / 2 + kArt - 1, cell.top + 8 + kArt - 1);
        if (selected) {
            BRect highlight = cell.InsetByCopy(4, 2);
            if (active)
                FillRoundGradient(this, highlight, 6, theme::kSelectionTop, theme::kSelectionBottom);
            else
                FillRoundGradient(this, highlight, 6, theme::kSelectionInactiveTop, theme::kSelectionInactiveBottom);
        }
        ArtRequest request = App()->Art().RequestFor(album.artKey, album.artist, album.name, album.localHint, false);
        BBitmap* bitmap = App()->Art().Get(album.artKey, (int)kArt, &request);
        // the cover lies on the page: a soft shadow below it, a hairline to hold a pale cover
        DrawArtworkOnPage(this, bitmap, art);
        if (album.isMA)
            DrawMABadge(this, BPoint(art.left + 5, art.top + 5), 12);
        SetFont(&bold);
        SetHighColor(selected ? theme::kSelectedText : theme::kListText);
        float text = art.bottom + 10; // below the shadow
        DrawTruncated(this, album.name.c_str(), BRect(cell.left + 6, text, cell.right - 6, text + 16), B_ALIGN_CENTER, 0);
        SetFont(&plain);
        SetHighColor(selected ? theme::kSelectedText : theme::kListSecondaryText);
        DrawTruncated(this, album.artist.c_str(), BRect(cell.left + 6, text + 16, cell.right - 6, text + 32), B_ALIGN_CENTER, 0);
        BString info;
        info << (int)album.trackIds.size() << (album.trackIds.size() == 1 ? " song" : " songs");
        if (album.year > 0)
            info << " · " << album.year;
        DrawTruncated(this, info.String(), BRect(cell.left + 6, text + 32, cell.right - 6, text + 48), B_ALIGN_CENTER, 0);
    }
}

void AlbumGridView::MouseDown(BPoint where)
{
    MakeFocus(true);
    int32 buttons = 0, clicks = 1;
    Window()->CurrentMessage()->FindInt32("buttons", &buttons);
    Window()->CurrentMessage()->FindInt32("clicks", &clicks);
    int index = CellAt(where);
    fSelected = index;
    Invalidate();
    if (index < 0)
        return;
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        ShowContextMenu(index, where);
        return;
    }
    if (clicks >= 2) {
        BMessage* message = ShowSongsMessage(fAlbums[index]);
        Window()->PostMessage(message);
        delete message;
        return;
    }
    fClickPoint = where;
    fMaybeDrag = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
}

void AlbumGridView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    if (!fMaybeDrag || drag)
        return;
    int32 buttons = 0;
    Window()->CurrentMessage()->FindInt32("buttons", &buttons);
    if (!buttons) {
        fMaybeDrag = false;
        return;
    }
    if (fabs(where.x - fClickPoint.x) > 4 || fabs(where.y - fClickPoint.y) > 4) {
        fMaybeDrag = false;
        if (fSelected >= 0)
            StartDrag(fSelected, where);
    }
}

void AlbumGridView::StartDrag(int index, BPoint where)
{
    const AlbumCell& album = fAlbums[index];
    if (album.trackIds.empty())
        return;
    BMessage drag(kMsgTrackDrag);
    for (int64_t id : album.trackIds)
        drag.AddInt64("tracks", id);
    drag.AddInt64("playlist", 0);
    DragMessage(&drag, CellRect(index), this);
}

void AlbumGridView::KeyDown(const char* bytes, int32 numBytes)
{
    if (numBytes < 1 || fAlbums.empty()) {
        BView::KeyDown(bytes, numBytes);
        return;
    }
    int next = fSelected;
    switch (bytes[0]) {
        case B_LEFT_ARROW: next = std::max(0, fSelected - 1); break;
        case B_RIGHT_ARROW: next = std::min((int)fAlbums.size() - 1, fSelected + 1); break;
        case B_UP_ARROW: next = std::max(0, fSelected - fColumns); break;
        case B_DOWN_ARROW: next = std::min((int)fAlbums.size() - 1, fSelected + fColumns); break;
        case B_ENTER:
            if (fSelected >= 0) {
                BMessage* message = ShowSongsMessage(fAlbums[fSelected]);
                Window()->PostMessage(message);
                delete message;
            }
            return;
        case ' ':
            Window()->PostMessage(kMsgPlayPause);
            return;
        default:
            BView::KeyDown(bytes, numBytes);
            return;
    }
    if (next != fSelected) {
        fSelected = next;
        BRect cell = CellRect(next);
        BRect bounds = Bounds();
        if (cell.top < bounds.top)
            ScrollTo(0, cell.top - kPadding);
        else if (cell.bottom > bounds.bottom)
            ScrollTo(0, cell.bottom - bounds.Height() + kPadding);
        Invalidate();
    }
}

void AlbumGridView::ShowContextMenu(int index, BPoint where)
{
    Library& library = App()->GetLibrary();
    std::vector<int64_t> tracks = fAlbums[index].trackIds;
    BPopUpMenu* menu = new BPopUpMenu("album-menu", false, false);
    auto withTracks = [&](uint32 what) {
        BMessage* message = new BMessage(what);
        for (int64_t id : tracks)
            message->AddInt64("tracks", id);
        return message;
    };
    BMessage* play = withTracks(kMsgPlayTracks);
    play->AddInt32("index", 0);
    menu->AddItem(new BMenuItem("Play Album", play));
    BMessage* next = withTracks(kMsgPlayTracks);
    next->AddBool("next", true);
    menu->AddItem(new BMenuItem("Play Next", next));
    menu->AddItem(new BMenuItem("Show Songs", ShowSongsMessage(fAlbums[index])));
    MiniDiscState miniDisc = App()->MiniDisc().State();
    if (miniDisc.connected) {
        BMessage* write = withTracks(kMsgWriteToMiniDisc);
        write->AddString("kind", "album");
        BString name(fAlbums[index].name.c_str());
        if (!fAlbums[index].artist.empty())
            name.Prepend(" - ").Prepend(fAlbums[index].artist.c_str());
        write->AddString("name", name);
        BMenuItem* item = new BMenuItem("Write Album to MiniDisc…", write);
        item->SetEnabled(!miniDisc.busy);
        menu->AddItem(item);
    }
    menu->AddSeparatorItem();
    BMenu* playlists = new BMenu("Add to Playlist");
    playlists->AddItem(new BMenuItem("New Playlist…", withTracks(kMsgNewPlaylist)));
    playlists->AddSeparatorItem();
    {
        Library::Locker locker(library);
        for (int64_t id : library.AllPlaylistIds()) {
            const Playlist* playlist = library.PlaylistById(id);
            if (!playlist || playlist->isMA())
                continue;
            BMessage* add = withTracks(kMsgAddToPlaylist);
            add->AddInt64("playlist", id);
            playlists->AddItem(new BMenuItem(playlist->name.c_str(), add));
        }
    }
    playlists->SetTargetForItems(Window());
    menu->AddItem(playlists);
    menu->SetTargetForItems(Window());
    menu->SetAsyncAutoDestruct(true);
    menu->Go(ConvertToScreen(where), true, true, true);
}

} // namespace amp
