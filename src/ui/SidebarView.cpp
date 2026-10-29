#include "SidebarView.h"
#include "Theme.h"
#include "Icons.h"
#include "player/Messages.h"
#include <Font.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Window.h>
#include <algorithm>

namespace amp {

namespace {
const float kItemHeight = 20.0f;
const float kHeaderHeight = 22.0f;
}

SidebarView::SidebarView()
    : BView("sidebar", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_SCROLL_VIEW_AWARE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(150, 100));
    Rebuild();
}

void SidebarView::SetPlaylists(const std::vector<SidebarPlaylist>& playlists)
{
    fPlaylists = playlists;
    Rebuild();
    Invalidate();
}

void SidebarView::SetMiniDisc(bool connected, const BString& label, bool busy, float fraction, bool locked)
{
    fMiniDiscLocked = locked;
    bool rebuild = connected != fMiniDiscConnected || label != fMiniDiscLabel;
    fMiniDiscConnected = connected;
    fMiniDiscLabel = label;
    fMiniDiscBusy = busy;
    fMiniDiscFraction = fraction;
    if (rebuild)
        Rebuild();
    Invalidate();
}

void SidebarView::Select(const char* source, int64_t playlistId)
{
    fSelectedSource = source;
    fSelectedPlaylist = playlistId;
    Invalidate();
}

void SidebarView::Rebuild()
{
    fItems.clear();
    float y = 6;
    auto add = [&](const char* source, const char* label, bool header, int64_t playlistId = 0) {
        Item item;
        item.source = source;
        item.label = label;
        item.header = header;
        item.playlistId = playlistId;
        float h = header ? kHeaderHeight : kItemHeight;
        item.rect = BRect(0, y, Bounds().Width(), y + h - 1);
        y += h;
        fItems.push_back(item);
    };
    add("", "LIBRARY", true);
    add("music", "Music", false);
    add("artists", "Artists", false);
    add("albums", "Albums", false);
    if (fMiniDiscConnected) {
        y += 8;
        add("", "DEVICES", true);
        add("minidisc", fMiniDiscLabel.String(), false);
    }
    y += 8;
    add("", "PLAYLISTS", true);
    for (const SidebarPlaylist& p : fPlaylists)
        add("playlist", p.name.String(), false, p.id);
    fContentHeight = y + 10;
    UpdateScrollBar();
}

void SidebarView::UpdateScrollBar()
{
    if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
        float visible = Bounds().Height();
        float max = std::max(0.0f, fContentHeight - visible);
        bar->SetRange(0, max);
        bar->SetProportion(fContentHeight > 0 ? visible / fContentHeight : 1);
        bar->SetSteps(kItemHeight, visible);
    }
}

void SidebarView::FrameResized(float width, float height)
{
    for (Item& item : fItems)
        item.rect.right = width;
    UpdateScrollBar();
    Invalidate();
}

void SidebarView::AttachedToWindow()
{
    BView::AttachedToWindow();
    UpdateScrollBar();
}

int SidebarView::ItemAt(BPoint where) const
{
    for (size_t i = 0; i < fItems.size(); i++)
        if (!fItems[i].header && fItems[i].rect.Contains(where))
            return (int)i;
    return -1;
}

void SidebarView::DrawIcon(const Item& item, BRect rect, bool selected)
{
    rgb_color color = selected ? Rgb(255, 255, 255) : Rgb(70, 90, 120);
    float size = rect.Height() * 0.9f;
    if (item.source == "music") {
        icons::Draw(this, icons::kMusic, rect, size, color);
    } else if (item.source == "artists") {
        icons::Draw(this, icons::kUser, rect, size, color);
    } else if (item.source == "albums") {
        icons::Draw(this, icons::kAlbum, rect, size, color);
    } else if (item.source == "minidisc") {
        DrawMiniDisc(this, rect.InsetByCopy(0.5f, 0.5f), color,
            selected ? theme::kSidebarSelectionBottom : theme::kSidebarBackground);
    } else {
        icons::Draw(this, icons::kList, rect, size, color);
    }
}

void SidebarView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    SetHighColor(theme::kSidebarBackground);
    FillRect(bounds);
    SetHighColor(theme::kSidebarBorder);
    StrokeLine(BPoint(bounds.right, bounds.top), BPoint(bounds.right, bounds.bottom));
    bool active = Window() && Window()->IsActive();
    BFont header(be_bold_font);
    header.SetSize(10);
    BFont plain(be_plain_font);
    plain.SetSize(12);
    BFont bold(be_bold_font);
    bold.SetSize(12);
    for (size_t i = 0; i < fItems.size(); i++) {
        const Item& item = fItems[i];
        BRect rect = item.rect;
        rect.right = bounds.right - 1;
        if (!rect.Intersects(updateRect))
            continue;
        if (item.header) {
            SetFont(&header);
            SetHighColor(theme::kSidebarHeader);
            DrawTruncated(this, item.label.String(), BRect(rect.left + 8, rect.top + 2, rect.right, rect.bottom), B_ALIGN_LEFT, 0);
            continue;
        }
        bool selected = item.source == fSelectedSource && (item.source != "playlist" || item.playlistId == fSelectedPlaylist);
        if (selected) {
            if (active)
                FillVerticalGradient(this, rect, theme::kSidebarSelectionTop, theme::kSidebarSelectionBottom);
            else
                FillVerticalGradient(this, rect, theme::kSidebarSelectionInactiveTop, theme::kSidebarSelectionInactiveBottom);
        } else if ((int)i == fDropIndex) {
            SetHighColor(255, 255, 255, 140);
            SetDrawingMode(B_OP_ALPHA);
            FillRect(rect);
            SetDrawingMode(B_OP_COPY);
            SetHighColor(theme::kSidebarSelectionBottom);
            StrokeRect(rect);
        }
        BRect icon(rect.left + 12, rect.top + 3, rect.left + 26, rect.bottom - 3);
        DrawIcon(item, icon, selected);
        SetFont(selected ? &bold : &plain);
        SetHighColor(selected ? theme::kSelectedText : theme::kSidebarText);
        float right = rect.right - 4;
        if (item.source == "minidisc" && fMiniDiscBusy) {
            BRect pie(rect.right - 20, rect.top + 3, rect.right - 7, rect.top + 16);
            DrawProgressPie(this, pie, std::max(0.0f, fMiniDiscFraction),
                selected ? theme::kSelectedText : theme::kSidebarSelectionBottom);
            right = pie.left - 4;
        } else if (item.source == "minidisc" && fMiniDiscLocked) {
            BRect lock(rect.right - 18, rect.top + 3, rect.right - 6, rect.top + 16);
            icons::Draw(this, icons::kLock, lock, 10, selected ? theme::kSelectedText : theme::kSidebarHeader);
            right = lock.left - 4;
        }
        SetHighColor(selected ? theme::kSelectedText : theme::kSidebarText);
        DrawTruncated(this, item.label.String(), BRect(rect.left + 32, rect.top, right, rect.bottom), B_ALIGN_LEFT, 0);
    }
}

void SidebarView::MouseDown(BPoint where)
{
    MakeFocus(true);
    int index = ItemAt(where);
    if (index < 0)
        return;
    const Item& item = fItems[index];
    int32 buttons = 0;
    Window()->CurrentMessage()->FindInt32("buttons", &buttons);
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        ShowContextMenu(index, where);
        return;
    }
    fSelectedSource = item.source;
    fSelectedPlaylist = item.playlistId;
    Invalidate();
    BMessage message(kMsgSourceSelected);
    message.AddString("source", item.source.String());
    message.AddInt64("playlist", item.playlistId);
    Window()->PostMessage(&message);
}

void SidebarView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    int previous = fDropIndex;
    fDropIndex = -1;
    if (drag && drag->what == kMsgTrackDrag && (transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW)) {
        int index = ItemAt(where);
        if (index >= 0 && (fItems[index].source == "playlist"
                || (fItems[index].source == "minidisc" && !fMiniDiscBusy)))
            fDropIndex = index;
    }
    if (previous != fDropIndex)
        Invalidate();
}

void SidebarView::MouseUp(BPoint where)
{
}

void SidebarView::MessageReceived(BMessage* message)
{
    if (message->WasDropped() && message->what == kMsgTrackDrag) {
        BPoint where = ConvertFromScreen(message->DropPoint());
        int index = ItemAt(where);
        fDropIndex = -1;
        Invalidate();
        if (index >= 0 && fItems[index].source == "minidisc" && !fMiniDiscBusy) {
            BMessage write(kMsgWriteToMiniDisc);
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                write.AddInt64("tracks", id);
            int64 playlist = 0;
            message->FindInt64("playlist", &playlist);
            write.AddInt64("playlist", playlist);
            write.AddString("kind", "songs");
            Window()->PostMessage(&write);
            return;
        }
        if (index >= 0 && fItems[index].source == "playlist") {
            BMessage add(kMsgAddToPlaylist);
            add.AddInt64("playlist", fItems[index].playlistId);
            int64 id;
            for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
                add.AddInt64("tracks", id);
            Window()->PostMessage(&add);
        }
        return;
    }
    BView::MessageReceived(message);
}

void SidebarView::ShowContextMenu(int index, BPoint where)
{
    const Item& item = fItems[index];
    BPopUpMenu* menu = new BPopUpMenu("sidebar-menu", false, false);
    if (item.source == "minidisc") {
        BMenuItem* erase = new BMenuItem("Erase MiniDisc…", new BMessage(kMsgMDErase));
        erase->SetEnabled(!fMiniDiscBusy);
        menu->AddItem(erase);
        BMenuItem* refresh = new BMenuItem("Refresh", new BMessage(kMsgMDRefresh));
        refresh->SetEnabled(!fMiniDiscBusy);
        menu->AddItem(refresh);
        menu->SetTargetForItems(Window());
        menu->SetAsyncAutoDestruct(true);
        menu->Go(ConvertToScreen(where), true, true, true);
        return;
    }
    menu->AddItem(new BMenuItem("New Playlist…", new BMessage(kMsgNewPlaylist)));
    if (item.source == "playlist") {
        menu->AddSeparatorItem();
        BMessage* play = new BMessage(kMsgSourceSelected);
        play->AddString("source", "playlist");
        play->AddInt64("playlist", item.playlistId);
        play->AddBool("play", true);
        menu->AddItem(new BMenuItem("Play", play));
        if (fMiniDiscConnected) {
            BMessage* write = new BMessage(kMsgWriteToMiniDisc);
            write->AddInt64("playlist", item.playlistId);
            write->AddString("kind", "playlist");
            BMenuItem* writeItem = new BMenuItem("Write to MiniDisc…", write);
            writeItem->SetEnabled(!fMiniDiscBusy);
            menu->AddItem(writeItem);
        }
        BMessage* rename = new BMessage(kMsgRenamePlaylist);
        rename->AddInt64("playlist", item.playlistId);
        menu->AddItem(new BMenuItem("Rename…", rename));
        BMessage* remove = new BMessage(kMsgDeletePlaylist);
        remove->AddInt64("playlist", item.playlistId);
        menu->AddItem(new BMenuItem("Delete Playlist", remove));
    }
    menu->SetTargetForItems(Window());
    menu->SetAsyncAutoDestruct(true);
    menu->Go(ConvertToScreen(where), true, true, true);
}

} // namespace amp
