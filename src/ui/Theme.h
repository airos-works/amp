// Colours, fonts and drawing helpers that give Amp its iTunes 8 look.
#pragma once
#include <GraphicsDefs.h>
#include <Rect.h>
#include <View.h>
#include <String.h>

namespace amp {

inline rgb_color Rgb(uint8 r, uint8 g, uint8 b, uint8 a = 255) { return {r, g, b, a}; }

namespace theme {
    // toolbar / status bar (brushed unified look)
    const rgb_color kToolbarTop = {236, 236, 236, 255};
    const rgb_color kToolbarBottom = {176, 176, 176, 255};
    const rgb_color kToolbarBorder = {108, 108, 108, 255};
    const rgb_color kToolbarText = {70, 70, 70, 255};
    // sidebar
    const rgb_color kSidebarBackground = {214, 221, 229, 255};
    const rgb_color kSidebarBorder = {160, 168, 178, 255};
    const rgb_color kSidebarHeader = {107, 122, 140, 255};
    const rgb_color kSidebarText = {20, 20, 20, 255};
    const rgb_color kSidebarSelectionTop = {110, 150, 217, 255};
    const rgb_color kSidebarSelectionBottom = {58, 107, 193, 255};
    const rgb_color kSidebarSelectionInactiveTop = {170, 170, 170, 255};
    const rgb_color kSidebarSelectionInactiveBottom = {140, 140, 140, 255};
    // lists
    const rgb_color kListBackground = {255, 255, 255, 255};
    const rgb_color kListStripe = {237, 243, 254, 255};
    const rgb_color kListText = {0, 0, 0, 255};
    const rgb_color kListSecondaryText = {110, 110, 110, 255};
    const rgb_color kListGrid = {214, 214, 214, 255};
    const rgb_color kSelectionTop = {97, 140, 219, 255};
    const rgb_color kSelectionBottom = {54, 105, 197, 255};
    const rgb_color kSelectionInactiveTop = {195, 195, 195, 255};
    const rgb_color kSelectionInactiveBottom = {165, 165, 165, 255};
    const rgb_color kSelectedText = {255, 255, 255, 255};
    const rgb_color kHeaderTop = {250, 250, 250, 255};
    const rgb_color kHeaderBottom = {222, 222, 222, 255};
    const rgb_color kHeaderBorder = {165, 165, 165, 255};
    const rgb_color kHeaderText = {40, 40, 40, 255};
    const rgb_color kHeaderSortedTop = {219, 229, 246, 255};
    const rgb_color kHeaderSortedBottom = {186, 204, 236, 255};
    // LCD display
    const rgb_color kLcdTop = {232, 237, 226, 255};
    const rgb_color kLcdBottom = {200, 210, 190, 255};
    const rgb_color kLcdBorder = {143, 151, 138, 255};
    const rgb_color kLcdText = {40, 45, 38, 255};
    const rgb_color kLcdSecondary = {90, 98, 86, 255};
    const rgb_color kLcdBar = {120, 130, 115, 255};
    const rgb_color kLcdBarBackground = {180, 190, 172, 255};
    // artwork placeholder
    const rgb_color kArtPlaceholderTop = {232, 232, 232, 255};
    const rgb_color kArtPlaceholderBottom = {200, 200, 200, 255};
    const rgb_color kArtPlaceholderNote = {160, 160, 160, 255};

    const float kRowHeight = 18.0f;
    const float kToolbarHeight = 66.0f;
    const float kStatusBarHeight = 24.0f;
    const float kHeaderHeight = 18.0f;
}

void FillVerticalGradient(BView* view, BRect rect, rgb_color top, rgb_color bottom);
void FillRoundGradient(BView* view, BRect rect, float radius, rgb_color top, rgb_color bottom);
void DrawArtPlaceholder(BView* view, BRect rect);
void DrawNoteIcon(BView* view, BRect rect, rgb_color color);
// A MiniDisc cartridge centred in `rect`. `detailed` is the colour picture for sizes above
// ~32 pixels; without it the one-colour glyph is drawn in `body`. Both come from the artwork
// in the resources; without them the cartridge is drawn from shapes, with `shutter` for the
// metal.
void DrawMiniDisc(BView* view, BRect rect, rgb_color body, rgb_color shutter, bool detailed = false);
// A small progress pie (iTunes' sync indicator): a ring filled clockwise from the top.
void DrawProgressPie(BView* view, BRect rect, float fraction, rgb_color color);
void DrawBitmapFitted(BView* view, const BBitmap* bitmap, BRect rect);
// Where DrawBitmapFitted puts the picture: `rect` itself without a bitmap.
BRect FittedRect(const BBitmap* bitmap, BRect rect);
// The shadow a sheet lying on `rect` throws: soft over `blur` pixels, falling `drop` pixels
// below the sheet, `strength` (0-255) at its darkest. Drawn before the sheet itself.
void DrawSoftShadow(BView* view, BRect rect, float blur = 8.0f, float drop = 3.0f, uint8 strength = 135);
// A picture lying on the page (album covers, artist pictures): its soft shadow, the picture
// fitted into `rect` and a hairline that holds a pale picture together. Without a bitmap a
// placeholder takes its place: a person for an `artist`, a note otherwise. Returns where the
// picture went.
BRect DrawArtworkOnPage(BView* view, const BBitmap* bitmap, BRect rect, bool artist = false,
    float blur = 8.0f, float drop = 3.0f);
BString TruncateToWidth(const BView* view, const char* text, float width);
void DrawTruncated(BView* view, const char* text, BRect rect, alignment align = B_ALIGN_LEFT, float inset = 4.0f);
rgb_color Blend(rgb_color a, rgb_color b, float amount);

} // namespace amp
