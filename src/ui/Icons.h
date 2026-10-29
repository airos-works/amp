// The glyphs the buttons draw. The shapes come from the bundled Font Awesome 6
// Free font, which is registered with the font server the first time the app runs.
#pragma once
#include <GraphicsDefs.h>
#include <Rect.h>

class BView;

namespace amp {
namespace icons {

enum Icon {
    kPlay,
    kPause,
    kPrevious,
    kNext,
    kShuffle,
    kRepeat,
    kPlus,
    kGear,
    kList,
    kAlbumList,
    kGrid,
    kVolume,
    kMusic,
    kUser,
    kAlbum,
    kClose,
    kCheck,
    kWarning,
    kLock,
    kFolder,
    kChevronRight,
    kIconCount      // the number of glyphs, not one itself
};

// Pictures shipped in the application's resources.
enum Picture {
    kMiniDiscPicture,   // the MiniDisc cartridge in colour
    kMiniDiscGlyph,     // the cartridge as a one-colour mark
};

// Makes the Font Awesome font available (copying it into the user font
// directory on first run) and loads it. Returns whether glyphs can be drawn.
bool Init();
bool Available();

// Draws the glyph centred on its ink inside `rect`, with the font's em size set
// to `size`. The glyph keeps its designed proportions.
void Draw(BView* view, Icon icon, BRect rect, float size, rgb_color color);

// Draws the glyph centred inside `rect` (inset by `margin`), sized to fill it.
void DrawFitted(BView* view, Icon icon, BRect rect, rgb_color color, float margin = 0.0f);

// Draws the picture as large as fits `rect`, centred. False when the resources lack it.
bool DrawPicture(BView* view, Picture picture, BRect rect);
// The same in one colour: the picture lends its shape, `color` fills it.
bool DrawGlyph(BView* view, Picture picture, BRect rect, rgb_color color);

} // namespace icons
} // namespace amp
