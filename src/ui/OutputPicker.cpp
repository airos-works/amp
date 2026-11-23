#include "OutputPicker.h"
#include "Icons.h"
#include "Theme.h"
#include "player/SonosOutput.h"
#include <MessageRunner.h>
#include <Screen.h>
#include <Shape.h>
#include <View.h>
#include <algorithm>
#include <cmath>

namespace amp {

namespace output {

rgb_color ActiveColor()
{
    return theme::IsDark() ? Rgb(92, 160, 255) : Rgb(32, 116, 232);
}

void DrawGlyph(BView* view, BRect rect, rgb_color color)
{
    float s = std::min(rect.Width(), rect.Height());
    float x0 = floorf(rect.left + (rect.Width() - s) / 2);
    float y0 = floorf(rect.top + (rect.Height() - s) / 2);
    auto at = [&](float x, float y) { return BPoint(x0 + x * s, y0 + y * s); };
    view->PushState();
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
    view->SetHighColor(color);
    view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
    view->SetPenSize(std::max(1.0f, roundf(s * 0.09f)));
    // the screen, open below where the triangle stands
    BShape screen;
    screen.MoveTo(at(0.32f, 0.70f));
    screen.LineTo(at(0.08f, 0.70f));
    screen.LineTo(at(0.08f, 0.12f));
    screen.LineTo(at(0.92f, 0.12f));
    screen.LineTo(at(0.92f, 0.70f));
    screen.LineTo(at(0.68f, 0.70f));
    view->MovePenTo(B_ORIGIN); // shapes are drawn relative to the pen
    view->StrokeShape(&screen);
    view->FillTriangle(at(0.5f, 0.54f), at(0.80f, 0.92f), at(0.20f, 0.92f));
    view->PopState();
}

void DrawSpeaker(BView* view, BRect rect, rgb_color color)
{
    float s = std::min(rect.Width(), rect.Height());
    BPoint c((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
    view->PushState();
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
    view->SetHighColor(color);
    view->SetPenSize(std::max(1.0f, roundf(s * 0.08f)));
    BRect cabinet(c.x - s * 0.32f, c.y - s * 0.46f, c.x + s * 0.32f, c.y + s * 0.46f);
    view->StrokeRoundRect(cabinet, s * 0.1f, s * 0.1f);
    view->FillEllipse(BPoint(c.x, c.y + s * 0.13f), s * 0.19f, s * 0.19f);
    view->FillEllipse(BPoint(c.x, c.y - s * 0.24f), s * 0.07f, s * 0.07f);
    view->PopState();
}

void DrawSpinner(BView* view, BRect rect, int phase, rgb_color color)
{
    float r = std::min(rect.Width(), rect.Height()) / 2;
    BPoint c((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
    view->PushState();
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
    view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
    view->SetPenSize(std::max(1.5f, r * 0.2f));
    for (int i = 0; i < 12; i++) {
        // the spoke at `phase` is darkest, the ones behind it fade
        int age = ((phase - i) % 12 + 12) % 12;
        rgb_color spoke = color;
        spoke.alpha = (uint8)(color.alpha * (1.0f - age / 13.0f) * (age == 0 ? 1.0f : 0.85f));
        float angle = i * (float)M_PI / 6;
        float dx = sinf(angle), dy = -cosf(angle);
        view->SetHighColor(spoke);
        view->StrokeLine(BPoint(c.x + dx * r * 0.45f, c.y + dy * r * 0.45f),
            BPoint(c.x + dx * r * 0.92f, c.y + dy * r * 0.92f));
    }
    view->PopState();
}

} // namespace output

namespace {
const uint32 kMsgSpin = 'opsp';
}

// The rows: this computer, then the rooms; a line below them while searching or when
// nothing was found.
class OutputPicker::ListView : public BView {
public:
    ListView(const BMessenger& owner, bool touch)
        : BView("outputs", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE), fOwner(owner), fTouch(touch)
    {
        SetViewColor(B_TRANSPARENT_COLOR);
        BFont font(be_plain_font);
        if (touch)
            font.SetSize(be_plain_font->Size() * 1.25f);
        SetFont(&font);
        fFont = font;
        font_height height;
        font.GetHeight(&height);
        float text = ceilf(height.ascent + height.descent);
        fRowHeight = touch ? std::max(ceilf(be_plain_font->Size() * 3.6f), text + 20) : std::max(24.0f, text + 10);
    }

    ~ListView() override { delete fSpin; }

    void AttachedToWindow() override
    {
        // every press, wherever it is: one outside closes the list, as with a menu (a click
        // on Amp's own window does not deactivate a floating one)
        SetEventMask(B_POINTER_EVENTS, 0);
        BView::AttachedToWindow();
    }

    void SetChoices(const OutputChoices& choices)
    {
        fChoices = choices;
        UpdateSpin();
        Invalidate();
    }

    int32 RowCount() const { return 1 + (int32)fChoices.devices.size(); }
    bool HasFooter() const { return fChoices.searching || fChoices.devices.empty(); }
    float Padding() const { return fTouch ? 6 : 4; }

    BSize ContentSize()
    {
        float width = fFont.StringWidth("This Computer");
        for (const SonosDevice& device : fChoices.devices)
            width = std::max(width, fFont.StringWidth(device.name.c_str()));
        BFont small(fFont);
        small.SetSize(fFont.Size() * 0.9f);
        float footer = small.StringWidth(FooterText().String()) + fRowHeight * 0.8f;
        width = std::max(width + fRowHeight * 2.2f, footer + 24);
        width = std::max(width, fTouch ? be_plain_font->Size() * 22 : 230.0f);
        width = std::min(width, 480.0f * (fTouch ? 1.5f : 1.0f));
        float height = Padding() * 2 + RowCount() * fRowHeight + 1;
        if (HasFooter())
            height += ceilf(fRowHeight * 0.9f);
        return BSize(ceilf(width), ceilf(height));
    }

    void MessageReceived(BMessage* message) override
    {
        if (message->what == kMsgSpin) {
            fPhase = (fPhase + 1) % 12;
            Invalidate();
            return;
        }
        BView::MessageReceived(message);
    }

    void Draw(BRect update) override
    {
        const theme::Palette& colors = theme::Colors();
        BRect bounds = Bounds();
        SetHighColor(colors.listBackground);
        FillRect(update);
        SetDrawingMode(B_OP_OVER);
        for (int32 i = 0; i < RowCount(); i++)
            DrawRow(i);
        if (HasFooter()) {
            BRect footer(bounds.left, RowRect(RowCount() - 1).bottom + 1, bounds.right, bounds.bottom - Padding());
            BFont small(fFont);
            small.SetSize(fFont.Size() * 0.9f);
            SetFont(&small);
            float spinner = 0;
            if (fChoices.searching) {
                spinner = floorf(fRowHeight * 0.42f);
                BRect box(footer.left + 12, 0, footer.left + 12 + spinner, spinner);
                box.OffsetTo(box.left, floorf((footer.top + footer.bottom - spinner) / 2));
                output::DrawSpinner(this, box, fPhase, colors.listSecondaryText);
                spinner += 8;
            }
            SetHighColor(colors.listSecondaryText);
            DrawTruncated(this, FooterText().String(), BRect(footer.left + 12 + spinner, footer.top,
                footer.right - 8, footer.bottom), B_ALIGN_LEFT, 0);
            SetFont(&fFont);
        }
        SetDrawingMode(B_OP_COPY);
        // the window's edge
        SetHighColor(colors.listGrid);
        StrokeRect(bounds);
    }

    void MouseDown(BPoint where) override
    {
        if (!Bounds().Contains(where)) {
            Window()->PostMessage(B_QUIT_REQUESTED);
            return;
        }
        fPressed = RowAt(where);
        fHover = fPressed;
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
        Invalidate();
    }

    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override
    {
        // a finger shows the row it is on only while it is down
        int32 hover = (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW) ? -1 : RowAt(where);
        if (fTouch && fPressed < 0)
            hover = -1;
        if (hover != fHover) {
            fHover = hover;
            Invalidate();
        }
    }

    void MouseUp(BPoint where) override
    {
        int32 row = RowAt(where);
        int32 pressed = fPressed;
        fPressed = -1;
        if (fTouch)
            fHover = -1;
        Invalidate();
        if (row < 0 || row != pressed)
            return;
        BMessage choice(kMsgChooseOutput);
        choice.AddString("uuid", row == 0 ? "" : fChoices.devices[row - 1].uuid.c_str());
        fOwner.SendMessage(&choice);
        Window()->PostMessage(B_QUIT_REQUESTED);
    }

private:
    BRect RowRect(int32 row) const
    {
        float top = Padding() + row * fRowHeight + (row > 0 ? 1 : 0); // a line below this computer
        return BRect(Bounds().left + 1, top, Bounds().right - 1, top + fRowHeight - 1);
    }

    int32 RowAt(BPoint where) const
    {
        for (int32 i = 0; i < RowCount(); i++)
            if (RowRect(i).Contains(where))
                return i;
        return -1;
    }

    BString FooterText() const
    {
        if (fChoices.searching)
            return "Looking for Sonos speakers…";
        if (!fChoices.error.IsEmpty())
            return BString("No Sonos speakers: ") << fChoices.error;
        return "No Sonos speakers found";
    }

    bool Spinning() const
    {
        return fChoices.searching || fChoices.state == kOutputConnecting;
    }

    void UpdateSpin()
    {
        if (Spinning() && !fSpin) {
            BMessage spin(kMsgSpin);
            fSpin = new BMessageRunner(BMessenger(this), &spin, output::kSpinnerFrame);
        } else if (!Spinning()) {
            delete fSpin;
            fSpin = nullptr;
        }
    }

    void DrawRow(int32 index)
    {
        const theme::Palette& colors = theme::Colors();
        BRect row = RowRect(index);
        bool hot = index == fHover && (fPressed < 0 || fPressed == index);
        rgb_color text = colors.listText;
        if (hot) {
            FillVerticalGradient(this, row, colors.selectionTop, colors.selectionBottom);
            text = colors.selectedText;
        }
        if (index == 0) {
            SetHighColor(colors.listGrid);
            StrokeLine(BPoint(row.left + 8, row.bottom + 1), BPoint(row.right - 8, row.bottom + 1));
        }
        const SonosDevice* device = index > 0 ? &fChoices.devices[index - 1] : nullptr;
        bool connecting = device && fChoices.state == kOutputConnecting && fChoices.uuid == device->uuid.c_str();
        bool active = device ? (fChoices.state == kOutputRemote && fChoices.uuid == device->uuid.c_str())
            : fChoices.state == kOutputLocal;
        float icon = floorf(fRowHeight * 0.56f);
        BRect iconRect(row.left + 10, 0, row.left + 10 + icon, icon);
        iconRect.OffsetTo(iconRect.left, floorf((row.top + row.bottom - icon) / 2));
        rgb_color iconColor = hot ? text : (active ? output::ActiveColor() : colors.listIcon);
        if (device)
            output::DrawSpeaker(this, iconRect, iconColor);
        else
            icons::Draw(this, icons::kDesktop, iconRect, icon * 0.72f, iconColor);
        float accessory = floorf(fRowHeight * 0.5f);
        BRect right(row.right - 10 - accessory, 0, row.right - 10, accessory);
        right.OffsetTo(right.left, floorf((row.top + row.bottom - accessory) / 2));
        if (connecting)
            output::DrawSpinner(this, right, fPhase, hot ? text : colors.listSecondaryText);
        else if (active)
            icons::Draw(this, icons::kCheck, right, accessory * 0.8f, hot ? text : output::ActiveColor());
        BFont font(fFont);
        if (active)
            font.SetFace(B_BOLD_FACE);
        SetFont(&font);
        SetHighColor(text);
        DrawTruncated(this, device ? device->name.c_str() : "This Computer",
            BRect(iconRect.right + 10, row.top, right.left - 8, row.bottom), B_ALIGN_LEFT, 0);
        SetFont(&fFont);
    }

    BMessenger fOwner;
    bool fTouch;
    BFont fFont;
    float fRowHeight;
    OutputChoices fChoices;
    int32 fHover = -1;
    int32 fPressed = -1;
    int fPhase = 0;
    BMessageRunner* fSpin = nullptr;
};

OutputPicker::OutputPicker(const BMessenger& owner, BRect anchor, bool touch, const OutputChoices& choices)
    : BWindow(BRect(0, 0, 100, 100), "Speakers", B_BORDERED_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
          B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_CLOSABLE
              | B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
      fOwner(owner), fAnchor(anchor), fTouch(touch)
{
    fList = new ListView(owner, touch);
    AddChild(fList);
    SetChoices(choices);
}

OutputPicker::~OutputPicker()
{
    BMessage closed(kMsgOutputPickerClosed);
    closed.AddInt64("when", system_time());
    fOwner.SendMessage(&closed);
}

void OutputPicker::SetChoices(const OutputChoices& choices)
{
    fList->SetChoices(choices);
    FitToContent();
}

void OutputPicker::FitToContent()
{
    BSize size = fList->ContentSize();
    ResizeTo(size.width, size.height);
    fList->ResizeTo(size.width, size.height);
    BRect screen = BScreen(this).Frame();
    // below the button: from its left edge on the desktop, ending at its right edge on a
    // tablet (the button is at the right end of the bar there)
    float left = fTouch ? fAnchor.right - size.width : fAnchor.left - 6;
    float top = fAnchor.bottom + 4;
    left = std::max(screen.left + 4, std::min(left, screen.right - size.width - 4));
    if (top + size.height > screen.bottom - 4)
        top = std::max(screen.top + 4, fAnchor.top - size.height - 4);
    MoveTo(left, top);
}

void OutputPicker::WindowActivated(bool active)
{
    // a click anywhere else closes it, as with a menu
    if (!active)
        PostMessage(B_QUIT_REQUESTED);
    BWindow::WindowActivated(active);
}

void OutputPicker::MessageReceived(BMessage* message)
{
    if (message->what == kMsgOutputChoices) {
        SetChoices(Unarchive(*message));
        return;
    }
    if (message->what == B_COLORS_UPDATED)
        fList->Invalidate();
    BWindow::MessageReceived(message);
}

void OutputPicker::Archive(const OutputChoices& choices, BMessage& message)
{
    message.what = kMsgOutputChoices;
    for (const SonosDevice& device : choices.devices)
        SonosOutput::AddDevice(message, device);
    message.AddBool("searching", choices.searching);
    message.AddString("error", choices.error);
    message.AddInt32("state", choices.state);
    message.AddString("active", choices.uuid);
}

OutputChoices OutputPicker::Unarchive(const BMessage& message)
{
    OutputChoices choices;
    choices.devices = SonosOutput::Devices(message);
    choices.searching = message.GetBool("searching", false);
    choices.error = message.GetString("error", "");
    choices.state = (OutputState)message.GetInt32("state", kOutputLocal);
    choices.uuid = message.GetString("active", "");
    return choices;
}

} // namespace amp
