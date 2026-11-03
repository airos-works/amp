#include "Theme.h"
#include "Icons.h"
#include <Bitmap.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <GradientLinear.h>
#include <Region.h>
#include <Shape.h>
#include <Window.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace amp {

namespace theme {

namespace {

Palette LightPalette()
{
    Palette p;
        p.toolbarTop = Rgb(236, 236, 236);
        p.toolbarBottom = Rgb(176, 176, 176);
        p.toolbarBorder = Rgb(108, 108, 108);
        p.toolbarText = Rgb(70, 70, 70);
        p.toolbarIcon = Rgb(90, 90, 90);
        p.barHighlight = Rgb(255, 255, 255, 140);
        p.statusTop = Rgb(226, 226, 226);
        p.statusBottom = Rgb(190, 190, 190);
        p.buttonTop = Rgb(251, 251, 251);
        p.buttonBottom = Rgb(200, 200, 200);
        p.buttonPressedTop = Rgb(190, 190, 190);
        p.buttonPressedBottom = Rgb(150, 150, 150);
        p.buttonOnTop = Rgb(130, 130, 130);
        p.buttonOnBottom = Rgb(160, 160, 160);
        p.buttonBorder = Rgb(116, 116, 116);
        p.buttonShadow = Rgb(0, 0, 0, 40);
        p.buttonGlyph = Rgb(64, 64, 64);
        p.buttonOnGlyph = Rgb(255, 255, 255);
        p.buttonDisabledGlyph = Rgb(155, 155, 155);
        p.sliderTrackTop = Rgb(150, 150, 150);
        p.sliderTrackBottom = Rgb(190, 190, 190);
        p.sliderFillTop = Rgb(96, 96, 96);
        p.sliderFillBottom = Rgb(130, 130, 130);
        p.sliderBorder = Rgb(110, 110, 110);
        p.knobTop = Rgb(250, 250, 250);
        p.knobBottom = Rgb(190, 190, 190);
        p.knobBorder = Rgb(100, 100, 100);
        p.lcdTop = Rgb(232, 237, 226);
        p.lcdBottom = Rgb(200, 210, 190);
        p.lcdBorder = Rgb(143, 151, 138);
        p.lcdText = Rgb(40, 45, 38);
        p.lcdSecondary = Rgb(90, 98, 86);
        p.lcdBar = Rgb(120, 130, 115);
        p.lcdBarBackground = Rgb(180, 190, 172);
        p.lcdHighlight = Rgb(255, 255, 255, 120);
        p.lcdInset = Rgb(219, 226, 213);
        p.lcdButton = Rgb(128, 136, 122);
        p.lcdButtonPressed = Rgb(80, 88, 76);
        p.lcdButtonGlyph = Rgb(236, 240, 230);
        p.lcdKnobTop = Rgb(255, 255, 255);
        p.lcdKnobBottom = Rgb(206, 212, 200);
        p.lcdWarning = Rgb(170, 90, 40);
        p.sidebarBackground = Rgb(214, 221, 229);
        p.sidebarBorder = Rgb(160, 168, 178);
        p.sidebarHeader = Rgb(107, 122, 140);
        p.sidebarText = Rgb(20, 20, 20);
        p.sidebarIcon = Rgb(70, 90, 120);
        p.sidebarSelectionTop = Rgb(110, 150, 217);
        p.sidebarSelectionBottom = Rgb(58, 107, 193);
        p.sidebarSelectionInactiveTop = Rgb(170, 170, 170);
        p.sidebarSelectionInactiveBottom = Rgb(140, 140, 140);
        p.dropFill = Rgb(255, 255, 255, 140);
        p.accent = Rgb(58, 107, 193);
        p.listBackground = Rgb(255, 255, 255);
        p.listStripe = Rgb(237, 243, 254);
        p.listText = Rgb(0, 0, 0);
        p.listSecondaryText = Rgb(110, 110, 110);
        p.listGrid = Rgb(214, 214, 214);
        p.listIcon = Rgb(60, 60, 60);
        p.selectionTop = Rgb(97, 140, 219);
        p.selectionBottom = Rgb(54, 105, 197);
        p.selectionInactiveTop = Rgb(195, 195, 195);
        p.selectionInactiveBottom = Rgb(165, 165, 165);
        p.selectedText = Rgb(255, 255, 255);
        p.headerTop = Rgb(250, 250, 250);
        p.headerBottom = Rgb(222, 222, 222);
        p.headerBorder = Rgb(165, 165, 165);
        p.headerText = Rgb(40, 40, 40);
        p.headerSortedTop = Rgb(219, 229, 246);
        p.headerSortedBottom = Rgb(186, 204, 236);
        p.pageHeaderTop = Rgb(248, 248, 248);
        p.pageHeaderBottom = Rgb(226, 227, 230);
        p.headingText = Rgb(30, 30, 30);
        p.link = Rgb(30, 84, 176);
        p.artPlaceholderTop = Rgb(232, 232, 232);
        p.artPlaceholderBottom = Rgb(200, 200, 200);
        p.artPlaceholderNote = Rgb(160, 160, 160);
        p.artPlaceholderBorder = Rgb(170, 170, 170);
        p.artHairline = Rgb(0, 0, 0, 46);
        p.capacityTop = Rgb(236, 236, 236);
        p.capacityBottom = Rgb(208, 208, 208);
        p.capacityBorder = Rgb(150, 150, 150);
        p.capacityFree = Rgb(220, 220, 220);
        p.warning = Rgb(200, 110, 20);
        p.warningText = Rgb(170, 90, 10);
        p.success = Rgb(60, 140, 70);
        p.error = Rgb(200, 40, 30);
        p.muted = Rgb(150, 150, 150);
        p.disabledText = Rgb(150, 150, 150);
    return p;
}

Palette DarkPalette()
{
    Palette p;
        p.toolbarTop = Rgb(66, 66, 68);
        p.toolbarBottom = Rgb(40, 40, 42);
        p.toolbarBorder = Rgb(14, 14, 14);
        p.toolbarText = Rgb(196, 196, 198);
        p.toolbarIcon = Rgb(176, 176, 178);
        p.barHighlight = Rgb(255, 255, 255, 26);
        p.statusTop = Rgb(54, 54, 56);
        p.statusBottom = Rgb(38, 38, 40);
        p.buttonTop = Rgb(92, 92, 94);
        p.buttonBottom = Rgb(62, 62, 64);
        p.buttonPressedTop = Rgb(44, 44, 46);
        p.buttonPressedBottom = Rgb(58, 58, 60);
        p.buttonOnTop = Rgb(32, 32, 34);
        p.buttonOnBottom = Rgb(48, 48, 50);
        p.buttonBorder = Rgb(18, 18, 20);
        p.buttonShadow = Rgb(0, 0, 0, 90);
        p.buttonGlyph = Rgb(226, 226, 228);
        p.buttonOnGlyph = Rgb(132, 180, 255);
        p.buttonDisabledGlyph = Rgb(110, 110, 112);
        p.sliderTrackTop = Rgb(24, 24, 26);
        p.sliderTrackBottom = Rgb(38, 38, 40);
        p.sliderFillTop = Rgb(176, 176, 178);
        p.sliderFillBottom = Rgb(140, 140, 142);
        p.sliderBorder = Rgb(14, 14, 16);
        p.knobTop = Rgb(212, 212, 214);
        p.knobBottom = Rgb(150, 150, 152);
        p.knobBorder = Rgb(18, 18, 20);
        p.lcdTop = Rgb(52, 56, 50);
        p.lcdBottom = Rgb(34, 38, 32);
        p.lcdBorder = Rgb(12, 14, 11);
        p.lcdText = Rgb(224, 230, 216);
        p.lcdSecondary = Rgb(156, 164, 148);
        p.lcdBar = Rgb(176, 186, 166);
        p.lcdBarBackground = Rgb(22, 25, 20);
        p.lcdHighlight = Rgb(255, 255, 255, 22);
        p.lcdInset = Rgb(26, 29, 24);
        p.lcdButton = Rgb(92, 100, 88);
        p.lcdButtonPressed = Rgb(132, 140, 126);
        p.lcdButtonGlyph = Rgb(24, 26, 22);
        p.lcdKnobTop = Rgb(236, 240, 230);
        p.lcdKnobBottom = Rgb(176, 184, 168);
        p.lcdWarning = Rgb(236, 150, 90);
        p.sidebarBackground = Rgb(36, 38, 42);
        p.sidebarBorder = Rgb(14, 14, 16);
        p.sidebarHeader = Rgb(132, 142, 158);
        p.sidebarText = Rgb(222, 222, 226);
        p.sidebarIcon = Rgb(140, 164, 200);
        p.sidebarSelectionTop = Rgb(74, 118, 196);
        p.sidebarSelectionBottom = Rgb(44, 86, 166);
        p.sidebarSelectionInactiveTop = Rgb(84, 84, 88);
        p.sidebarSelectionInactiveBottom = Rgb(66, 66, 70);
        p.dropFill = Rgb(255, 255, 255, 36);
        p.accent = Rgb(96, 146, 230);
        p.listBackground = Rgb(28, 28, 30);
        p.listStripe = Rgb(36, 38, 44);
        p.listText = Rgb(232, 232, 234);
        p.listSecondaryText = Rgb(150, 150, 156);
        p.listGrid = Rgb(58, 58, 62);
        p.listIcon = Rgb(200, 200, 204);
        p.selectionTop = Rgb(74, 118, 196);
        p.selectionBottom = Rgb(44, 86, 166);
        p.selectionInactiveTop = Rgb(86, 86, 90);
        p.selectionInactiveBottom = Rgb(68, 68, 72);
        p.selectedText = Rgb(255, 255, 255);
        p.headerTop = Rgb(62, 62, 64);
        p.headerBottom = Rgb(44, 44, 46);
        p.headerBorder = Rgb(18, 18, 20);
        p.headerText = Rgb(214, 214, 216);
        p.headerSortedTop = Rgb(60, 82, 120);
        p.headerSortedBottom = Rgb(42, 62, 98);
        p.pageHeaderTop = Rgb(50, 50, 53);
        p.pageHeaderBottom = Rgb(36, 36, 38);
        p.headingText = Rgb(236, 236, 238);
        p.link = Rgb(120, 170, 245);
        p.artPlaceholderTop = Rgb(72, 72, 76);
        p.artPlaceholderBottom = Rgb(52, 52, 56);
        p.artPlaceholderNote = Rgb(128, 128, 134);
        p.artPlaceholderBorder = Rgb(22, 22, 24);
        p.artHairline = Rgb(255, 255, 255, 30);
        p.capacityTop = Rgb(66, 66, 70);
        p.capacityBottom = Rgb(50, 50, 54);
        p.capacityBorder = Rgb(20, 20, 22);
        p.capacityFree = Rgb(70, 70, 74);
        p.warning = Rgb(240, 150, 60);
        p.warningText = Rgb(240, 165, 85);
        p.success = Rgb(110, 195, 120);
        p.error = Rgb(255, 110, 100);
        p.muted = Rgb(110, 110, 114);
        p.disabledText = Rgb(108, 108, 112);
    return p;
}

} // namespace

bool IsDark()
{
    return ui_color(B_PANEL_BACKGROUND_COLOR).IsDark();
}

const Palette& Colors()
{
    static const Palette light = LightPalette();
    static const Palette dark = DarkPalette();
    return IsDark() ? dark : light;
}

static void InvalidateTree(BView* view)
{
    view->Invalidate();
    for (BView* child = view->ChildAt(0); child != nullptr; child = child->NextSibling())
        InvalidateTree(child);
}

void InvalidateAll(BWindow* window)
{
    for (int32 i = 0; BView* view = window->ChildAt(i); i++)
        InvalidateTree(view);
}

} // namespace theme

rgb_color Blend(rgb_color a, rgb_color b, float amount)
{
    rgb_color out;
    out.red = (uint8)(a.red + (b.red - a.red) * amount);
    out.green = (uint8)(a.green + (b.green - a.green) * amount);
    out.blue = (uint8)(a.blue + (b.blue - a.blue) * amount);
    out.alpha = 255;
    return out;
}

void FillVerticalGradient(BView* view, BRect rect, rgb_color top, rgb_color bottom)
{
    BGradientLinear gradient(BPoint(rect.left, rect.top), BPoint(rect.left, rect.bottom));
    gradient.AddColor(top, 0);
    gradient.AddColor(bottom, 255);
    view->FillRect(rect, gradient);
}

void FillRoundGradient(BView* view, BRect rect, float radius, rgb_color top, rgb_color bottom)
{
    BGradientLinear gradient(BPoint(rect.left, rect.top), BPoint(rect.left, rect.bottom));
    gradient.AddColor(top, 0);
    gradient.AddColor(bottom, 255);
    view->FillRoundRect(rect, radius, radius, gradient);
}

void DrawNoteIcon(BView* view, BRect rect, rgb_color color)
{
    // the Font Awesome music note, centred in the rect
    icons::DrawFitted(view, icons::kMusic, rect, color);
}

void DrawMiniDisc(BView* view, BRect rect, rgb_color body, rgb_color shutter, bool detailed)
{
    if (detailed ? icons::DrawPicture(view, icons::kMiniDiscPicture, rect)
            : icons::DrawGlyph(view, icons::kMiniDiscGlyph, rect, body))
        return;
    float side = floorf(std::min(rect.Width(), rect.Height()));
    float left = floorf(rect.left + (rect.Width() - side) / 2);
    float top = floorf(rect.top + (rect.Height() - side) / 2);
    float right = left + side, bottom = top + side;
    float cut = std::max(2.0f, floorf(side * 0.16f));
    BPoint outline[] = {BPoint(left, top), BPoint(right - cut, top), BPoint(right, top + cut),
        BPoint(right, bottom), BPoint(left, bottom)};
    BShape shape;
    shape.MoveTo(outline[0]);
    for (int i = 1; i < 5; i++)
        shape.LineTo(outline[i]);
    shape.Close();
    view->PushState();
    view->MovePenTo(B_ORIGIN);
    if (detailed) {
        BGradientLinear gradient(BPoint(left, top), BPoint(left, bottom));
        gradient.AddColor(Blend(body, Rgb(255, 255, 255), 0.25f), 0);
        gradient.AddColor(Blend(body, Rgb(0, 0, 0), 0.2f), 255);
        view->FillShape(&shape, gradient);
        view->SetHighColor(Blend(body, Rgb(0, 0, 0), 0.45f));
        view->StrokeShape(&shape);
        // label area with two ruled lines
        BRect label(left + side * 0.12f, top + side * 0.1f, right - side * 0.2f, top + side * 0.42f);
        view->SetHighColor(Blend(body, Rgb(255, 255, 255), 0.8f));
        view->FillRoundRect(label, 2, 2);
        view->SetHighColor(Blend(body, Rgb(255, 255, 255), 0.45f));
        for (int i = 1; i <= 2; i++) {
            float y = floorf(label.top + label.Height() * i / 3) + 0.5f;
            view->StrokeLine(BPoint(label.left + 3, y), BPoint(label.right - 3, y));
        }
    } else {
        view->SetHighColor(body);
        view->FillShape(&shape);
    }
    // the shutter covers the lower middle; through its window the disc shows
    BRect door(left + side * 0.2f, top + side * 0.5f, right - side * 0.2f, bottom - 1);
    if (detailed) {
        BGradientLinear metal(BPoint(door.left, door.top), BPoint(door.right, door.top));
        metal.AddColor(Blend(shutter, Rgb(255, 255, 255), 0.5f), 0);
        metal.AddColor(shutter, 128);
        metal.AddColor(Blend(shutter, Rgb(255, 255, 255), 0.35f), 255);
        view->FillRect(door, metal);
        view->SetHighColor(Blend(shutter, Rgb(0, 0, 0), 0.35f));
        view->StrokeRect(door);
        // the window: a dark opening with the rainbow-grey edge of the disc and its hub
        BRect window(door.left + side * 0.07f, door.top + side * 0.07f, door.right - side * 0.07f, door.bottom - side * 0.07f);
        view->SetHighColor(Blend(body, Rgb(0, 0, 0), 0.55f));
        view->FillRect(window);
        BRegion clip(window);
        view->ConstrainClippingRegion(&clip);
        BPoint center((window.left + window.right) / 2, window.top - side * 0.08f);
        float radius = side * 0.34f;
        BGradientLinear disc(BPoint(center.x - radius, center.y), BPoint(center.x + radius, center.y));
        disc.AddColor(Rgb(150, 160, 180), 0);
        disc.AddColor(Rgb(236, 238, 244), 110);
        disc.AddColor(Rgb(184, 170, 196), 170);
        disc.AddColor(Rgb(140, 150, 168), 255);
        view->FillEllipse(center, radius, radius, disc);
        view->SetHighColor(Blend(body, Rgb(0, 0, 0), 0.55f));
        view->FillEllipse(center, side * 0.1f, side * 0.1f);
        view->ConstrainClippingRegion(nullptr);
    } else {
        // small sizes: the disc as a ring in the cartridge, which no floppy has
        BPoint center(left + side * 0.46f, top + side * 0.56f);
        float radius = side * 0.33f;
        view->SetHighColor(shutter);
        view->FillEllipse(center, radius, radius);
        view->SetHighColor(body);
        view->FillEllipse(center, std::max(1.0f, side * 0.1f), std::max(1.0f, side * 0.1f));
    }
    view->PopState();
}

void DrawProgressPie(BView* view, BRect rect, float fraction, rgb_color color)
{
    view->PushState();
    view->SetHighColor(color);
    view->SetPenSize(1.2f);
    view->StrokeEllipse(rect);
    fraction = std::max(0.0f, std::min(1.0f, fraction));
    if (fraction > 0) {
        BRect inner = rect.InsetByCopy(2, 2);
        // arcs run counter-clockwise from three o'clock: start at twelve and go clockwise
        view->FillArc(inner, 90 - 360 * fraction, 360 * fraction);
    }
    view->PopState();
}

void DrawArtPlaceholder(BView* view, BRect rect)
{
    FillVerticalGradient(view, rect, theme::Colors().artPlaceholderTop, theme::Colors().artPlaceholderBottom);
    view->SetHighColor(theme::Colors().artPlaceholderBorder);
    view->StrokeRect(rect);
    BRect note = rect.InsetByCopy(rect.Width() * 0.25f, rect.Height() * 0.25f);
    DrawNoteIcon(view, note, theme::Colors().artPlaceholderNote);
}

BRect FittedRect(const BBitmap* bitmap, BRect rect)
{
    if (!bitmap)
        return rect;
    BRect bounds = bitmap->Bounds();
    float scale = std::min((rect.Width() + 1) / (bounds.Width() + 1), (rect.Height() + 1) / (bounds.Height() + 1));
    float w = (bounds.Width() + 1) * scale;
    float h = (bounds.Height() + 1) * scale;
    BRect dest(0, 0, w - 1, h - 1);
    dest.OffsetTo(rect.left + (rect.Width() + 1 - w) / 2, rect.top + (rect.Height() + 1 - h) / 2);
    return dest;
}

void DrawBitmapFitted(BView* view, const BBitmap* bitmap, BRect rect)
{
    if (!bitmap) {
        DrawArtPlaceholder(view, rect);
        return;
    }
    view->SetDrawingMode(B_OP_COPY);
    view->DrawBitmap(bitmap, bitmap->Bounds(), FittedRect(bitmap, rect), B_FILTER_BITMAP_BILINEAR);
}

namespace {

// How much of a blurred edge at 0 covers the point `x` pixels inside it (negative: outside).
float EdgeCover(float x, float sigma)
{
    return 0.5f * (1.0f + erff(x / (sigma * (float)M_SQRT2)));
}

// The shadow of a sheet of the given size as a picture: black, with the blur in its alpha.
// The pictures are kept, a grid draws the same few sizes over and over.
const BBitmap* ShadowBitmap(int width, int height, int blur, uint8 strength)
{
    // never deleted: at exit the connection to the app_server, which a bitmap needs to go
    // away, is closed before static objects are destroyed
    static auto& cache = *new std::map<std::tuple<int, int, int, int>, std::unique_ptr<BBitmap>>;
    auto key = std::make_tuple(width, height, blur, (int)strength);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second.get();
    if (cache.size() >= 24)
        cache.clear();
    int w = width + 2 * blur, h = height + 2 * blur;
    std::unique_ptr<BBitmap> bitmap(new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGBA32));
    if (bitmap->InitCheck() != B_OK)
        return nullptr;
    float sigma = blur / 2.6f;
    std::vector<float> across(w), down(h);
    for (int x = 0; x < w; x++)
        across[x] = EdgeCover(x + 0.5f - blur, sigma) - EdgeCover(x + 0.5f - blur - width, sigma);
    for (int y = 0; y < h; y++)
        down[y] = EdgeCover(y + 0.5f - blur, sigma) - EdgeCover(y + 0.5f - blur - height, sigma);
    uint8* bits = (uint8*)bitmap->Bits();
    int32 rowBytes = bitmap->BytesPerRow();
    for (int y = 0; y < h; y++) {
        uint8* pixel = bits + y * rowBytes;
        for (int x = 0; x < w; x++, pixel += 4) {
            pixel[0] = pixel[1] = pixel[2] = 0;
            pixel[3] = (uint8)(strength * across[x] * down[y] + 0.5f);
        }
    }
    const BBitmap* result = bitmap.get();
    cache[key] = std::move(bitmap);
    return result;
}

} // namespace

void DrawSoftShadow(BView* view, BRect rect, float blur, float drop, uint8 strength)
{
    int width = (int)floorf(rect.Width() + 1.5f), height = (int)floorf(rect.Height() + 1.5f);
    int spread = std::max(1, (int)ceilf(blur));
    if (width < 1 || height < 1)
        return;
    const BBitmap* shadow = ShadowBitmap(width, height, spread, strength);
    if (!shadow)
        return;
    view->PushState();
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
    view->DrawBitmap(shadow, BPoint(floorf(rect.left) - spread, floorf(rect.top) - spread + drop));
    view->PopState();
}

BRect DrawArtworkOnPage(BView* view, const BBitmap* bitmap, BRect rect, bool artist, float blur, float drop)
{
    BRect shown = FittedRect(bitmap, rect);
    shown = BRect(floorf(shown.left), floorf(shown.top), ceilf(shown.right), ceilf(shown.bottom));
    DrawSoftShadow(view, shown, blur, drop);
    view->PushState();
    if (bitmap)
        DrawBitmapFitted(view, bitmap, rect);
    else if (artist) {
        FillVerticalGradient(view, shown, theme::Colors().artPlaceholderTop, theme::Colors().artPlaceholderBottom);
        icons::DrawFitted(view, icons::kUser, shown.InsetByCopy(shown.Width() * 0.22f, shown.Height() * 0.22f),
            theme::Colors().artPlaceholderNote);
    } else
        DrawArtPlaceholder(view, shown);
    // a hairline holds a picture together on the page: a pale one on white, a dark one on black
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
    view->SetHighColor(theme::Colors().artHairline);
    view->StrokeRect(shown);
    view->PopState();
    return shown;
}

BString TruncateToWidth(const BView* view, const char* text, float width)
{
    BString result(text);
    BFont font;
    view->GetFont(&font);
    font.TruncateString(&result, B_TRUNCATE_END, width);
    return result;
}

void DrawTruncated(BView* view, const char* text, BRect rect, alignment align, float inset)
{
    if (!text || !*text)
        return;
    BString truncated = TruncateToWidth(view, text, rect.Width() - inset * 2);
    font_height fh;
    view->GetFontHeight(&fh);
    float y = rect.top + (rect.Height() - (fh.ascent + fh.descent)) / 2 + fh.ascent;
    float x = rect.left + inset;
    float width = view->StringWidth(truncated.String());
    if (align == B_ALIGN_RIGHT)
        x = rect.right - inset - width;
    else if (align == B_ALIGN_CENTER)
        x = rect.left + (rect.Width() - width) / 2;
    view->SetDrawingMode(B_OP_OVER);
    view->DrawString(truncated.String(), BPoint(x, y));
}

} // namespace amp
