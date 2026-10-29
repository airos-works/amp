#include "Theme.h"
#include "Icons.h"
#include <Bitmap.h>
#include <Font.h>
#include <GradientLinear.h>
#include <Region.h>
#include <Shape.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace amp {

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
    FillVerticalGradient(view, rect, theme::kArtPlaceholderTop, theme::kArtPlaceholderBottom);
    view->SetHighColor(180, 180, 180);
    view->StrokeRect(rect);
    BRect note = rect.InsetByCopy(rect.Width() * 0.25f, rect.Height() * 0.25f);
    DrawNoteIcon(view, note, theme::kArtPlaceholderNote);
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
        FillVerticalGradient(view, shown, theme::kArtPlaceholderTop, theme::kArtPlaceholderBottom);
        icons::DrawFitted(view, icons::kUser, shown.InsetByCopy(shown.Width() * 0.22f, shown.Height() * 0.22f),
            theme::kArtPlaceholderNote);
    } else
        DrawArtPlaceholder(view, shown);
    // a hairline holds a pale picture together on the white page
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
    view->SetHighColor(0, 0, 0, 46);
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
