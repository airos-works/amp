#include "LabelRenderer.h"
#include "core/Model.h"

#include <Bitmap.h>
#include <Font.h>
#include <String.h>
#include <View.h>

#include <algorithm>
#include <vector>

namespace amp {

namespace {

const int kMargin = 14;        // dots kept clear along every edge (rounded corners, feed slack)
const int kGap = 12;           // between the artwork and the text

// Lines of `text` in `font` no wider than `width`, at most `maxLines`; the last one truncated.
std::vector<BString> WrapText(const BFont& font, const char* text, float width, int maxLines)
{
    std::vector<BString> lines;
    BString rest(text);
    rest.Trim();
    while (rest.Length() > 0 && (int)lines.size() < maxLines) {
        if ((int)lines.size() == maxLines - 1 || font.StringWidth(rest.String()) <= width) {
            BString last(rest);
            font.TruncateString(&last, B_TRUNCATE_END, width);
            lines.push_back(last);
            break;
        }
        // the longest run of words that fits; a single word too long is cut
        int32 cut = -1;
        for (int32 i = 1; i <= rest.Length(); i++) {
            if (i < rest.Length() && rest[i] != ' ')
                continue;
            BString candidate;
            rest.CopyInto(candidate, 0, i);
            if (font.StringWidth(candidate.String()) > width)
                break;
            cut = i;
        }
        BString line;
        if (cut <= 0) {
            line = rest;
            font.TruncateString(&line, B_TRUNCATE_END, width);
            lines.push_back(line);
            break;
        }
        rest.CopyInto(line, 0, cut);
        lines.push_back(line);
        rest.Remove(0, cut);
        rest.Trim();
    }
    return lines;
}

// The biggest size from `largest` down to `smallest` at which the text fits in `maxLines`.
float FitSize(BFont font, const char* text, float width, int maxLines, float largest, float smallest)
{
    for (float size = largest; size > smallest; size -= 1) {
        font.SetSize(size);
        std::vector<BString> lines = WrapText(font, text, width, maxLines + 1);
        if ((int)lines.size() <= maxLines && (lines.empty()
                || font.StringWidth(lines.back().String()) <= width))
            return size;
    }
    return smallest;
}

} // namespace

const LabelSize& DefaultLabelSize()
{
    static const LabelSize size = {"50 × 30 mm", 50, 30, 384, 240};
    return size;
}

niimbot::Raster RenderLabel(const LabelContent& content, const LabelSize& size)
{
    niimbot::Raster raster(size.cols, size.rows);
    const int width = raster.cols, height = raster.rows;

    // Artwork: square, the full height inside the margins, dithered from grey.
    int art = height - 2 * kMargin;
    int textLeft = kMargin;
    if (content.artwork != nullptr) {
        BRect artRect(0, 0, art - 1, art - 1);
        BBitmap canvas(artRect, B_RGB32, true);
        if (canvas.Lock()) {
            BView* view = new BView(artRect, "art", B_FOLLOW_NONE, 0);
            canvas.AddChild(view);
            view->SetDrawingMode(B_OP_COPY);
            view->DrawBitmap(content.artwork, content.artwork->Bounds(), artRect, B_FILTER_BITMAP_BILINEAR);
            view->Sync();
            std::vector<uint8_t> grey((size_t)art * art), dots;
            const uint8* bits = (const uint8*)canvas.Bits();
            int32 stride = canvas.BytesPerRow();
            for (int y = 0; y < art; y++) {
                for (int x = 0; x < art; x++) {
                    const uint8* p = bits + y * stride + x * 4;
                    grey[(size_t)y * art + x] = (uint8_t)((p[2] * 299 + p[1] * 587 + p[0] * 114) / 1000);
                }
            }
            canvas.RemoveChild(view);
            delete view;
            canvas.Unlock();
            niimbot::PrepareGrey(grey, art, art);
            niimbot::Dither(grey, art, art, dots);
            for (int y = 0; y < art; y++)
                for (int x = 0; x < art; x++)
                    if (dots[(size_t)y * art + x])
                        raster.Set(kMargin + x, kMargin + y);
        }
        textLeft = kMargin + art + kGap;
    }

    // Text, drawn in black on white and thresholded: dithered text would look ragged.
    BRect bounds(0, 0, width - 1, height - 1);
    BBitmap canvas(bounds, B_RGB32, true);
    if (!canvas.Lock())
        return raster;
    BView* view = new BView(bounds, "text", B_FOLLOW_NONE, 0);
    canvas.AddChild(view);
    view->SetHighColor(255, 255, 255);
    view->FillRect(bounds);
    view->SetHighColor(0, 0, 0);
    view->SetDrawingMode(B_OP_OVER);

    const float textWidth = width - kMargin - textLeft;
    BFont titleFont(be_bold_font), plainFont(be_plain_font);
    std::string title = content.title.empty() ? std::string("Untitled") : content.title;
    float titleSize = FitSize(titleFont, title.c_str(), textWidth, 3, 30, 17);
    titleFont.SetSize(titleSize);
    std::vector<BString> titleLines = WrapText(titleFont, title.c_str(), textWidth, 3);
    plainFont.SetSize(std::min(22.0f, std::max(17.0f, titleSize - 4)));
    std::vector<BString> artistLines;
    if (!content.artist.empty())
        artistLines = WrapText(plainFont, content.artist.c_str(), textWidth, 2);

    font_height titleHeight, plainHeight;
    titleFont.GetHeight(&titleHeight);
    plainFont.GetHeight(&plainHeight);
    float titleLine = ceilf(titleHeight.ascent + titleHeight.descent);
    float plainLine = ceilf(plainHeight.ascent + plainHeight.descent);

    float y = kMargin + ceilf(titleHeight.ascent) - 2;
    view->SetFont(&titleFont);
    for (const BString& line : titleLines) {
        view->DrawString(line.String(), BPoint(textLeft, y));
        y += titleLine;
    }
    // The total time sits at the bottom, under a thin rule; the artist must end above it.
    float timeBase = height - kMargin - ceilf(plainHeight.descent);
    float rule = content.durationMs > 0 ? timeBase - ceilf(plainHeight.ascent) - 7 : height - kMargin;
    y += 4 - titleLine + ceilf(plainHeight.ascent) + ceilf(titleHeight.descent);
    view->SetFont(&plainFont);
    for (const BString& line : artistLines) {
        if (y + plainHeight.descent > rule - 3)
            break;
        view->DrawString(line.String(), BPoint(textLeft, y));
        y += plainLine;
    }
    if (content.durationMs > 0) {
        BString time(FormatDuration(content.durationMs).c_str());
        view->DrawString(time.String(), BPoint(textLeft, timeBase));
        view->FillRect(BRect(textLeft, rule, width - kMargin - 1, rule + 1));
    }
    view->Sync();

    const uint8* bits = (const uint8*)canvas.Bits();
    int32 stride = canvas.BytesPerRow();
    for (int yy = 0; yy < height; yy++)
        for (int x = textLeft; x < width; x++) {
            const uint8* p = bits + yy * stride + x * 4;
            if ((p[0] + p[1] + p[2]) < 3 * 128)
                raster.Set(x, yy);
        }
    canvas.RemoveChild(view);
    delete view;
    canvas.Unlock();
    return raster;
}

BBitmap* RasterPreview(const niimbot::Raster& raster, int scale)
{
    BBitmap* bitmap = new BBitmap(BRect(0, 0, raster.cols * scale - 1, raster.rows * scale - 1), B_RGB32);
    uint8* bits = (uint8*)bitmap->Bits();
    int32 stride = bitmap->BytesPerRow();
    for (int y = 0; y < raster.rows * scale; y++) {
        uint32* row = (uint32*)(bits + y * stride);
        for (int x = 0; x < raster.cols * scale; x++)
            row[x] = raster.Get(x / scale, y / scale) ? 0xff202020 : 0xffffffff;
    }
    return bitmap;
}

} // namespace amp
