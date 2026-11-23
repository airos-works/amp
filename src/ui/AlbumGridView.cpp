#include "AlbumGridView.h"
#include "compat/MobileKit.h"

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

float AlbumGridView::ArtSize() const
{
    return fMobile ? roundf(BMobileKit::TargetSize() * 3.2f) : kArt;
}

float AlbumGridView::Padding() const
{
    return fMobile ? roundf(BMobileKit::TargetSize() * 0.4f) : kPadding;
}

float AlbumGridView::CellWidth() const
{
    return fMobile ? ArtSize() + 2 * Padding() : kCellWidth;
}

float AlbumGridView::CellHeight() const
{
    if (!fMobile)
        return kCellHeight;
    font_height height;
    be_plain_font->GetHeight(&height);
    float line = ceilf(height.ascent + height.descent + height.leading);
    return ArtSize() + Padding() * 1.5f + line * 2.6f;
}

void AlbumGridView::SetMobile(bool mobile)
{
    if (mobile == fMobile)
        return;
    fMobile = mobile;
    fTouchCell = -1;
    Relayout();
    ScrollTo(0, 0);
    Invalidate();
}

AlbumGridView::AlbumGridView()
    : BView("albumgrid", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE | B_SCROLL_VIEW_AWARE)
{
    SetExplicitMinSize(BSize(0, 0));
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
    if (cell.playlistId != 0) {
        BMessage* message = new BMessage(kMsgSourceSelected);
        message->AddString("source", "playlist");
        message->AddInt64("playlist", cell.playlistId);
        return message;
    }
    if (cell.artistId != 0) {
        BMessage* message = new BMessage(kMsgShowArtist);
        message->AddInt64("artist", cell.artistId);
        return message;
    }
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
    fColumns = std::max(1, (int)((Bounds().Width() - Padding()) / CellWidth()));
    UpdateScrollBar();
}

float AlbumGridView::ContentHeight() const
{
    int rows = ((int)fAlbums.size() + fColumns - 1) / fColumns;
    return Padding() + rows * CellHeight() + Padding();
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
    bar->SetSteps(CellHeight() / 4, visible - CellHeight() / 2);
}

void AlbumGridView::FrameResized(float width, float height)
{
    // a change in the number of columns moves every album to another row: keep the row at
    // the top of the view, at the same offset, on the album that began it
    float top = Bounds().top;
    int row = std::max(0, (int)((top - Padding()) / CellHeight()));
    float offset = top - (Padding() + row * CellHeight());
    int first = row * fColumns;
    int columns = fColumns;
    Relayout();
    if (fColumns != columns && top > 0)
        ScrollTo(0, std::min(MaxScroll(), std::max(0.0f, Padding() + first / fColumns * CellHeight() + offset)));
    Invalidate();
}

BRect AlbumGridView::CellRect(int index) const
{
    int col = index % fColumns;
    int row = index / fColumns;
    float extra = std::max(0.0f, (Bounds().Width() - Padding() - fColumns * CellWidth()) / fColumns);
    float left = Padding() + col * (CellWidth() + extra);
    float top = Padding() + row * CellHeight();
    return BRect(left, top, left + CellWidth() - 1, top + CellHeight() - 1);
}

int AlbumGridView::CellAt(BPoint where) const
{
    int row = (int)((where.y - Padding()) / CellHeight());
    if (where.y < Padding() || row < 0)
        return -1;
    for (int col = 0; col < fColumns; col++) {
        int index = row * fColumns + col;
        if (index < (int)fAlbums.size() && CellRect(index).Contains(where))
            return index;
    }
    return -1;
}

void AlbumGridView::DrawMosaic(const AlbumCell& cell, BRect art)
{
    // four covers in a square, like a stack of records seen from above
    DrawSoftShadow(this, art);
    float half = floorf(art.Width() / 2);
    for (size_t i = 0; i < 4; i++) {
        BRect tile(art.left + (i % 2) * (half + 1), art.top + (i / 2) * (half + 1),
            art.left + (i % 2) * (half + 1) + half - 1, art.top + (i / 2) * (half + 1) + half - 1);
        if (i % 2)
            tile.right = art.right;
        if (i / 2)
            tile.bottom = art.bottom;
        const AlbumCell::Cover& cover = cell.mosaic[i % cell.mosaic.size()];
        ArtRequest request = App()->Art().RequestFor(cover.key, cover.artist, cover.album, cover.hint, false);
        BBitmap* bitmap = App()->Art().Get(cover.key, (int)half, &request);
        if (bitmap)
            DrawBitmapFitted(this, bitmap, tile);
        else
            DrawArtPlaceholder(this, tile);
    }
}

void AlbumGridView::DrawMobileCell(const AlbumCell& cell, BRect rect, bool pressed)
{
    const BMobileKit::Skin& skin = BMobileKit::CurrentSkin();
    float art = ArtSize();
    BRect cover(rect.left + floorf((rect.Width() - art) / 2), rect.top + Padding(),
        rect.left + floorf((rect.Width() - art) / 2) + art - 1, rect.top + Padding() + art - 1);
    if (cell.mosaic.size() >= 4)
        DrawMosaic(cell, cover);
    else if (!cell.mosaic.empty()) {
        // a tile that borrows an album's cover (an artist, a short playlist)
        const AlbumCell::Cover& from = cell.mosaic.front();
        ArtRequest request = App()->Art().RequestFor(from.key, from.artist, from.album, from.hint, false);
        cover = DrawArtworkOnPage(this, App()->Art().Get(from.key, (int)art, &request), cover);
    } else if (cell.playlistId != 0) {
        cover = DrawArtworkOnPage(this, nullptr, cover); // an empty playlist
    } else {
        ArtRequest request = App()->Art().RequestFor(cell.artKey, cell.artist, cell.name, cell.localHint, false);
        BBitmap* bitmap = App()->Art().Get(cell.artKey, (int)art, &request);
        cover = DrawArtworkOnPage(this, bitmap, cover);
    }
    if (pressed) {
        // the tile darkens under the finger
        SetDrawingMode(B_OP_ALPHA);
        SetHighColor(0, 0, 0, 90);
        FillRect(cover);
        SetDrawingMode(B_OP_COPY);
    }
    font_height height;
    be_plain_font->GetHeight(&height);
    float line = ceilf(height.ascent + height.descent + height.leading);
    BRect text(rect.left + 2, cover.bottom + Padding() * 0.6f, rect.right - 2,
        cover.bottom + Padding() * 0.6f + line);
    BFont bold(be_bold_font);
    bold.SetSize(be_plain_font->Size());
    SetFont(&bold);
    SetHighColor(skin.pageText);
    DrawTruncated(this, cell.name.c_str(), text, B_ALIGN_CENTER, 0);
    BFont small(be_plain_font);
    small.SetSize(be_plain_font->Size() * 0.92f);
    SetFont(&small);
    SetHighColor(skin.pageSecondary);
    const std::string& caption = cell.caption.empty() ? cell.artist : cell.caption;
    DrawTruncated(this, caption.c_str(), text.OffsetByCopy(0, line), B_ALIGN_CENTER, 0);
}

void AlbumGridView::Draw(BRect updateRect)
{
    if (fMobile) {
        BMobileKit::DrawPage(this, Bounds(), updateRect);
        if (fAlbums.empty()) {
            SetFont(be_bold_font);
            SetHighColor(BMobileKit::CurrentSkin().pageSecondary);
            BRect message = Bounds();
            message.bottom = message.top + BMobileKit::TargetSize() * 3;
            DrawTruncated(this, fEmptyText.String(), message, B_ALIGN_CENTER, 0);
            return;
        }
        int firstRow = std::max(0, (int)((updateRect.top - Padding()) / CellHeight()));
        int lastRow = (int)((updateRect.bottom - Padding()) / CellHeight());
        size_t first = (size_t)firstRow * fColumns;
        size_t last = std::min(fAlbums.size(), (size_t)(lastRow + 1) * fColumns);
        for (size_t i = first; i < last; i++) {
            BRect cell = CellRect((int)i);
            if (cell.Intersects(updateRect))
                DrawMobileCell(fAlbums[i], cell, (int)i == fTouchCell && !fTouchScrolling);
        }
        return;
    }
    SetHighColor(theme::Colors().listBackground);
    FillRect(updateRect);
    bool active = Window() && Window()->IsActive() && IsFocus();
    if (fAlbums.empty()) {
        BFont font(be_plain_font);
        font.SetSize(14);
        SetFont(&font);
        SetHighColor(theme::Colors().listSecondaryText);
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
                FillRoundGradient(this, highlight, 6, theme::Colors().selectionTop, theme::Colors().selectionBottom);
            else
                FillRoundGradient(this, highlight, 6, theme::Colors().selectionInactiveTop, theme::Colors().selectionInactiveBottom);
        }
        ArtRequest request = App()->Art().RequestFor(album.artKey, album.artist, album.name, album.localHint, false);
        BBitmap* bitmap = App()->Art().Get(album.artKey, (int)kArt, &request);
        // the cover lies on the page: a soft shadow below it, a hairline to hold a pale cover
        DrawArtworkOnPage(this, bitmap, art);
        SetFont(&bold);
        SetHighColor(selected ? theme::Colors().selectedText : theme::Colors().listText);
        float text = art.bottom + 10; // below the shadow
        DrawTruncated(this, album.name.c_str(), BRect(cell.left + 6, text, cell.right - 6, text + 16), B_ALIGN_CENTER, 0);
        SetFont(&plain);
        SetHighColor(selected ? theme::Colors().selectedText : theme::Colors().listSecondaryText);
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
    if (fMobile && !(buttons & B_SECONDARY_MOUSE_BUTTON)) {
        // nothing happens until the finger lifts: it may be the start of a drag
        fTouchCell = index;
        fTouchScrolling = false;
        fTouchStart = ConvertToScreen(where);
        fTouchScroll = Bounds().top;
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
        if (index >= 0)
            Invalidate(CellRect(index));
        return;
    }
    fSelected = index;
    Invalidate();
    if (index < 0)
        return;
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        ShowContextMenu(index, where);
        return;
    }
    if (clicks >= 2) {
        Open(index);
        return;
    }
    fClickPoint = where;
    fMaybeDrag = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
}

void AlbumGridView::Open(int index)
{
    if (index < 0 || index >= (int)fAlbums.size())
        return;
    BMessage* message = ShowSongsMessage(fAlbums[index]);
    Window()->PostMessage(message);
    delete message;
}

void AlbumGridView::MouseUp(BPoint where)
{
    if (!fMobile)
        return;
    int index = fTouchCell;
    bool tapped = !fTouchScrolling && index >= 0 && CellAt(where) == index;
    fTouchCell = -1;
    fTouchScrolling = false;
    if (index >= 0)
        Invalidate(CellRect(index));
    if (tapped)
        Open(index);
}

void AlbumGridView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    if (fMobile) {
        int32 buttons = 0;
        Window()->CurrentMessage()->FindInt32("buttons", &buttons);
        if (buttons == 0 || drag)
            return;
        // the page follows the finger (screen coordinates: the view moves under it)
        BPoint screen = ConvertToScreen(where);
        float distance = screen.y - fTouchStart.y;
        if (!fTouchScrolling && fabs(distance) > BMobileKit::TargetSize() * 0.2f) {
            fTouchScrolling = true;
            if (fTouchCell >= 0)
                Invalidate(CellRect(fTouchCell));
        }
        if (fTouchScrolling) {
            float top = std::max(0.0f, std::min(MaxScroll(), fTouchScroll - distance));
            if (top != Bounds().top)
                ScrollTo(0, top);
        }
        return;
    }
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
            Open(fSelected);
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
            ScrollTo(0, cell.top - Padding());
        else if (cell.bottom > bounds.bottom)
            ScrollTo(0, cell.bottom - bounds.Height() + Padding());
        Invalidate();
    }
}

void AlbumGridView::ShowContextMenu(int index, BPoint where)
{
    if (fAlbums[index].playlistId != 0 || fAlbums[index].artistId != 0)
        return; // the album commands do not fit an artist's or a playlist's tile
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
    BMessage* label = withTracks(kMsgPrintLabel);
    label->AddString("kind", "album");
    label->AddString("name", fAlbums[index].name.c_str());
    if (!fAlbums[index].artist.empty())
        label->AddString("artist", fAlbums[index].artist.c_str());
    menu->AddItem(new BMenuItem("Print Label…", label));
    menu->AddSeparatorItem();
    BMenu* playlists = new BMenu("Add to Playlist");
    playlists->AddItem(new BMenuItem("New Playlist…", withTracks(kMsgNewPlaylist)));
    playlists->AddSeparatorItem();
    {
        Library::Locker locker(library);
        for (int64_t id : library.AllPlaylistIds()) {
            const Playlist* playlist = library.PlaylistById(id);
            if (!playlist)
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
