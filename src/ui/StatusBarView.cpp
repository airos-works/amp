#include "StatusBarView.h"
#include "Icons.h"
#include "Theme.h"
#include "player/Messages.h"
#include <Font.h>
#include <Window.h>

namespace amp {

namespace {
const uint32 kMsgExpire = 'stex';

// The Settings button is the right-most element of the bar.
const float kSettingsButtonWidth = 78.0f;
const float kStatusBarInset = 8.0f;
const float kMiniDiscButtonWidth = 136.0f;
}

StatusBarView::StatusBarView()
    : BView("statusbar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(300, theme::kStatusBarHeight));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, theme::kStatusBarHeight));
}

void StatusBarView::SetSummary(const BString& text)
{
    fSummary = text;
    Invalidate();
}

void StatusBarView::SetTransient(const BString& text, int seconds)
{
    fTransient = text;
    delete fExpiry;
    fExpiry = nullptr;
    if (seconds > 0 && !text.IsEmpty() && Window()) {
        BMessage expire(kMsgExpire);
        fExpiry = new BMessageRunner(BMessenger(this), &expire, (bigtime_t)seconds * 1000000, 1);
    }
    Invalidate();
}

void StatusBarView::MessageReceived(BMessage* message)
{
    if (message->what == kMsgExpire) {
        fTransient = "";
        Invalidate();
        return;
    }
    BView::MessageReceived(message);
}

void StatusBarView::DetachedFromWindow()
{
    delete fExpiry;
    fExpiry = nullptr;
    BView::DetachedFromWindow();
}

void StatusBarView::SetShuffle(bool shuffle)
{
    fShuffle = shuffle;
    Invalidate();
}

void StatusBarView::SetRepeat(int repeat)
{
    fRepeat = repeat;
    Invalidate();
}

BRect StatusBarView::ButtonRect(int index) const
{
    float left = 6 + index * 34;
    return BRect(left, 3, left + 30, theme::kStatusBarHeight - 4);
}

void StatusBarView::SetMiniDiscButton(bool visible, bool enabled)
{
    if (visible == fMiniDiscVisible && enabled == fMiniDiscEnabled)
        return;
    fMiniDiscVisible = visible;
    fMiniDiscEnabled = enabled;
    Invalidate();
}

BRect StatusBarView::MiniDiscRect() const
{
    BRect settings = SettingsRect();
    return BRect(settings.left - 8 - kMiniDiscButtonWidth, settings.top, settings.left - 8, settings.bottom);
}

BRect StatusBarView::SettingsRect() const
{
    BRect bounds = Bounds();
    float right = bounds.right - kStatusBarInset;
    return BRect(right - kSettingsButtonWidth, 3, right, theme::kStatusBarHeight - 4);
}

void StatusBarView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    const theme::Palette& colors = theme::Colors();
    FillVerticalGradient(this, bounds, colors.statusTop, colors.statusBottom);
    SetHighColor(colors.toolbarBorder);
    StrokeLine(bounds.LeftTop(), bounds.RightTop());
    for (int i = 0; i < 3; i++) {
        BRect r = ButtonRect(i);
        bool active = (i == 1 && fShuffle) || (i == 2 && fRepeat != 0);
        bool pressed = fPressed == (Hot)(i + 1);
        DrawButtonFace(r, active || pressed);
        icons::Icon glyph = i == 0 ? icons::kPlus : (i == 1 ? icons::kShuffle : icons::kRepeat);
        icons::Draw(this, glyph, r, 11, active ? colors.buttonOnGlyph : colors.buttonGlyph);
        if (i == 2 && fRepeat == 2) {
            // repeat one: a small marker on the repeat glyph
            BFont small(be_bold_font);
            small.SetSize(8);
            SetFont(&small);
            SetHighColor(active ? colors.buttonOnGlyph : colors.buttonGlyph);
            SetDrawingMode(B_OP_OVER);
            float cx = (r.left + r.right) / 2;
            float cy = (r.top + r.bottom) / 2;
            DrawString("1", BPoint(cx + 1, cy + 6));
        }
    }
    BFont font(be_plain_font);
    font.SetSize(11);
    SetFont(&font);
    SetHighColor(colors.toolbarText);
    BString center = fTransient.IsEmpty() ? fSummary : fTransient;
    // the MiniDisc and Settings buttons own the right-hand end: keep the summary clear of them
    float centerRight = (fMiniDiscVisible ? MiniDiscRect().left : SettingsRect().left) - 24;
    if (centerRight < 120)
        centerRight = 120;
    DrawTruncated(this, center.String(), BRect(120, 0, centerRight, bounds.bottom), B_ALIGN_CENTER, 0);
    DrawSettingsButton();
    if (fMiniDiscVisible)
        DrawMiniDiscButton();
}

// A button's rounded face and its outline, held down or switched on when `on`.
void StatusBarView::DrawButtonFace(BRect r, bool on)
{
    const theme::Palette& colors = theme::Colors();
    if (on)
        FillRoundGradient(this, r, 4, colors.buttonOnTop, colors.buttonOnBottom);
    else
        FillRoundGradient(this, r, 4, colors.buttonTop, colors.buttonBottom);
    SetHighColor(colors.buttonBorder);
    StrokeRoundRect(r, 4, 4);
}

void StatusBarView::DrawMiniDiscButton()
{
    BRect r = MiniDiscRect();
    bool pressed = fPressed == kMiniDisc;
    const theme::Palette& colors = theme::Colors();
    DrawButtonFace(r, pressed);
    rgb_color ink = !fMiniDiscEnabled ? colors.buttonDisabledGlyph : pressed ? colors.buttonOnGlyph : colors.buttonGlyph;
    float cy = (r.top + r.bottom) / 2;
    DrawMiniDisc(this, BRect(r.left + 6, cy - 6, r.left + 18, cy + 6), ink,
        pressed ? Blend(colors.buttonOnTop, colors.buttonOnBottom, 0.5f) : Blend(colors.buttonTop, colors.buttonBottom, 0.5f));
    BFont font(be_plain_font);
    font.SetSize(11);
    SetFont(&font);
    SetHighColor(!fMiniDiscEnabled ? colors.disabledText : pressed ? colors.buttonOnGlyph : colors.toolbarText);
    SetDrawingMode(B_OP_OVER);
    DrawTruncated(this, "Write to MiniDisc", BRect(r.left + 22, r.top, r.right - 4, r.bottom), B_ALIGN_LEFT, 0);
    SetDrawingMode(B_OP_COPY);
}

void StatusBarView::DrawSettingsButton()
{
    BRect r = SettingsRect();
    bool pressed = fPressed == kSettings;
    const theme::Palette& colors = theme::Colors();
    DrawButtonFace(r, pressed);

    // the Font Awesome gear, centred on its ink
    float cy = (r.top + r.bottom) / 2;
    icons::Draw(this, icons::kGear, BRect(r.left + 5, cy - 7, r.left + 19, cy + 7), 13,
        pressed ? colors.buttonOnGlyph : colors.buttonGlyph);

    BFont font(be_plain_font);
    font.SetSize(11);
    SetFont(&font);
    SetHighColor(pressed ? colors.buttonOnGlyph : colors.toolbarText);
    SetDrawingMode(B_OP_OVER);
    DrawTruncated(this, "Settings", BRect(r.left + 22, r.top, r.right - 6, r.bottom), B_ALIGN_LEFT, 0);
    SetDrawingMode(B_OP_COPY);
}

StatusBarView::Hot StatusBarView::HitTest(BPoint where) const
{
    for (int i = 0; i < 3; i++)
        if (ButtonRect(i).Contains(where))
            return (Hot)(i + 1);
    if (SettingsRect().Contains(where))
        return kSettings;
    if (fMiniDiscVisible && fMiniDiscEnabled && MiniDiscRect().Contains(where))
        return kMiniDisc;
    return kNone;
}

void StatusBarView::MouseDown(BPoint where)
{
    fPressed = HitTest(where);
    if (fPressed != kNone) {
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
        Invalidate();
    }
}

void StatusBarView::MouseUp(BPoint where)
{
    Hot released = HitTest(where);
    if (released == fPressed && fPressed != kNone) {
        if (fPressed == kAdd)
            Window()->PostMessage(kMsgNewPlaylist);
        else if (fPressed == kShuffle)
            Window()->PostMessage(kMsgToggleShuffle);
        else if (fPressed == kRepeat)
            Window()->PostMessage(kMsgToggleRepeat);
        else if (fPressed == kMiniDisc) {
            BMessage write(kMsgWriteToMiniDisc);
            write.AddString("kind", "view");
            Window()->PostMessage(&write);
        } else
            Window()->PostMessage(kMsgShowSettings);
    }
    fPressed = kNone;
    Invalidate();
}

} // namespace amp
