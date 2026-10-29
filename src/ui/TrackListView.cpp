#include "TrackListView.h"
#include "App.h"
#include "Theme.h"
#include "Icons.h"
#include <Bitmap.h>
#include <Font.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Window.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace amp {

namespace {
enum { kFieldIndicator = 0, kFieldNumber, kFieldName, kFieldTime, kFieldArtist, kFieldAlbum, kFieldQuality, kFieldYear };
const float kArtColumnWidth = 176.0f;
const float kGroupMinHeight = 182.0f;
const float kArtSize = 128.0f;
}

TrackListView::TrackListView(const char* name)
    : BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE | B_SCROLL_VIEW_AWARE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    fColumns = {
        {"", 18, kFieldIndicator, B_ALIGN_CENTER, false},
        {"#", 32, kFieldNumber, B_ALIGN_RIGHT, false},
        {"Name", 200, kFieldName, B_ALIGN_LEFT, true},
        {"Time", 52, kFieldTime, B_ALIGN_RIGHT, false},
        {"Artist", 170, kFieldArtist, B_ALIGN_LEFT, false},
        {"Album", 170, kFieldAlbum, B_ALIGN_LEFT, false},
        {"Quality", 76, kFieldQuality, B_ALIGN_LEFT, false},
        {"Year", 46, kFieldYear, B_ALIGN_RIGHT, false},
    };
    fEmptyText = "No songs";
}

void TrackListView::AttachedToWindow()
{
    BView::AttachedToWindow();
    LayoutColumns();
    UpdateScrollBar();
}

void TrackListView::MakeFocus(bool focus)
{
    BView::MakeFocus(focus);
    Invalidate();
}

void TrackListView::SetTracks(const std::vector<int64_t>& ids, int mode, int64_t playlistId,
    const std::vector<int32>& positions)
{
    // keep selection by track id when the same list is refreshed
    std::set<int64_t> selectedIds;
    for (int row : fSelection)
        if (row < (int)fTracks.size())
            selectedIds.insert(fTracks[row]);
    fTracks = ids;
    fMode = mode;
    fPlaylistId = playlistId;
    fPositions = positions.size() == ids.size() ? positions : std::vector<int32>();
    if (fPlaylistId != 0)
        fSortField = -1; // playlist order is authoritative
    if (fSortField >= 0 && fMode == kPlain)
        SortTracks();
    fSelection.clear();
    for (size_t i = 0; i < fTracks.size(); i++)
        if (selectedIds.count(fTracks[i]))
            fSelection.insert((int)i);
    if (fFocusRow >= (int)fTracks.size())
        fFocusRow = (int)fTracks.size() - 1;
    fDropIndex = -1;
    Relayout();
    // a shorter list may leave the view scrolled past its end
    float maxScroll = std::max(0.0f, ContentHeight() - Bounds().Height());
    if (Bounds().top > maxScroll)
        ScrollTo(0, maxScroll);
    Invalidate();
    InvalidateHeader(); // the mode decides whether the artwork column shows
}

int32 TrackListView::PositionOfRow(int row) const
{
    return (row >= 0 && row < (int)fPositions.size()) ? fPositions[row] : row;
}

void TrackListView::SetNowPlaying(int64_t trackId, PlayerState state)
{
    if (fNowPlaying == trackId && fNowState == state)
        return;
    fNowPlaying = trackId;
    fNowState = state;
    Invalidate();
}

std::vector<int64_t> TrackListView::SelectedTracks() const
{
    std::vector<int64_t> ids;
    for (int row : fSelection)
        if (row >= 0 && row < (int)fTracks.size())
            ids.push_back(fTracks[row]);
    return ids;
}

std::vector<int32> TrackListView::SelectedPositions() const
{
    std::vector<int32> positions;
    for (int row : fSelection)
        positions.push_back(PositionOfRow(row));
    return positions;
}

void TrackListView::ClearSelection()
{
    fSelection.clear();
    fAnchor = fFocusRow = -1;
    Invalidate();
}

void TrackListView::SelectTrack(int64_t trackId, bool scrollTo)
{
    for (size_t i = 0; i < fTracks.size(); i++) {
        if (fTracks[i] == trackId) {
            fSelection.clear();
            fSelection.insert((int)i);
            fFocusRow = fAnchor = (int)i;
            if (scrollTo)
                ScrollToRow((int)i);
            Invalidate();
            PostSelectionChanged();
            return;
        }
    }
}

void TrackListView::Relayout()
{
    fGroups.clear();
    fGroupOfRow.assign(fTracks.size(), -1);
    if (fMode == kGrouped) {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        float y = 0;
        const Track* previous = nullptr;
        auto sameAlbum = [](const Track* a, const Track* b) {
            if (!a || !b)
                return false;
            if (a->albumId != 0 || b->albumId != 0)
                return a->albumId == b->albumId;
            return ToLower(a->album) == ToLower(b->album)
                && ToLower(a->groupingArtist()) == ToLower(b->groupingArtist());
        };
        for (size_t i = 0; i < fTracks.size(); i++) {
            const Track* track = library.TrackById(fTracks[i]);
            if (fGroups.empty() || !sameAlbum(previous, track)) {
                if (!fGroups.empty()) {
                    Group& last = fGroups.back();
                    last.height = std::max(kGroupMinHeight, last.rowCount * theme::kRowHeight);
                    y += last.height;
                }
                Group group;
                group.albumId = track ? track->albumId : 0;
                group.firstRow = (int)i;
                group.rowCount = 0;
                group.top = y;
                group.height = 0;
                group.year = 0;
                const Album* album = track ? library.AlbumById(track->albumId) : nullptr;
                if (album) {
                    group.name = album->name;
                    group.artist = album->artist;
                    group.artKey = album->art;
                    group.year = album->year;
                } else if (track) {
                    // a song the index has no album for yet
                    group.name = track->album.empty() ? "Unknown Album" : track->album;
                    group.artist = track->groupingArtist().empty() ? "Unknown Artist" : track->groupingArtist();
                    group.artKey = track->art.empty() ? MakeAlbumArtKey(group.artist, group.name) : track->art;
                    group.year = track->year;
                }
                if (track)
                    group.localHint = track->uri;
                fGroups.push_back(group);
            }
            previous = track;
            fGroups.back().rowCount++;
            fGroupOfRow[i] = (int)fGroups.size() - 1;
        }
        if (!fGroups.empty()) {
            Group& last = fGroups.back();
            last.height = std::max(kGroupMinHeight, last.rowCount * theme::kRowHeight);
        }
    }
    LayoutColumns();
    UpdateScrollBar();
}

float TrackListView::ContentHeight() const
{
    if (fMode == kGrouped) {
        if (fGroups.empty())
            return 0;
        const Group& last = fGroups.back();
        return last.top + last.height;
    }
    return fTracks.size() * theme::kRowHeight;
}

void TrackListView::UpdateScrollBar()
{
    BScrollBar* bar = ScrollBar(B_VERTICAL);
    if (!bar)
        return;
    float visible = Bounds().Height();
    float content = ContentHeight();
    float max = std::max(0.0f, content - visible);
    bar->SetRange(0, max);
    bar->SetProportion(content > 0 ? std::min(1.0f, visible / content) : 1.0f);
    bar->SetSteps(theme::kRowHeight, std::max(theme::kRowHeight, visible - theme::kRowHeight));
}

void TrackListView::FrameResized(float width, float height)
{
    LayoutColumns();
    UpdateScrollBar();
    Invalidate();
    InvalidateHeader(); // the flexible column width changed
}

float TrackListView::RowsLeft() const
{
    return fMode == kGrouped ? kArtColumnWidth : 0;
}

void TrackListView::LayoutColumns()
{
    float available = Bounds().Width() - RowsLeft();
    float fixed = 0;
    for (const Column& c : fColumns)
        if (!c.flexible)
            fixed += c.width;
    for (Column& c : fColumns)
        if (c.flexible)
            c.width = std::max(120.0f, available - fixed);
}

int TrackListView::ColumnAt(float x, float* leftOut) const
{
    float left = RowsLeft();
    for (size_t i = 0; i < fColumns.size(); i++) {
        if (x >= left && x < left + fColumns[i].width) {
            if (leftOut)
                *leftOut = left;
            return (int)i;
        }
        left += fColumns[i].width;
    }
    return -1;
}

BRect TrackListView::RowRect(int row) const
{
    float top;
    if (fMode == kGrouped) {
        int g = (row >= 0 && row < (int)fGroupOfRow.size()) ? fGroupOfRow[row] : -1;
        if (g < 0)
            return BRect();
        const Group& group = fGroups[g];
        top = group.top + (row - group.firstRow) * theme::kRowHeight;
    } else
        top = row * theme::kRowHeight;
    return BRect(RowsLeft(), top, Bounds().right, top + theme::kRowHeight - 1);
}

int TrackListView::RowAt(BPoint where) const
{
    if (fMode == kGrouped) {
        if (where.x < RowsLeft())
            return -1;
        int g = GroupAt(where);
        if (g < 0)
            return -1;
        const Group& group = fGroups[g];
        int row = group.firstRow + (int)((where.y - group.top) / theme::kRowHeight);
        if (row >= group.firstRow + group.rowCount)
            return -1;
        return row;
    }
    int row = (int)(where.y / theme::kRowHeight);
    return (row >= 0 && row < (int)fTracks.size()) ? row : -1;
}

int TrackListView::GroupAt(BPoint where) const
{
    for (size_t i = 0; i < fGroups.size(); i++)
        if (where.y >= fGroups[i].top && where.y < fGroups[i].top + fGroups[i].height)
            return (int)i;
    return -1;
}

BString TrackListView::CellText(const Track& track, int field) const
{
    switch (field) {
        case kFieldNumber:
            return track.trackNumber > 0 ? BString() << track.trackNumber : BString();
        case kFieldName:
            return track.title.c_str();
        case kFieldTime:
            return FormatDuration(track.durationMs).c_str();
        case kFieldArtist:
            return track.artist.c_str();
        case kFieldAlbum:
            return track.album.c_str();
        case kFieldQuality:
            // same wording as the now playing display
            return QualityLabel(track.lossless, track.bitrate).c_str();
        case kFieldYear:
            return track.year > 0 ? BString() << track.year : BString();
        default:
            return BString();
    }
}

void TrackListView::DrawHeader(BView* target, BRect bounds)
{
    // every row of the header view is painted: a row left out keeps what was on the screen
    // before (the grid's captions showed through under the header, #7)
    BRect header = bounds;
    FillVerticalGradient(target, header, theme::kHeaderTop, theme::kHeaderBottom);
    target->SetHighColor(theme::kHeaderBorder);
    target->StrokeLine(header.LeftBottom(), header.RightBottom());
    BFont font(be_bold_font);
    font.SetSize(11);
    target->SetFont(&font);
    float left = RowsLeft();
    if (fMode == kGrouped) {
        BRect cell(0, header.top, kArtColumnWidth - 1, header.bottom);
        target->SetHighColor(theme::kHeaderText);
        DrawTruncated(target, "Artwork", cell, B_ALIGN_CENTER, 0);
        target->SetHighColor(theme::kHeaderBorder);
        target->StrokeLine(BPoint(cell.right, header.top), BPoint(cell.right, header.bottom));
    }
    for (size_t i = 0; i < fColumns.size(); i++) {
        const Column& c = fColumns[i];
        BRect cell(left, header.top, left + c.width - 1, header.bottom);
        if (fSortField == c.field && fMode == kPlain) {
            FillVerticalGradient(target, cell, theme::kHeaderSortedTop, theme::kHeaderSortedBottom);
            target->SetHighColor(theme::kHeaderText);
            float cx = cell.right - 8;
            float cy = (cell.top + cell.bottom) / 2;
            BPoint tri[3];
            if (fSortAscending) {
                tri[0] = BPoint(cx - 4, cy + 2); tri[1] = BPoint(cx + 4, cy + 2); tri[2] = BPoint(cx, cy - 2);
            } else {
                tri[0] = BPoint(cx - 4, cy - 2); tri[1] = BPoint(cx + 4, cy - 2); tri[2] = BPoint(cx, cy + 2);
            }
            target->FillPolygon(tri, 3);
        }
        target->SetHighColor(theme::kHeaderText);
        DrawTruncated(target, c.title, BRect(cell.left, cell.top, cell.right - (fSortField == c.field ? 14 : 0), cell.bottom), c.align, 4);
        target->SetHighColor(theme::kHeaderBorder);
        target->StrokeLine(BPoint(cell.right, header.top + 2), BPoint(cell.right, header.bottom - 1));
        left += c.width;
    }
}

void TrackListView::DrawRow(int row, BRect rect, bool selected, bool active)
{
    Library& library = App()->GetLibrary();
    const Track* track = library.TrackById(fTracks[row]);
    if (selected) {
        if (active)
            FillVerticalGradient(this, rect, theme::kSelectionTop, theme::kSelectionBottom);
        else
            FillVerticalGradient(this, rect, theme::kSelectionInactiveTop, theme::kSelectionInactiveBottom);
    } else {
        SetHighColor(row % 2 ? theme::kListStripe : theme::kListBackground);
        FillRect(rect);
    }
    if (!track)
        return;
    BFont font(be_plain_font);
    font.SetSize(12);
    SetFont(&font);
    rgb_color text = selected ? theme::kSelectedText : theme::kListText;
    float left = rect.left;
    bool isCurrent = track->id == fNowPlaying && fNowState != kStopped;
    for (const Column& c : fColumns) {
        BRect cell(left, rect.top, left + c.width - 1, rect.bottom);
        left += c.width;
        if (c.field == kFieldIndicator) {
            if (isCurrent) {
                // the playing row gets a small speaker, as a Font Awesome glyph
                icons::Draw(this, icons::kVolume, cell.InsetByCopy(1, 2), cell.Height() - 4,
                    selected ? theme::kSelectedText : Rgb(60, 60, 60));
            }
            continue;
        }
        BString value = CellText(*track, c.field);
        SetHighColor(text);
        DrawTruncated(this, value.String(), cell, c.align, 4);
    }
}

void TrackListView::DrawGroup(const Group& group, BRect rect)
{
    BRect cell(0, group.top, kArtColumnWidth - 1, group.top + group.height - 1);
    SetHighColor(theme::kListBackground);
    FillRect(cell);
    BRect art((kArtColumnWidth - kArtSize) / 2, group.top + 10, (kArtColumnWidth - kArtSize) / 2 + kArtSize - 1, group.top + 10 + kArtSize - 1);
    ArtRequest request = App()->Art().RequestFor(group.artKey, group.artist, group.name, group.localHint, false);
    BBitmap* bitmap = App()->Art().Get(group.artKey, (int)kArtSize, &request);
    // the cover lies on the page like in the grid: a soft shadow, not a grey block
    DrawArtworkOnPage(this, bitmap, art);
    BFont bold(be_bold_font);
    bold.SetSize(12);
    SetFont(&bold);
    SetHighColor(theme::kListText);
    BRect title(cell.left + 6, art.bottom + 9, cell.right - 6, art.bottom + 25); // below the shadow
    DrawTruncated(this, group.name.c_str(), title, B_ALIGN_CENTER, 0);
    BFont plain(be_plain_font);
    plain.SetSize(11);
    SetFont(&plain);
    SetHighColor(theme::kListSecondaryText);
    BString artistLine(group.artist.c_str());
    if (group.year > 0)
        artistLine << "  (" << group.year << ")";
    DrawTruncated(this, artistLine.String(), BRect(cell.left + 6, title.bottom + 1, cell.right - 6, title.bottom + 16), B_ALIGN_CENTER, 0);
    // the empty area under short groups continues the striping of the rows
    float rowsBottom = group.top + group.rowCount * theme::kRowHeight;
    if (rowsBottom < group.top + group.height) {
        int row = group.firstRow + group.rowCount;
        for (float y = rowsBottom; y < group.top + group.height; y += theme::kRowHeight, row++) {
            BRect stripe(kArtColumnWidth, y, rect.right, std::min(y + theme::kRowHeight - 1, group.top + group.height - 1));
            SetHighColor(row % 2 ? theme::kListStripe : theme::kListBackground);
            FillRect(stripe);
        }
    }
    SetHighColor(theme::kListGrid);
    StrokeLine(BPoint(0, group.top + group.height - 1), BPoint(rect.right, group.top + group.height - 1));
    StrokeLine(BPoint(kArtColumnWidth - 1, group.top), BPoint(kArtColumnWidth - 1, group.top + group.height - 1));
}

void TrackListView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    bool active = Window() && Window()->IsActive() && IsFocus();
    SetHighColor(theme::kListBackground);
    FillRect(updateRect);
    if (fTracks.empty()) {
        BFont font(be_plain_font);
        font.SetSize(14);
        SetFont(&font);
        SetHighColor(theme::kListSecondaryText);
        DrawTruncated(this, fEmptyText.String(), BRect(bounds.left, bounds.top + 24, bounds.right, bounds.top + 54), B_ALIGN_CENTER, 0);
        return;
    }
    Library::Locker locker(App()->GetLibrary());
    if (fMode == kGrouped) {
        for (const Group& group : fGroups) {
            BRect groupRect(0, group.top, bounds.right, group.top + group.height - 1);
            if (!groupRect.Intersects(updateRect))
                continue;
            DrawGroup(group, bounds);
            for (int row = group.firstRow; row < group.firstRow + group.rowCount; row++) {
                BRect rect = RowRect(row);
                if (rect.Intersects(updateRect))
                    DrawRow(row, rect, fSelection.count(row) != 0, active);
            }
        }
    } else {
        int first = std::max(0, (int)(updateRect.top / theme::kRowHeight));
        int last = std::min((int)fTracks.size() - 1, (int)(updateRect.bottom / theme::kRowHeight) + 1);
        for (int row = first; row <= last; row++)
            DrawRow(row, RowRect(row), fSelection.count(row) != 0, active);
    }
    if (fDropIndex >= 0) {
        float y = fDropIndex < (int)fTracks.size() ? RowRect(fDropIndex).top : RowRect((int)fTracks.size() - 1).bottom + 1;
        SetHighColor(theme::kSelectionBottom);
        SetPenSize(2);
        StrokeLine(BPoint(RowsLeft(), y), BPoint(bounds.right, y));
        SetPenSize(1);
    }
}

void TrackListView::SortTracks()
{
    if (fSortField < 0)
        return;
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    int field = fSortField;
    bool ascending = fSortAscending;
    std::stable_sort(fTracks.begin(), fTracks.end(), [&](int64_t a, int64_t b) {
        const Track* ta = library.TrackById(a);
        const Track* tb = library.TrackById(b);
        if (!ta || !tb)
            return ta != nullptr;
        int cmp = 0;
        switch (field) {
            case kFieldName: cmp = ToLower(ta->title).compare(ToLower(tb->title)); break;
            case kFieldTime: cmp = ta->durationMs < tb->durationMs ? -1 : (ta->durationMs > tb->durationMs ? 1 : 0); break;
            case kFieldArtist: cmp = SortKeyFor(ta->artist).compare(SortKeyFor(tb->artist)); break;
            case kFieldAlbum: cmp = SortKeyFor(ta->album).compare(SortKeyFor(tb->album)); if (cmp == 0) cmp = ta->trackNumber - tb->trackNumber; break;
            case kFieldQuality: {
                // lossless above any lossy rate, then by bit rate
                int qa = (ta->lossless ? 1000000 : 0) + ta->bitrate;
                int qb = (tb->lossless ? 1000000 : 0) + tb->bitrate;
                cmp = qa < qb ? -1 : (qa > qb ? 1 : 0);
                break;
            }
            case kFieldYear: cmp = ta->year - tb->year; break;
            case kFieldNumber: cmp = ta->trackNumber - tb->trackNumber; break;
            default: break;
        }
        return ascending ? cmp < 0 : cmp > 0;
    });
}

void TrackListView::PlayRow(int row)
{
    if (row < 0 || row >= (int)fTracks.size())
        return;
    BMessage message(kMsgPlayTracks);
    for (int64_t id : fTracks)
        message.AddInt64("tracks", id);
    message.AddInt32("index", row);
    Window()->PostMessage(&message);
}

void TrackListView::PostSelectionChanged()
{
    Window()->PostMessage(kMsgSelectionChanged);
}

// ---- TrackHeaderView --------------------------------------------------------

TrackHeaderView::TrackHeaderView(TrackListView* list, const char* name)
    : BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
    , fList(list)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    // a layout size is a frame height: kHeaderHeight pixel rows need kHeaderHeight - 1
    SetExplicitMinSize(BSize(160, theme::kHeaderHeight - 1));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, theme::kHeaderHeight - 1));
}

TrackHeaderView::~TrackHeaderView()
{
    if (fList != nullptr && fList->fHeaderView == this)
        fList->fHeaderView = nullptr;
}

void TrackHeaderView::Draw(BRect updateRect)
{
    if (fList != nullptr)
        fList->DrawHeader(this, Bounds());
}

void TrackHeaderView::MouseDown(BPoint where)
{
    if (fList != nullptr)
        fList->HeaderMouseDown(where, this);
}

void TrackHeaderView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    if (fList != nullptr)
        fList->HeaderMouseMoved(where);
}

void TrackHeaderView::MouseUp(BPoint where)
{
    if (fList != nullptr)
        fList->HeaderMouseUp();
}

void TrackListView::HeaderMouseDown(BPoint where, BView* target)
{
    // sort (plain mode, not playlists) or start a column resize
    float left = 0;
    int column = ColumnAt(where.x, &left);
    if (column < 0)
        return;
    float right = left + fColumns[column].width;
    if (right - where.x < 5 && column + 1 < (int)fColumns.size() && !fColumns[column].flexible) {
        fResizeColumn = column;
        fResizeStartX = where.x;
        fResizeStartWidth = fColumns[column].width;
        target->SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
        return;
    }
    if (fMode == kPlain && fPlaylistId == 0 && fColumns[column].field != kFieldIndicator) {
        int field = fColumns[column].field;
        if (fSortField == field)
            fSortAscending = !fSortAscending;
        else {
            fSortField = field;
            fSortAscending = true;
        }
        std::vector<int64_t> selected = SelectedTracks();
        SortTracks();
        fSelection.clear();
        for (size_t i = 0; i < fTracks.size(); i++)
            if (std::find(selected.begin(), selected.end(), fTracks[i]) != selected.end())
                fSelection.insert((int)i);
        Invalidate();
        InvalidateHeader();
    }
}

void TrackListView::HeaderMouseMoved(BPoint where)
{
    if (fResizeColumn < 0)
        return;
    fColumns[fResizeColumn].width = std::max(30.0f, fResizeStartWidth + (where.x - fResizeStartX));
    LayoutColumns();
    Invalidate();
    InvalidateHeader();
}

void TrackListView::HeaderMouseUp()
{
    fResizeColumn = -1;
}

void TrackListView::InvalidateHeader()
{
    if (fHeaderView)
        fHeaderView->Invalidate();
}

TrackHeaderView* TrackListView::HeaderView()
{
    if (!fHeaderView)
        fHeaderView = new TrackHeaderView(this);
    return fHeaderView;
}

void TrackListView::MouseDown(BPoint where)
{
    MakeFocus(true);
    int32 buttons = 0, clicks = 1, modifiers = 0;
    BMessage* current = Window()->CurrentMessage();
    current->FindInt32("buttons", &buttons);
    current->FindInt32("clicks", &clicks);
    current->FindInt32("modifiers", &modifiers);
    int row = RowAt(where);
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        if (row >= 0 && !fSelection.count(row)) {
            fSelection.clear();
            fSelection.insert(row);
            fAnchor = fFocusRow = row;
            Invalidate();
            PostSelectionChanged();
        }
        if (row < 0 && fMode == kGrouped) {
            // clicking the artwork column selects the album's rows
            int g = GroupAt(where);
            if (g >= 0) {
                fSelection.clear();
                for (int r = fGroups[g].firstRow; r < fGroups[g].firstRow + fGroups[g].rowCount; r++)
                    fSelection.insert(r);
                fAnchor = fFocusRow = fGroups[g].firstRow;
                Invalidate();
                PostSelectionChanged();
                row = fGroups[g].firstRow;
            }
        }
        ShowContextMenu(row, where);
        return;
    }
    if (row < 0) {
        if (fMode == kGrouped && where.x < RowsLeft()) {
            int g = GroupAt(where);
            if (g >= 0) {
                fSelection.clear();
                for (int r = fGroups[g].firstRow; r < fGroups[g].firstRow + fGroups[g].rowCount; r++)
                    fSelection.insert(r);
                fAnchor = fFocusRow = fGroups[g].firstRow;
                if (clicks >= 2)
                    PlayRow(fGroups[g].firstRow);
                Invalidate();
                PostSelectionChanged();
                fClickRow = fGroups[g].firstRow;
                fClickPoint = where;
                fMaybeDrag = true;
                SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
                return;
            }
        }
        fSelection.clear();
        Invalidate();
        PostSelectionChanged();
        return;
    }
    if (clicks >= 2 && fSelection.count(row)) {
        PlayRow(row);
        return;
    }
    bool toggle = (modifiers & (B_COMMAND_KEY | B_CONTROL_KEY)) != 0;
    bool extend = (modifiers & B_SHIFT_KEY) != 0;
    if (toggle) {
        if (fSelection.count(row))
            fSelection.erase(row);
        else
            fSelection.insert(row);
        fAnchor = row;
    } else if (extend && fAnchor >= 0) {
        fSelection.clear();
        for (int r = std::min(fAnchor, row); r <= std::max(fAnchor, row); r++)
            fSelection.insert(r);
    } else if (!fSelection.count(row)) {
        fSelection.clear();
        fSelection.insert(row);
        fAnchor = row;
    }
    fFocusRow = row;
    fClickRow = row;
    fClickPoint = where;
    fMaybeDrag = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
    Invalidate();
    PostSelectionChanged();
}

void TrackListView::MouseUp(BPoint where)
{
    if (fResizeColumn >= 0) {
        fResizeColumn = -1;
        return;
    }
    if (fMaybeDrag && fClickRow >= 0) {
        // a plain click on an already selected row (without drag) selects just that row
        int32 modifiers = 0;
        Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
        if (!(modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_SHIFT_KEY)) && fSelection.size() > 1
            && RowAt(where) == fClickRow && where.x >= RowsLeft()) {
            fSelection.clear();
            fSelection.insert(fClickRow);
            Invalidate();
            PostSelectionChanged();
        }
    }
    fMaybeDrag = false;
    fClickRow = -1;
}

void TrackListView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    if (fResizeColumn >= 0) {
        float width = std::max(30.0f, fResizeStartWidth + (where.x - fResizeStartX));
        fColumns[fResizeColumn].width = width;
        LayoutColumns();
        Invalidate();
        return;
    }
    if (drag) {
        int previous = fDropIndex;
        fDropIndex = -1;
        if (drag->what == kMsgTrackDrag && CanReorder() && (transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW))
            fDropIndex = DropIndexAt(where);
        if (previous != fDropIndex)
            Invalidate();
        return;
    }
    if (fMaybeDrag && fClickRow >= 0) {
        int32 buttons = 0;
        Window()->CurrentMessage()->FindInt32("buttons", &buttons);
        if (buttons && (fabs(where.x - fClickPoint.x) > 4 || fabs(where.y - fClickPoint.y) > 4)) {
            fMaybeDrag = false;
            StartDrag(where);
        }
    }
}

int TrackListView::DropIndexAt(BPoint where) const
{
    if (fTracks.empty())
        return 0;
    float y = where.y;
    int index = (int)((y + theme::kRowHeight / 2) / theme::kRowHeight);
    return std::max(0, std::min((int)fTracks.size(), index));
}

void TrackListView::StartDrag(BPoint where)
{
    std::vector<int64_t> ids = SelectedTracks();
    if (ids.empty())
        return;
    BMessage drag(kMsgTrackDrag);
    for (int64_t id : ids)
        drag.AddInt64("tracks", id);
    for (int row : fSelection)
        drag.AddInt32("positions", PositionOfRow(row));
    drag.AddInt64("playlist", fPlaylistId);
    BRect rect = RowRect(fClickRow);
    rect.right = rect.left + 200;
    BString label;
    label << (int)ids.size() << (ids.size() == 1 ? " song" : " songs");
    DragMessage(&drag, rect, this);
}

void TrackListView::MessageReceived(BMessage* message)
{
    if (message->WasDropped() && message->what == kMsgTrackDrag) {
        BPoint where = ConvertFromScreen(message->DropPoint());
        // an exact position only exists in the unfiltered list view; otherwise songs are appended
        int index = CanReorder() ? DropIndexAt(where) : -1;
        fDropIndex = -1;
        Invalidate();
        if (fPlaylistId == 0)
            return;
        int64 source = 0;
        message->FindInt64("playlist", &source);
        if (source == fPlaylistId) {
            if (!CanReorder())
                return; // reordering happens in the list view
            BMessage move(kMsgMovePlaylistTracks);
            move.AddInt64("playlist", fPlaylistId);
            int32 position;
            for (int32 i = 0; message->FindInt32("positions", i, &position) == B_OK; i++)
                move.AddInt32("positions", position);
            move.AddInt32("target", index);
            Window()->PostMessage(&move);
        } else {
            BMessage add(kMsgAddToPlaylist);
            add.AddInt64("playlist", fPlaylistId);
            add.AddInt32("position", index);
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                add.AddInt64("tracks", id);
            Window()->PostMessage(&add);
        }
        return;
    }
    BView::MessageReceived(message);
}

void TrackListView::ScrollToRow(int row)
{
    BRect rect = RowRect(row);
    BRect bounds = Bounds();
    if (rect.top < bounds.top)
        ScrollTo(0, std::max(0.0f, rect.top));
    else if (rect.bottom > bounds.bottom)
        ScrollTo(0, rect.bottom - bounds.Height());
}

void TrackListView::KeyDown(const char* bytes, int32 numBytes)
{
    if (numBytes < 1 || fTracks.empty()) {
        BView::KeyDown(bytes, numBytes);
        return;
    }
    int32 modifiers = 0;
    Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
    bool extend = (modifiers & B_SHIFT_KEY) != 0;
    switch (bytes[0]) {
        case B_DOWN_ARROW:
        case B_UP_ARROW: {
            int row = fFocusRow < 0 ? 0 : fFocusRow + (bytes[0] == B_DOWN_ARROW ? 1 : -1);
            row = std::max(0, std::min((int)fTracks.size() - 1, row));
            if (!extend) {
                fSelection.clear();
                fAnchor = row;
            } else if (fAnchor < 0)
                fAnchor = row;
            if (extend) {
                fSelection.clear();
                for (int r = std::min(fAnchor, row); r <= std::max(fAnchor, row); r++)
                    fSelection.insert(r);
            } else
                fSelection.insert(row);
            fFocusRow = row;
            ScrollToRow(row);
            Invalidate();
            PostSelectionChanged();
            break;
        }
        case B_HOME:
        case B_END: {
            int row = bytes[0] == B_HOME ? 0 : (int)fTracks.size() - 1;
            fSelection.clear();
            fSelection.insert(row);
            fFocusRow = fAnchor = row;
            ScrollToRow(row);
            Invalidate();
            PostSelectionChanged();
            break;
        }
        case B_PAGE_DOWN:
        case B_PAGE_UP: {
            int rows = (int)(Bounds().Height() / theme::kRowHeight) - 1;
            int row = fFocusRow < 0 ? 0 : fFocusRow + (bytes[0] == B_PAGE_DOWN ? rows : -rows);
            row = std::max(0, std::min((int)fTracks.size() - 1, row));
            fSelection.clear();
            fSelection.insert(row);
            fFocusRow = fAnchor = row;
            ScrollToRow(row);
            Invalidate();
            PostSelectionChanged();
            break;
        }
        case B_ENTER:
            PlayRow(fFocusRow >= 0 ? fFocusRow : (fSelection.empty() ? 0 : *fSelection.begin()));
            break;
        case B_DELETE:
        case B_BACKSPACE:
            if (fPlaylistId != 0 && !fSelection.empty()) {
                BMessage message(kMsgRemoveFromPlaylist);
                message.AddInt64("playlist", fPlaylistId);
                for (int row : fSelection)
                    message.AddInt32("positions", PositionOfRow(row));
                Window()->PostMessage(&message);
            }
            break;
        case ' ':
            Window()->PostMessage(kMsgPlayPause);
            break;
        default:
            BView::KeyDown(bytes, numBytes);
    }
}

void TrackListView::ShowContextMenu(int row, BPoint where)
{
    std::vector<int64_t> ids = SelectedTracks();
    BPopUpMenu* menu = new BPopUpMenu("track-menu", false, false);
    auto withTracks = [&](uint32 what) {
        BMessage* message = new BMessage(what);
        for (int64_t id : ids)
            message->AddInt64("tracks", id);
        return message;
    };
    if (row >= 0) {
        BMessage* play = new BMessage(kMsgPlayTracks);
        for (int64_t id : fTracks)
            play->AddInt64("tracks", id);
        play->AddInt32("index", row);
        menu->AddItem(new BMenuItem("Play", play));
        BMessage* playSelected = withTracks(kMsgPlayTracks);
        playSelected->AddInt32("index", 0);
        if (ids.size() > 1)
            menu->AddItem(new BMenuItem("Play Selected Songs", playSelected));
        else
            delete playSelected;
        BMessage* next = withTracks(kMsgPlayTracks);
        next->AddBool("next", true);
        menu->AddItem(new BMenuItem("Play Next", next));
        BMessage* last = withTracks(kMsgPlayTracks);
        last->AddBool("append", true);
        menu->AddItem(new BMenuItem("Add to Up Next", last));
        MiniDiscState miniDisc = App()->MiniDisc().State();
        if (miniDisc.connected) {
            BMessage* write = withTracks(kMsgWriteToMiniDisc);
            write->AddString("kind", "songs");
            write->AddInt64("playlist", fPlaylistId);
            BMenuItem* item = new BMenuItem(ids.size() == 1 ? "Write Song to MiniDisc…" : "Write Songs to MiniDisc…", write);
            item->SetEnabled(!miniDisc.busy);
            menu->AddItem(item);
        }
        menu->AddSeparatorItem();
    }
    // playlists submenu
    BMenu* playlists = new BMenu("Add to Playlist");
    BMessage* fresh = withTracks(kMsgNewPlaylist);
    playlists->AddItem(new BMenuItem("New Playlist…", fresh));
    playlists->AddSeparatorItem();
    {
        Library& library = App()->GetLibrary();
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
    if (fPlaylistId != 0 && row >= 0) {
        BMessage* remove = new BMessage(kMsgRemoveFromPlaylist);
        remove->AddInt64("playlist", fPlaylistId);
        for (int r : fSelection)
            remove->AddInt32("positions", PositionOfRow(r));
        menu->AddItem(new BMenuItem("Remove from Playlist", remove));
    }
    if (row >= 0 && ids.size() == 1) {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        const Track* track = library.TrackById(ids.front());
        // songs that only live in a playlist have no library album or artist to jump to
        if (track && track->albumId != 0) {
            menu->AddSeparatorItem();
            BMessage* album = new BMessage(kMsgShowAlbum);
            album->AddInt64("album", track->albumId);
            menu->AddItem(new BMenuItem("Show Album", album));
            BMessage* artist = new BMessage(kMsgShowArtist);
            artist->AddInt64("artist", library.ArtistIdFor(track->groupingArtist()));
            menu->AddItem(new BMenuItem("Show Artist", artist));
        }
    }
    menu->SetTargetForItems(Window());
    menu->SetAsyncAutoDestruct(true);
    menu->Go(ConvertToScreen(where), true, true, true);
}

} // namespace amp
