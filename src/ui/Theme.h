// Colours, fonts and drawing helpers that give Amp its iTunes 8 look.
#pragma once
#include <GraphicsDefs.h>
#include <Rect.h>
#include <View.h>
#include <String.h>

class BWindow;

namespace amp {

inline rgb_color Rgb(uint8 r, uint8 g, uint8 b, uint8 a = 255) { return {r, g, b, a}; }

namespace theme {
    // Every colour Amp paints with. There are two sets: the light one is the iTunes 8 look,
    // and the dark one is used while the system's panels are dark (Appearance preferences).
    struct Palette {
        // toolbar and status bar (brushed unified look)
        rgb_color toolbarTop, toolbarBottom, toolbarBorder, toolbarText, toolbarIcon;
        rgb_color barHighlight;        // the bright line along a bar's top edge (has alpha)
        rgb_color statusTop, statusBottom;
        // the buttons on the bars: plain, held down, switched on (shuffle, the shown view)
        rgb_color buttonTop, buttonBottom, buttonPressedTop, buttonPressedBottom;
        rgb_color buttonOnTop, buttonOnBottom, buttonBorder, buttonShadow;
        rgb_color buttonGlyph, buttonOnGlyph, buttonDisabledGlyph;
        // the volume slider
        rgb_color sliderTrackTop, sliderTrackBottom, sliderFillTop, sliderFillBottom, sliderBorder;
        rgb_color knobTop, knobBottom, knobBorder;
        // LCD display
        rgb_color lcdTop, lcdBottom, lcdBorder, lcdText, lcdSecondary, lcdBar, lcdBarBackground;
        rgb_color lcdHighlight;        // has alpha
        rgb_color lcdInset;            // the quality pill and the activity tile
        rgb_color lcdButton, lcdButtonPressed, lcdButtonGlyph, lcdKnobTop, lcdKnobBottom, lcdWarning;
        // sidebar
        rgb_color sidebarBackground, sidebarBorder, sidebarHeader, sidebarText, sidebarIcon;
        rgb_color sidebarSelectionTop, sidebarSelectionBottom;
        rgb_color sidebarSelectionInactiveTop, sidebarSelectionInactiveBottom;
        rgb_color dropFill;            // a drop target's wash (has alpha)
        rgb_color accent;              // drop marks, progress pies
        // lists
        rgb_color listBackground, listStripe, listText, listSecondaryText, listGrid, listIcon;
        rgb_color selectionTop, selectionBottom, selectionInactiveTop, selectionInactiveBottom, selectedText;
        rgb_color headerTop, headerBottom, headerBorder, headerText, headerSortedTop, headerSortedBottom;
        // the headers above a page (an artist, the MiniDisc)
        rgb_color pageHeaderTop, pageHeaderBottom, headingText, link;
        // artwork placeholder
        rgb_color artPlaceholderTop, artPlaceholderBottom, artPlaceholderNote, artPlaceholderBorder;
        rgb_color artHairline;         // holds a picture together on the page (has alpha)
        // the MiniDisc capacity bar's groove and its free space
        rgb_color capacityTop, capacityBottom, capacityBorder, capacityFree;
        // states
        rgb_color warning, warningText, success, error, muted, disabledText;
    };

    // True while the system's panels are dark.
    bool IsDark();
    // The set that goes with the system colours now. It follows them as they change: a
    // window redraws itself on B_COLORS_UPDATED (see InvalidateAll) and gets the new set.
    const Palette& Colors();
    // Redraws every view in the window, for B_COLORS_UPDATED.
    void InvalidateAll(BWindow* window);

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
