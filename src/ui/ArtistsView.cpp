#include "ArtistsView.h"
#include "App.h"
#include "Theme.h"
#include "Icons.h"
#include <Bitmap.h>
#include <Cursor.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <Window.h>

namespace amp {

namespace {
const float kArtistRowHeight = 40.0f;
const float kHeaderHeight = 100.0f;    // the picture, its shadow and a margin
const float kPictureSize = 80.0f;
const rgb_color kLinkColor = {30, 84, 176, 255};
}

// ---- ArtistListView ---------------------------------------------------------

ArtistListView::ArtistListView()
    : BView("artistlist", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(160, 100));
}

void ArtistListView::SetArtists(const std::vector<int64_t>& ids)
{
    fArtists = ids;
    UpdateScrollBar();
    Invalidate();
}

void ArtistListView::Select(int64_t artistId, bool notify)
{
    fSelected = artistId;
    for (size_t i = 0; i < fArtists.size(); i++)
        if (fArtists[i] == artistId)
            ScrollToIndex((int)i);
    Invalidate();
    if (notify && Window()) {
        BMessage message(kMsgArtistSelected);
        message.AddInt64("artist", artistId);
        Window()->PostMessage(&message, fOwner ? fOwner : Parent());
    }
}

void ArtistListView::UpdateScrollBar()
{
    BScrollBar* bar = ScrollBar(B_VERTICAL);
    if (!bar)
        return;
    float content = fArtists.size() * kArtistRowHeight;
    float visible = Bounds().Height();
    bar->SetRange(0, std::max(0.0f, content - visible));
    bar->SetProportion(content > 0 ? std::min(1.0f, visible / content) : 1);
    bar->SetSteps(kArtistRowHeight, visible - kArtistRowHeight);
}

void ArtistListView::FrameResized(float width, float height)
{
    UpdateScrollBar();
}

int ArtistListView::IndexAt(BPoint where) const
{
    int index = (int)(where.y / kArtistRowHeight);
    return (index >= 0 && index < (int)fArtists.size()) ? index : -1;
}

void ArtistListView::ScrollToIndex(int index)
{
    BRect bounds = Bounds();
    float top = index * kArtistRowHeight;
    if (top < bounds.top)
        ScrollTo(0, top);
    else if (top + kArtistRowHeight > bounds.bottom)
        ScrollTo(0, top + kArtistRowHeight - bounds.Height());
}

void ArtistListView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    SetHighColor(theme::kListBackground);
    FillRect(updateRect);
    bool active = Window() && Window()->IsActive() && IsFocus();
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    BFont bold(be_bold_font);
    bold.SetSize(12);
    BFont plain(be_plain_font);
    plain.SetSize(10);
    int first = std::max(0, (int)(updateRect.top / kArtistRowHeight));
    int last = std::min((int)fArtists.size() - 1, (int)(updateRect.bottom / kArtistRowHeight));
    for (int i = first; i <= last; i++) {
        const Artist* artist = library.ArtistById(fArtists[i]);
        if (!artist)
            continue;
        BRect row(0, i * kArtistRowHeight, bounds.right, (i + 1) * kArtistRowHeight - 1);
        bool selected = artist->id == fSelected;
        if (selected) {
            if (active)
                FillVerticalGradient(this, row, theme::kSelectionTop, theme::kSelectionBottom);
            else
                FillVerticalGradient(this, row, theme::kSelectionInactiveTop, theme::kSelectionInactiveBottom);
        } else {
            SetHighColor(i % 2 ? theme::kListStripe : theme::kListBackground);
            FillRect(row);
        }
        BRect art(row.left + 6, row.top + 4, row.left + 37, row.top + 35);
        ArtRequest request = App()->Art().RequestFor(artist->art, artist->name, "", "", true);
        BBitmap* bitmap = App()->Art().Get(artist->art, 64, &request);
        if (bitmap)
            DrawBitmapFitted(this, bitmap, art);
        else {
            FillVerticalGradient(this, art, theme::kArtPlaceholderTop, theme::kArtPlaceholderBottom);
            icons::DrawFitted(this, icons::kUser, art.InsetByCopy(art.Width() * 0.2f, art.Height() * 0.2f), theme::kArtPlaceholderNote);
        }
        SetHighColor(160, 160, 160);
        StrokeRect(art);
        SetFont(&bold);
        SetHighColor(selected ? theme::kSelectedText : theme::kListText);
        float right = row.right - 6;
        if (artist->isMA()) {
            DrawMABadge(this, BPoint(row.right - 30, row.top + 6), 11);
            right = row.right - 34;
        }
        SetFont(&bold);
        SetHighColor(selected ? theme::kSelectedText : theme::kListText); // the badge changed both
        DrawTruncated(this, artist->name.c_str(), BRect(art.right + 8, row.top + 3, right, row.top + 20), B_ALIGN_LEFT, 0);
        SetFont(&plain);
        SetHighColor(selected ? theme::kSelectedText : theme::kListSecondaryText);
        BString info;
        info << artist->albumCount << (artist->albumCount == 1 ? " album, " : " albums, ") << artist->trackCount
            << (artist->trackCount == 1 ? " song" : " songs");
        DrawTruncated(this, info.String(), BRect(art.right + 8, row.top + 20, right, row.top + 36), B_ALIGN_LEFT, 0);
    }
    SetHighColor(theme::kSidebarBorder);
    StrokeLine(BPoint(bounds.right, bounds.top), BPoint(bounds.right, bounds.bottom));
}

void ArtistListView::MouseDown(BPoint where)
{
    MakeFocus(true);
    int index = IndexAt(where);
    if (index < 0)
        return;
    int32 clicks = 1;
    Window()->CurrentMessage()->FindInt32("clicks", &clicks);
    Select(fArtists[index], true);
    if (clicks >= 2) {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        const Artist* artist = library.ArtistById(fArtists[index]);
        if (artist && !artist->trackIds.empty()) {
            BMessage message(kMsgPlayTracks);
            for (int64_t id : artist->trackIds)
                message.AddInt64("tracks", id);
            message.AddInt32("index", 0);
            Window()->PostMessage(&message);
        }
    }
}

void ArtistListView::KeyDown(const char* bytes, int32 numBytes)
{
    if (numBytes < 1 || fArtists.empty()) {
        BView::KeyDown(bytes, numBytes);
        return;
    }
    int current = -1;
    for (size_t i = 0; i < fArtists.size(); i++)
        if (fArtists[i] == fSelected)
            current = (int)i;
    int next = current;
    switch (bytes[0]) {
        case B_DOWN_ARROW: next = std::min((int)fArtists.size() - 1, current + 1); break;
        case B_UP_ARROW: next = std::max(0, current - 1); break;
        case B_HOME: next = 0; break;
        case B_END: next = (int)fArtists.size() - 1; break;
        case ' ': Window()->PostMessage(kMsgPlayPause); return;
        default: BView::KeyDown(bytes, numBytes); return;
    }
    if (next != current && next >= 0)
        Select(fArtists[next], true);
}

// ---- ArtistHeaderView -------------------------------------------------------

ArtistHeaderView::ArtistHeaderView()
    : BView("artistheader", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(200, kHeaderHeight));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, kHeaderHeight));
}

void ArtistHeaderView::SetArtist(int64_t artistId)
{
    if (artistId != fArtist)
        SetHot(false);
    fArtist = artistId;
    Invalidate();
}

void ArtistHeaderView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    FillVerticalGradient(this, bounds, Rgb(245, 245, 245), Rgb(226, 226, 226));
    SetHighColor(theme::kHeaderBorder);
    StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    const Artist* artist = library.ArtistById(fArtist);
    fArtRect = fNameRect = BRect();
    if (!artist) {
        BFont font(be_plain_font);
        font.SetSize(13);
        SetFont(&font);
        SetHighColor(theme::kListSecondaryText);
        DrawTruncated(this, "Select an artist", bounds, B_ALIGN_CENTER, 0);
        return;
    }
    // the picture lies on the header like the covers on the page: a soft shadow below it
    BRect art(14, 9, 14 + kPictureSize - 1, 9 + kPictureSize - 1);
    ArtRequest request = App()->Art().RequestFor(artist->art, artist->name, "", "", true);
    BBitmap* bitmap = App()->Art().Get(artist->art, 160, &request);
    fArtRect = DrawArtworkOnPage(this, bitmap, art, true, 7.0f, 3.0f);
    BFont big(be_bold_font);
    big.SetSize(18);
    SetFont(&big);
    float right = bounds.right - 12;
    float nameLeft = art.right + 16;
    BRect nameBox(nameLeft, 14, right, 40);
    // the name is a link: blue and underlined while the pointer is on it or on the picture
    SetHighColor(fHot ? kLinkColor : theme::kListText);
    DrawTruncated(this, artist->name.c_str(), nameBox, B_ALIGN_LEFT, 0);
    float nameWidth = std::min(StringWidth(artist->name.c_str()), nameBox.Width());
    font_height height;
    big.GetHeight(&height);
    float baseline = floorf((nameBox.top + nameBox.bottom + height.ascent - height.descent) / 2 + 0.5f);
    fNameRect = BRect(nameLeft, baseline - ceilf(height.ascent), nameLeft + nameWidth, baseline + ceilf(height.descent));
    if (fHot)
        StrokeLine(BPoint(nameLeft, baseline + 2), BPoint(nameLeft + nameWidth, baseline + 2));
    if (artist->isMA())
        DrawMABadge(this, BPoint(nameLeft + std::min(StringWidth(artist->name.c_str()) + 8, right - nameLeft - 26), 20), 13);
    BFont plain(be_plain_font);
    plain.SetSize(12);
    SetFont(&plain);
    SetHighColor(theme::kListSecondaryText);
    BString info;
    info << artist->albumCount << (artist->albumCount == 1 ? " album" : " albums") << ", " << artist->trackCount
        << (artist->trackCount == 1 ? " song" : " songs");
    int64_t total = 0;
    for (int64_t albumId : artist->albumIds)
        if (const Album* album = library.AlbumById(albumId))
            total += album->durationMs;
    if (total > 0)
        info << ", " << FormatDuration(total).c_str();
    DrawTruncated(this, info.String(), BRect(nameLeft, 45, right, 63), B_ALIGN_LEFT, 0);
    if (artist->isMA()) {
        SetHighColor(theme::kBadgeBackground);
        DrawTruncated(this, "Streamed from Music Assistant", BRect(nameLeft, 63, right, 81), B_ALIGN_LEFT, 0);
    }
}

bool ArtistHeaderView::OverLink(BPoint where) const
{
    return fArtist != 0 && (fArtRect.Contains(where) || fNameRect.InsetByCopy(-2, -2).Contains(where));
}

void ArtistHeaderView::SetHot(bool hot)
{
    if (hot == fHot)
        return;
    fHot = hot;
    static const BCursor link(B_CURSOR_ID_FOLLOW_LINK);
    if (hot) {
        SetViewCursor(&link);
        SetToolTip("Show this artist on MusicBrainz");
    } else {
        SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
        SetToolTip((const char*)nullptr);
    }
    Invalidate(fNameRect.InsetByCopy(-2, -4));
}

void ArtistHeaderView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    SetHot(drag == nullptr && transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW && OverLink(where));
}

void ArtistHeaderView::MouseDown(BPoint where)
{
    if (!OverLink(where) || !Window())
        return;
    BMessage message(kMsgOpenArtistPage);
    message.AddInt64("artist", fArtist);
    Window()->PostMessage(&message);
}

// ---- ArtistsView ------------------------------------------------------------

ArtistsView::ArtistsView()
    : BView("artists", B_WILL_DRAW)
{
    fList = new ArtistListView();
    fList->SetOwner(this);
    fHeader = new ArtistHeaderView();
    fTracks = new TrackListView("artist-tracks");
    fTracks->SetEmptyText("This artist has no songs");
    BScrollView* listScroll = new BScrollView("artist-scroll", fList, 0, false, true, B_NO_BORDER);
    BScrollView* trackScroll = new BScrollView("artist-track-scroll", fTracks, 0, false, true, B_NO_BORDER);
    // the track list's column header sits above its scroll view, like in the main window
    BView* trackPane = new BView("artist-track-pane", 0);
    BLayoutBuilder::Group<>(trackPane, B_VERTICAL, 0)
        .Add(fTracks->HeaderView())
        .Add(trackScroll);
    BView* right = new BView("artist-right", 0);
    BLayoutBuilder::Group<>(right, B_VERTICAL, 0)
        .Add(fHeader)
        .Add(trackPane);
    BSplitView* split = new BSplitView(B_HORIZONTAL, 0);
    split->AddChild(listScroll, 0.28f);
    split->AddChild(right, 0.72f);
    split->SetCollapsible(false);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(split);
}

void ArtistsView::SetArtists(const std::vector<int64_t>& ids)
{
    fList->SetArtists(ids);
    int64_t selected = fList->Selected();
    {
        // the index numbers artists afresh when it is rebuilt (a running scan does that every
        // few seconds): the selected id may name someone else now, so the artist is found
        // again by name (#9)
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        const Artist* artist = library.ArtistById(selected);
        if (!fSelectedName.empty() && (!artist || artist->name != fSelectedName)) {
            int64_t found = library.ArtistIdFor(fSelectedName);
            if (found != 0)
                selected = found;
        }
    }
    bool stillThere = false;
    for (int64_t id : ids)
        if (id == selected)
            stillThere = true;
    if (!stillThere)
        selected = ids.empty() ? 0 : ids.front();
    if (selected != fList->Selected())
        fList->Select(selected, false);
    if (!stillThere)
        RememberSelection();
    ReloadTracks();
}

void ArtistsView::SelectArtist(int64_t artistId)
{
    fList->Select(artistId, false);
    RememberSelection();
    ReloadTracks();
}

void ArtistsView::RememberSelection()
{
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    const Artist* artist = library.ArtistById(fList->Selected());
    fSelectedName = artist ? artist->name : std::string();
}

void ArtistsView::ReloadTracks()
{
    int64_t artistId = fList->Selected();
    fHeader->SetArtist(artistId);
    std::vector<int64_t> tracks;
    {
        Library& library = App()->GetLibrary();
        Library::Locker locker(library);
        if (const Artist* artist = library.ArtistById(artistId))
            tracks = artist->trackIds;
    }
    fTracks->SetTracks(tracks, TrackListView::kGrouped, 0);
}

void ArtistsView::MessageReceived(BMessage* message)
{
    if (message->what == kMsgArtistSelected) {
        RememberSelection();
        ReloadTracks();
        Window()->PostMessage(kMsgSelectionChanged);
        return;
    }
    BView::MessageReceived(message);
}

void ArtistsView::Refresh()
{
    fList->Invalidate();
    fHeader->Invalidate();
    fTracks->Invalidate();
}

} // namespace amp
