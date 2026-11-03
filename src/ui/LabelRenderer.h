// Draws a disc label (artwork, title, artist, total time) as the printer will print it.
#pragma once
#include "core/Niimbot.h"
#include <string>

class BBitmap;

namespace amp {

struct LabelContent {
    std::string title;
    std::string artist;
    int64_t durationMs = 0;
    const BBitmap* artwork = nullptr;   // any size; drawn square, dithered
};

struct LabelSize {
    const char* name;
    int widthMm;
    int heightMm;
    int cols;   // dots across the print head
    int rows;   // dots along the label
};

// The label the B1's usual roll has, which also fits a MiniDisc's face: 50 x 30 mm. The head
// prints 48 mm of it.
const LabelSize& DefaultLabelSize();

niimbot::Raster RenderLabel(const LabelContent& content, const LabelSize& size);

// The raster as a picture for the screen: black dots on the label's white, `scale` pixels per dot.
BBitmap* RasterPreview(const niimbot::Raster& raster, int scale);

} // namespace amp
