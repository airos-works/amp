#include "Icons.h"

#include <Bitmap.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <View.h>
#include <OS.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace amp {
namespace icons {

namespace {

// Font Awesome 6 Free, SIL OFL 1.1. The file is shipped in the app's package
// under data/Amp and copied into the user font directory on first use, which is
// the one location the font server rescans on request.
const char* kFontFileName = "FontAwesome6Free-Solid-900.otf";
const char* kFamily = "Font Awesome 6 Free";
const char* kStyle = "Solid";

// Font Awesome 6 Free Solid glyphs (UTF-8).
const char* const kGlyphs[] = {
    "\xef\x81\x8b", // kPlay        U+F04B
    "\xef\x81\x8c", // kPause       U+F04C
    "\xef\x81\x88", // kPrevious    U+F048 backward-step
    "\xef\x81\x91", // kNext        U+F051 forward-step
    "\xef\x81\xb4", // kShuffle     U+F074
    "\xef\x8d\xa3", // kRepeat      U+F363
    "+",            // kPlus        U+002B
    "\xef\x80\x93", // kGear        U+F013
    "\xef\x80\xba", // kList        U+F03A
    "\xef\x80\xa2", // kAlbumList   U+F022 rectangle-list
    "\xef\x80\x8a", // kGrid        U+F00A table-cells
    "\xef\x80\xa8", // kVolume      U+F028 volume-high
    "\xef\x80\x81", // kMusic       U+F001
    "\xef\x80\x87", // kUser        U+F007
    "\xef\x94\x9f", // kAlbum       U+F51F compact-disc
    "\xef\x80\x8d", // kClose       U+F00D xmark
    "\xef\x80\x8c", // kCheck       U+F00C check
    "\xef\x81\xb1", // kWarning     U+F071 triangle-exclamation
    "\xef\x80\xa3", // kLock        U+F023 lock
    "\xef\x81\xbc", // kFolder      U+F07C folder-open
    "\xef\x81\x94", // kChevronRight U+F054 chevron-right
};
static_assert(sizeof(kGlyphs) / sizeof(kGlyphs[0]) == kIconCount, "one glyph per icon");

int fState = 0; // 0 unknown, 1 ready, -1 unavailable

// Copies the font shipped inside the app's package next to the user's other
// fonts; the font server watches that directory and picks the file up.
bool InstallBundledFont(const char* destination)
{
    BPath source;
    if (find_directory(B_SYSTEM_DATA_DIRECTORY, &source) != B_OK)
        return false;
    source.Append("Amp");
    source.Append(kFontFileName);
    BFile in(source.Path(), B_READ_ONLY);
    if (in.InitCheck() != B_OK)
        return false;
    BFile out(destination, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (out.InitCheck() != B_OK)
        return false;
    char buffer[64 * 1024];
    while (true) {
        ssize_t read = in.Read(buffer, sizeof(buffer));
        if (read <= 0)
            return read == 0;
        if (out.Write(buffer, read) != read)
            return false;
    }
}

} // namespace

bool Init()
{
    if (fState != 0)
        return fState > 0;

    BPath fonts;
    if (find_directory(B_USER_NONPACKAGED_FONTS_DIRECTORY, &fonts, true) == B_OK) {
        create_directory(fonts.Path(), 0755);
        BPath destination(fonts);
        destination.Append(kFontFileName);
        BEntry entry(destination.Path());
        if (!entry.Exists())
            InstallBundledFont(destination.Path());
    }

    // the server rescans on request; give it a moment to publish the family
    for (int attempt = 0; attempt < 25; attempt++) {
        update_font_families(false);
        BFont probe;
        if (probe.SetFamilyAndStyle(kFamily, kStyle) == B_OK) {
            fState = 1;
            return true;
        }
        snooze(20000);
    }
    fprintf(stderr, "Amp: the Font Awesome icon font is not available\n");
    fState = -1;
    return false;
}

bool Available()
{
    return Init();
}

void Draw(BView* view, Icon icon, BRect rect, float size, rgb_color color)
{
    if (view == nullptr || !Init())
        return;
    if (icon < 0 || icon >= kIconCount)
        return;

    BFont font;
    font.SetFamilyAndStyle(kFamily, kStyle);
    font.SetSize(size);

    const char* glyph = kGlyphs[icon];
    BRect box;
    font.GetBoundingBoxesAsGlyphs(glyph, 1, B_SCREEN_METRIC, &box);

    // centre the ink, not the em square: Font Awesome glyphs sit on the baseline
    // with different amounts of space above and below
    BPoint pen(rect.left + (rect.Width() - box.Width()) / 2 - box.left,
        rect.top + (rect.Height() - box.Height()) / 2 - box.top);

    BFont previous;
    view->GetFont(&previous);
    drawing_mode mode = view->DrawingMode();
    view->SetFont(&font);
    view->SetHighColor(color);
    view->SetDrawingMode(B_OP_OVER);
    view->DrawString(glyph, pen);
    view->SetDrawingMode(mode);
    view->SetFont(&previous);
}

void DrawFitted(BView* view, Icon icon, BRect rect, rgb_color color, float margin)
{
    float size = rect.Height() - margin * 2;
    if (size <= 0)
        return;
    Draw(view, icon, rect, size, color);
}

// ---- pictures from the resources --------------------------------------------

namespace {

const char* const kPictureNames[] = {"minidisc", "minidisc-glyph"};

// The picture as it is stored, in 32 bits with alpha; nullptr when it is missing.
const BBitmap* Original(Picture picture)
{
    // the bitmaps here and in Sized() are never deleted: at exit the connection to the
    // app_server, which a bitmap needs to go away, is closed before static objects are destroyed
    static BBitmap* originals[2] = {nullptr, nullptr};
    static bool loaded[2] = {false, false};
    if (!loaded[picture]) {
        loaded[picture] = true;
        std::unique_ptr<BBitmap> bitmap(BTranslationUtils::GetBitmap(B_PNG_FORMAT, kPictureNames[picture]));
        if (bitmap && bitmap->InitCheck() == B_OK && bitmap->ColorSpace() != B_RGBA32) {
            std::unique_ptr<BBitmap> converted(new BBitmap(bitmap->Bounds(), B_RGBA32));
            if (converted->InitCheck() == B_OK && converted->ImportBits(bitmap.get()) == B_OK)
                bitmap = std::move(converted);
            else
                bitmap.reset();
        }
        if (bitmap && bitmap->InitCheck() == B_OK)
            originals[picture] = bitmap.release();
        else
            fprintf(stderr, "Amp: the picture \"%s\" is not in the resources\n", kPictureNames[picture]);
    }
    return originals[picture];
}

// The picture `size` pixels wide and high. Every pixel is the average of the area it covers
// in the original: the pictures are several times larger than they are shown, and a filter
// that reads four pixels would skip most of them. With `tint` the colours are replaced.
const BBitmap* Sized(Picture picture, int size, const rgb_color* tint)
{
    static auto& cache = *new std::map<std::tuple<int, int, uint32>, std::unique_ptr<BBitmap>>;
    uint32 colour = tint ? ((uint32)tint->red << 16 | (uint32)tint->green << 8 | tint->blue) | 0x1000000u : 0;
    auto key = std::make_tuple((int)picture, size, colour);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second.get();
    const BBitmap* original = Original(picture);
    if (!original || size < 1)
        return nullptr;
    if (cache.size() >= 48)
        cache.clear();
    int sourceWidth = original->Bounds().IntegerWidth() + 1, sourceHeight = original->Bounds().IntegerHeight() + 1;
    std::unique_ptr<BBitmap> bitmap(new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32));
    if (bitmap->InitCheck() != B_OK)
        return nullptr;
    const uint8* source = (const uint8*)original->Bits();
    int32 sourceRow = original->BytesPerRow();
    uint8* bits = (uint8*)bitmap->Bits();
    int32 rowBytes = bitmap->BytesPerRow();
    double stepX = (double)sourceWidth / size, stepY = (double)sourceHeight / size;
    for (int y = 0; y < size; y++) {
        double top = y * stepY, bottom = std::min((double)sourceHeight, (y + 1) * stepY);
        for (int x = 0; x < size; x++) {
            double left = x * stepX, right = std::min((double)sourceWidth, (x + 1) * stepX);
            double sum[4] = {0, 0, 0, 0}, area = 0;
            for (int sy = (int)top; sy < (int)ceil(bottom); sy++) {
                double high = std::min(bottom, sy + 1.0) - std::max(top, (double)sy);
                for (int sx = (int)left; sx < (int)ceil(right); sx++) {
                    double weight = high * (std::min(right, sx + 1.0) - std::max(left, (double)sx));
                    const uint8* pixel = source + sy * sourceRow + sx * 4;
                    double alpha = pixel[3] / 255.0;
                    for (int c = 0; c < 3; c++)
                        sum[c] += pixel[c] * alpha * weight;
                    sum[3] += alpha * weight;
                    area += weight;
                }
            }
            uint8* out = bits + y * rowBytes + x * 4;
            double alpha = area > 0 ? sum[3] / area : 0;
            for (int c = 0; c < 3; c++)
                out[c] = sum[3] > 0 ? (uint8)std::min(255.0, sum[c] / sum[3] + 0.5) : 0;
            if (tint) {
                out[0] = tint->blue;
                out[1] = tint->green;
                out[2] = tint->red;
            }
            out[3] = (uint8)std::min(255.0, alpha * 255.0 + 0.5);
        }
    }
    const BBitmap* result = bitmap.get();
    cache[key] = std::move(bitmap);
    return result;
}

bool DrawSized(BView* view, Picture picture, BRect rect, const rgb_color* tint)
{
    if (view == nullptr)
        return false;
    int size = (int)floorf(std::min(rect.Width(), rect.Height()) + 1.5f);
    const BBitmap* bitmap = Sized(picture, size, tint);
    if (!bitmap)
        return false;
    view->PushState();
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
    view->DrawBitmap(bitmap, BPoint(floorf(rect.left + (rect.Width() + 1 - size) / 2 + 0.5f),
        floorf(rect.top + (rect.Height() + 1 - size) / 2 + 0.5f)));
    view->PopState();
    return true;
}

} // namespace

bool DrawPicture(BView* view, Picture picture, BRect rect)
{
    return DrawSized(view, picture, rect, nullptr);
}

bool DrawGlyph(BView* view, Picture picture, BRect rect, rgb_color color)
{
    return DrawSized(view, picture, rect, &color);
}

} // namespace icons
} // namespace amp
