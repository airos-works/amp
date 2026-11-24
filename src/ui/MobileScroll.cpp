#include "MobileScroll.h"
#include <Message.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <View.h>
#include <algorithm>
#include <cmath>

namespace amp {

namespace {
// At fractional desktop scales, app_server can copy the existing pixels exactly
// only for certain logical offsets. Query once at gesture start. Both APIs
// are available on beta6 as well as air/OS.
int ScrollStep(BView* view)
{
    BScreen screen(view->Window());
    display_mode mode;
    if (!screen.IsValid() || screen.GetMode(&mode) != B_OK)
        return 1;
    float width = screen.Frame().Width() + 1;
    for (int step = 1; step <= 4; step++) {
        float scale = roundf(mode.virtual_width * step / width) / step;
        if (scale >= 1 && fabsf(mode.virtual_width / scale - width) <= 1)
            return step;
    }
    return 1;
}
}

void MobileScroll::Reset()
{
    fTracking = fDragging = false;
    fRemainder = 0;
    fStep = 1;
}

void MobileScroll::Begin(BView* view, float screenY)
{
    Reset();
    fTracking = true;
    fLastY = screenY;
    fStep = ScrollStep(view);
}

bool MobileScroll::Drag(BView* view, float screenY, float slop)
{
    if (!fTracking)
        return false;
    float delta = fLastY - screenY;
    if (!fDragging && fabsf(delta) <= slop)
        return false;
    fDragging = true;
    fLastY = screenY;
    Move(view, delta, fStep);
    return true;
}

bool MobileScroll::End(BView* view, float screenY, float slop)
{
    if (!fTracking)
        return false;
    bool tapped = !Drag(view, screenY, slop);
    Move(view, 0, 1);
    Reset();
    return tapped;
}

void MobileScroll::Move(BView* view, float delta, int step)
{
    BScrollBar* bar = view->ScrollBar(B_VERTICAL);
    if (!bar)
        return;
    float min, max;
    bar->GetRange(&min, &max);
    float current = bar->Value();
    float distance = std::clamp(fRemainder + delta, min - current, max - current);
    // Keep slow motion pixel-precise. Carry the unused fraction across events.
    float movement = distance;
    if (distance > min - current && distance < max - current && fabsf(delta) >= step)
        movement = truncf(movement / step) * step;
    bar->SetValue(current + movement);
    fRemainder = distance - (bar->Value() - current);
}

bool MobileScroll::Wheel(BView* view, const BMessage* message)
{
    float delta;
    if (message->what != B_MOUSE_WHEEL_CHANGED
        || message->FindFloat("be:wheel_pixel_delta_y", &delta) != B_OK)
        return false;
    // A queued move/release from the pointer may still arrive during the glide.
    fTracking = fDragging = false;
    // air/OS wheel phases: older Haiku SDKs do not name them.
    const int32 began = 1, ended = 3;
    int32 phase = message->GetInt32("be:wheel_phase", 0);
    if (phase == began) {
        fRemainder = 0;
        fStep = ScrollStep(view);
    }
    Move(view, delta, phase == ended ? 1 : fStep);
    if (phase == ended)
        Reset();
    return true;
}

} // namespace amp
