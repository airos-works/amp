// Where the sound goes: the output button's glyph (after iTunes' AirPlay button, blue while
// a speaker plays), the spinner shown while one connects, and the picker that lists this
// computer and the Sonos rooms with a check mark on the one playing.
#pragma once
#include "core/Sonos.h"
#include "player/Messages.h"
#include <Messenger.h>
#include <String.h>
#include <Window.h>
#include <vector>

namespace amp {

namespace output {
// The blue of an output in use.
rgb_color ActiveColor();
// A screen with a triangle below it, filling the square centred in `rect`.
void DrawGlyph(BView* view, BRect rect, rgb_color color);
// A Sonos speaker: a cabinet with a woofer.
void DrawSpeaker(BView* view, BRect rect, rgb_color color);
// Twelve spokes fading round from `phase` (0..11): something is on its way.
void DrawSpinner(BView* view, BRect rect, int phase, rgb_color color);
const bigtime_t kSpinnerFrame = 80000;
} // namespace output

// What the picker shows; the window keeps one and sends it on every change.
struct OutputChoices {
    std::vector<SonosDevice> devices;
    bool searching = false;
    BString error;                  // the last search failed
    OutputState state = kOutputLocal;
    BString uuid;                   // the room connecting or playing
};

class OutputPicker : public BWindow {
public:
    // Opens below `anchor` (screen coordinates), larger for fingers when `touch`.
    // Choices go to `owner` as kMsgChooseOutput; it closes after a choice, a click
    // elsewhere or Escape, and then sends kMsgOutputPickerClosed.
    OutputPicker(const BMessenger& owner, BRect anchor, bool touch, const OutputChoices& choices);
    ~OutputPicker() override;

    // Send kMsgOutputChoices (see Archive) to change what it shows.
    void SetChoices(const OutputChoices& choices);
    static void Archive(const OutputChoices& choices, BMessage& message);
    static OutputChoices Unarchive(const BMessage& message);

    void WindowActivated(bool active) override;
    void MessageReceived(BMessage* message) override;

private:
    class ListView;
    void FitToContent();

    BMessenger fOwner;
    BRect fAnchor;
    bool fTouch;
    ListView* fList;
};

const uint32 kMsgOutputPickerClosed = 'opcl';
const uint32 kMsgOutputChoices = 'opch';

} // namespace amp
