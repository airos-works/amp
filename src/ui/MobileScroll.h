// Pointer drags and air/OS pixel-wheel gestures share one vertical scrollbar.
#pragma once
#include <SupportDefs.h>

class BMessage;
class BView;

namespace amp {

class MobileScroll {
public:
    void Begin(BView* view, float screenY);
    bool Drag(BView* view, float screenY, float slop);
    bool End(BView* view, float screenY, float slop);
    bool TapPending() const { return fTracking && !fDragging; }
    void Reset();
    bool Wheel(BView* view, const BMessage* message);

private:
    void Move(BView* view, float delta, int step);
    bool fTracking = false;
    bool fDragging = false;
    float fLastY = 0;
    float fRemainder = 0;
    int fStep = 1;
};

} // namespace amp
