#include "MiniDiscView.h"
#include "Icons.h"
#include "Theme.h"
#include "core/Model.h"
#include "player/Messages.h"
#include <Button.h>
#include <LayoutBuilder.h>
#include <Region.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <Window.h>
#include <algorithm>

namespace amp {

namespace {

const float kSummaryHeight = 132;
const float kColumnHeaderHeight = 18;

struct PendingRow {
    BString title;
    int64_t durationMs = 0;
    int state = 0;          // 0 waiting, 1 converting, 2 writing, 3 done
    float fraction = 0;
};

// Column edges shared by the header strip and the rows.
struct Columns {
    float number, name, time, mode, right;
    explicit Columns(float width)
    {
        number = 0;
        name = 44;
        right = width - 8;
        mode = right - 76;
        time = mode - 64;
    }
};

const char* EncodingName(const netmd::TrackInfo& track)
{
    switch (track.encoding) {
        case netmd::kEncodingLP2: return "LP2";
        case netmd::kEncodingLP4: return "LP4";
        default: return track.mono ? "SP Mono" : "SP";
    }
}

// Posts a write request for songs dropped on the view.
bool HandleDrop(BView* view, BMessage* message, bool busy)
{
    if (!message->WasDropped() || message->what != kMsgTrackDrag || busy)
        return false;
    BMessage write(kMsgWriteToMiniDisc);
    int64 id;
    for (int32 i = 0; message->FindInt64("tracks", i, &id) == B_OK; i++)
        write.AddInt64("tracks", id);
    int64 playlist = 0;
    message->FindInt64("playlist", &playlist);
    write.AddInt64("playlist", playlist);
    write.AddString("kind", "songs");
    view->Window()->PostMessage(&write);
    return true;
}

} // namespace

// ---- summary: the disc, its capacity bar and the column header

class MiniDiscSummaryView : public BView {
public:
    MiniDiscSummaryView()
        : BView("minidisc-summary", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE)
    {
        SetViewColor(B_TRANSPARENT_COLOR);
        SetExplicitMinSize(BSize(300, kSummaryHeight));
        SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, kSummaryHeight));
        fErase = new BButton("erase", "Erase Disc…", new BMessage(kMsgMDErase));
        AddChild(fErase);
    }

    void AttachedToWindow() override
    {
        fErase->SetTarget(Window());
        PlaceButton();
    }

    void FrameResized(float width, float height) override { PlaceButton(); }

    // The button hugs the right edge; its left side limits the text column.
    float PlaceButton()
    {
        fErase->ResizeToPreferred();
        float left = floorf(Bounds().right - fErase->Bounds().Width() - 18);
        if (fErase->Frame().LeftTop() != BPoint(left, 18))
            fErase->MoveTo(left, 18);
        return left;
    }

    void Update(const MiniDiscState& state, int64_t pendingMs, bool erasing, bool writing)
    {
        fState = state;
        fPendingMs = pendingMs;
        fErasing = erasing;
        fWriting = writing;
        fErase->SetEnabled(state.connected && state.known && state.disc.present && !state.busy
            && state.disc.trackCount > 0 && !state.disc.writeProtected);
        Invalidate();
    }

    void Draw(BRect updateRect) override
    {
        BRect bounds = Bounds();
        BRect top(bounds.left, bounds.top, bounds.right, bounds.bottom - kColumnHeaderHeight);
        FillVerticalGradient(this, top, Rgb(252, 252, 252), Rgb(228, 230, 234));
        BRect icon(22, 18, 22 + 74, 18 + 74);
        DrawMiniDisc(this, icon, Rgb(62, 84, 122), Rgb(176, 182, 190), true);

        float textLeft = icon.right + 22;
        float textRight = PlaceButton() - 16;
        BFont title(be_bold_font);
        title.SetSize(18);
        BFont plain(be_plain_font);
        plain.SetSize(11.5f);
        const netmd::DiscInfo& disc = fState.disc;
        BString heading, detail = fState.deviceName.c_str(), counts;
        if (!fState.connected) {
            heading = "No MiniDisc recorder";
            detail = "Connect a NetMD recorder with USB.";
        } else if (!fState.error.empty() && !fState.known) {
            heading = "The recorder cannot be read";
            counts = fState.error.c_str();
        } else if (!fState.known) {
            heading = "Reading the MiniDisc…";
        } else if (!disc.present) {
            heading = "No disc in the recorder";
            counts = "Insert a recordable MiniDisc.";
        } else {
            heading = disc.title.empty() ? "Untitled MiniDisc" : disc.title.c_str();
            counts << disc.trackCount << (disc.trackCount == 1 ? " song" : " songs");
            if (disc.trackCount > 0)
                counts << ", " << netmd::FormatFrames(disc.usedFrames).c_str();
        }
        SetFont(&title);
        SetHighColor(Rgb(30, 30, 30));
        DrawTruncated(this, heading.String(), BRect(textLeft, 18, textRight, 42), B_ALIGN_LEFT, 0);
        SetFont(&plain);
        SetHighColor(theme::kListSecondaryText);
        // a reason recording cannot work is worth more than the recorder's name
        const char* warningText = nullptr;
        if (fState.connected && fState.known && disc.present && disc.writeProtected)
            warningText = "This MiniDisc is write-protected: slide the record tab on the disc to write to it.";
        else if (fState.connected && fState.known && disc.present && !disc.writable)
            warningText = "This MiniDisc cannot be recorded on.";
        if (warningText) {
            BRect warning(textLeft, 42, textRight, 58);
            icons::Draw(this, icons::kWarning, BRect(warning.left, warning.top + 2, warning.left + 13, warning.bottom - 2),
                10, Rgb(200, 110, 20));
            SetHighColor(Rgb(170, 90, 10));
            DrawTruncated(this, warningText, BRect(warning.left + 17, warning.top, warning.right, warning.bottom), B_ALIGN_LEFT, 0);
        } else
            DrawTruncated(this, detail.String(), BRect(textLeft, 42, textRight, 58), B_ALIGN_LEFT, 0);
        DrawTruncated(this, counts.String(), BRect(textLeft, 57, bounds.right - 18, 73), B_ALIGN_LEFT, 0);
        if (fState.connected && fState.known && disc.present && disc.totalFrames > 0)
            DrawCapacity(BRect(textLeft, 80, bounds.right - 18, 94));

        // column header, in the style of the song list's
        BRect header(bounds.left, bounds.bottom - kColumnHeaderHeight + 1, bounds.right, bounds.bottom);
        FillVerticalGradient(this, header, theme::kHeaderTop, theme::kHeaderBottom);
        SetHighColor(theme::kHeaderBorder);
        StrokeLine(header.LeftTop(), header.RightTop());
        StrokeLine(header.LeftBottom(), header.RightBottom());
        Columns columns(bounds.Width());
        BFont small(be_plain_font);
        small.SetSize(11);
        SetFont(&small);
        SetHighColor(theme::kHeaderText);
        DrawTruncated(this, "#", BRect(columns.number, header.top, columns.name, header.bottom), B_ALIGN_RIGHT, 8);
        DrawTruncated(this, "Name", BRect(columns.name, header.top, columns.time, header.bottom), B_ALIGN_LEFT, 6);
        DrawTruncated(this, "Time", BRect(columns.time, header.top, columns.mode, header.bottom), B_ALIGN_RIGHT, 8);
        DrawTruncated(this, "Mode", BRect(columns.mode, header.top, columns.right, header.bottom), B_ALIGN_LEFT, 8);
        SetHighColor(theme::kHeaderBorder);
        for (float x : {columns.name, columns.time, columns.mode})
            StrokeLine(BPoint(x, header.top + 3), BPoint(x, header.bottom - 3));
    }

    void MessageReceived(BMessage* message) override
    {
        if (!HandleDrop(this, message, fState.busy))
            BView::MessageReceived(message);
    }

private:
    // iTunes' capacity bar: recorded audio, what the running write adds, and free space.
    void DrawCapacity(BRect bar)
    {
        const netmd::DiscInfo& disc = fState.disc;
        double total = (double)disc.totalFrames;
        double used = fErasing ? 0 : (double)std::max<int64_t>(0, disc.totalFrames - disc.leftFrames);
        double adding = fWriting ? (double)netmd::SPFramesForDuration(fPendingMs) : 0;
        adding = std::min(adding, total - used);
        float usedEnd = bar.left + (float)(bar.Width() * used / total);
        float addEnd = usedEnd + (float)(bar.Width() * adding / total);
        SetHighColor(0, 0, 0, 30);
        SetDrawingMode(B_OP_ALPHA);
        FillRoundRect(bar.OffsetByCopy(0, 1), 5, 5);
        SetDrawingMode(B_OP_COPY);
        FillRoundGradient(this, bar, 5, Rgb(236, 236, 236), Rgb(208, 208, 208));
        BRegion clip(bar);
        ConstrainClippingRegion(&clip);
        if (usedEnd > bar.left)
            FillVerticalGradient(this, BRect(bar.left, bar.top, usedEnd, bar.bottom), Rgb(120, 160, 225), Rgb(52, 102, 190));
        if (addEnd > usedEnd + 0.5f)
            FillVerticalGradient(this, BRect(usedEnd, bar.top, addEnd, bar.bottom), Rgb(250, 196, 110), Rgb(222, 140, 40));
        ConstrainClippingRegion(nullptr);
        SetHighColor(150, 150, 150);
        StrokeRoundRect(bar, 5, 5);

        BFont small(be_plain_font);
        small.SetSize(10.5f);
        SetFont(&small);
        float x = bar.left;
        float y = bar.bottom + 8;
        auto legend = [&](rgb_color color, const char* label, double frames) {
            BRect swatch(x, y, x + 9, y + 9);
            SetHighColor(color);
            FillRoundRect(swatch, 2, 2);
            SetHighColor(Blend(color, Rgb(0, 0, 0), 0.3f));
            StrokeRoundRect(swatch, 2, 2);
            BString text;
            text << label << " " << netmd::FormatFrames((int64_t)frames).c_str();
            SetHighColor(theme::kListSecondaryText);
            SetDrawingMode(B_OP_OVER);
            DrawString(text.String(), BPoint(x + 14, y + 8.5f));
            x += 14 + StringWidth(text.String()) + 18;
        };
        legend(Rgb(80, 128, 208), "Used", used);
        if (adding > 0)
            legend(Rgb(236, 168, 72), "Adding", adding);
        legend(Rgb(220, 220, 220), "Free", std::max(0.0, total - used - adding));
        BString mode("Space in SP (stereo) time");
        DrawTruncated(this, mode.String(), BRect(x, y - 3, bar.right, y + 12), B_ALIGN_RIGHT, 0);
    }

    BButton* fErase;
    MiniDiscState fState;
    int64_t fPendingMs = 0;
    bool fErasing = false;
    bool fWriting = false;
};

// ---- the tracks

class MiniDiscListView : public BView {
public:
    MiniDiscListView()
        : BView("minidisc-tracks", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_SCROLL_VIEW_AWARE)
    {
        SetViewColor(B_TRANSPARENT_COLOR);
    }

    void SetState(const MiniDiscState& state)
    {
        fState = state;
        Changed();
    }

    std::vector<PendingRow>& Pending() { return fPending; }
    void SetErasing(bool erasing) { fErasing = erasing; }
    void Changed()
    {
        UpdateScrollBar();
        Invalidate();
    }

    int ShownTracks() const
    {
        if (fErasing || !fState.connected || !fState.known || !fState.disc.present)
            return 0;
        return (int)fState.disc.tracks.size();
    }

    void Draw(BRect updateRect) override
    {
        BRect bounds = Bounds();
        SetHighColor(theme::kListBackground);
        FillRect(updateRect);
        int existing = ShownTracks();
        int rows = existing + (int)fPending.size();
        Columns columns(bounds.Width());
        BFont font(be_plain_font);
        font.SetSize(12);
        SetFont(&font);
        if (rows == 0) {
            BString text;
            if (fState.connected && fState.known && fState.disc.present && !fState.contentsKnown && fState.disc.trackCount > 0)
                text = "Reading the song list…";
            else if (fState.connected && fState.known && fState.disc.present)
                text = "This MiniDisc is empty. Drag songs, an album or a playlist here to write them to it.";
            SetHighColor(theme::kListSecondaryText);
            DrawTruncated(this, text.String(), BRect(bounds.left, bounds.top + 30, bounds.right, bounds.top + 50), B_ALIGN_CENTER, 12);
            return;
        }
        int first = std::max(0, (int)(updateRect.top / theme::kRowHeight));
        int last = std::min(rows - 1, (int)(updateRect.bottom / theme::kRowHeight));
        for (int row = first; row <= last; row++) {
            BRect rect(bounds.left, row * theme::kRowHeight, bounds.right, (row + 1) * theme::kRowHeight - 1);
            if (row % 2 == 1) {
                SetHighColor(theme::kListStripe);
                FillRect(rect);
            }
            BString number, name, time, mode;
            rgb_color text = theme::kListText;
            number << row + 1;
            if (row < existing) {
                const netmd::TrackInfo& track = fState.disc.tracks[row];
                name = track.title.empty() ? "(untitled)" : track.title.c_str();
                time = netmd::FormatFrames(track.frames).c_str();
                mode = EncodingName(track);
                if (track.title.empty())
                    text = theme::kListSecondaryText;
            } else {
                const PendingRow& pending = fPending[row - existing];
                name = pending.title;
                time = FormatDuration(pending.durationMs).c_str();
                mode = pending.state == 1 ? "Converting…" : pending.state == 2 ? "Writing…" : pending.state == 3 ? "SP" : "Waiting";
                if (pending.state == 0)
                    text = theme::kListSecondaryText;
                BRect status(columns.number + 6, rect.top + 3, columns.number + 18, rect.top + 15);
                if (pending.state == 1 || pending.state == 2) {
                    DrawProgressPie(this, status, pending.fraction,
                        pending.state == 1 ? Rgb(150, 150, 150) : theme::kSidebarSelectionBottom);
                } else if (pending.state == 3)
                    icons::Draw(this, icons::kCheck, status, 10, Rgb(60, 140, 70));
            }
            SetHighColor(theme::kListSecondaryText);
            DrawTruncated(this, number.String(), BRect(columns.number, rect.top, columns.name, rect.bottom), B_ALIGN_RIGHT, 8);
            SetHighColor(text);
            DrawTruncated(this, name.String(), BRect(columns.name, rect.top, columns.time, rect.bottom), B_ALIGN_LEFT, 6);
            DrawTruncated(this, time.String(), BRect(columns.time, rect.top, columns.mode, rect.bottom), B_ALIGN_RIGHT, 8);
            SetHighColor(theme::kListSecondaryText);
            DrawTruncated(this, mode.String(), BRect(columns.mode, rect.top, columns.right, rect.bottom), B_ALIGN_LEFT, 8);
        }
    }

    void FrameResized(float width, float height) override
    {
        UpdateScrollBar();
    }

    void AttachedToWindow() override
    {
        BView::AttachedToWindow();
        UpdateScrollBar();
    }

    void MessageReceived(BMessage* message) override
    {
        if (!HandleDrop(this, message, fState.busy))
            BView::MessageReceived(message);
    }

    void ScrollToRow(int row)
    {
        float top = row * theme::kRowHeight;
        BRect bounds = Bounds();
        if (top < bounds.top)
            ScrollTo(0, top);
        else if (top + theme::kRowHeight > bounds.bottom)
            ScrollTo(0, top + theme::kRowHeight - bounds.Height());
    }

private:
    void UpdateScrollBar()
    {
        BScrollBar* bar = ScrollBar(B_VERTICAL);
        if (!bar)
            return;
        float content = (ShownTracks() + fPending.size()) * theme::kRowHeight;
        float visible = Bounds().Height();
        bar->SetRange(0, std::max(0.0f, content - visible));
        bar->SetProportion(content > 0 ? std::min(1.0f, visible / content) : 1.0f);
        bar->SetSteps(theme::kRowHeight, visible);
    }

    MiniDiscState fState;
    std::vector<PendingRow> fPending;
    bool fErasing = false;
};

// ---- the container

MiniDiscView::MiniDiscView()
    : BView("minidisc", 0)
{
    fSummary = new MiniDiscSummaryView();
    fList = new MiniDiscListView();
    fScroll = new BScrollView("minidisc-scroll", fList, 0, false, true, B_NO_BORDER);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fSummary)
        .Add(fScroll);
}

void MiniDiscView::SetState(const MiniDiscState& state)
{
    int64_t pending = 0;
    for (const PendingRow& row : fList->Pending())
        pending += row.durationMs;
    bool writing = !fList->Pending().empty();
    fList->SetState(state);
    fSummary->Update(state, pending, writing && fErasing, writing);
}

void MiniDiscView::BeginWrite(const std::vector<BString>& titles, const std::vector<int64_t>& durationsMs, bool erase)
{
    std::vector<PendingRow>& rows = fList->Pending();
    rows.clear();
    for (size_t i = 0; i < titles.size(); i++) {
        PendingRow row;
        row.title = titles[i];
        row.durationMs = i < durationsMs.size() ? durationsMs[i] : 0;
        rows.push_back(row);
    }
    fErasing = erase;
    fList->SetErasing(erase);
    fList->Changed();
}

void MiniDiscView::SetWriteProgress(int track, int phase, float trackFraction)
{
    std::vector<PendingRow>& rows = fList->Pending();
    if (rows.empty())
        return;
    int index = track - 1;
    for (int i = 0; i < (int)rows.size(); i++) {
        PendingRow& row = rows[i];
        if (i < index)
            row.state = 3;
        else if (i == index && (phase == kMDConverting || phase == kMDWriting)) {
            row.state = phase == kMDConverting ? 1 : 2;
            row.fraction = trackFraction;
        } else if (i == index && phase == kMDFinishing)
            row.state = 3;
    }
    fList->Invalidate();
    if (index >= 0 && Window() && !IsHidden())
        fList->ScrollToRow(fList->ShownTracks() + index);
}

void MiniDiscView::EndWrite()
{
    fList->Pending().clear();
    fErasing = false;
    fList->SetErasing(false);
    fList->Changed();
}

} // namespace amp
