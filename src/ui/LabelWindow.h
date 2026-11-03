// Print Label: a preview of a disc label and the Bluetooth label printer to print it on.
#pragma once
#include "LabelRenderer.h"
#include <Window.h>
#include <string>
#include <vector>

class BButton;
class BMenuField;
class BPopUpMenu;
class BStatusBar;
class BStringView;
class BTextControl;

namespace amp {

class LabelPreviewView;

class LabelWindow : public BWindow {
public:
    // Takes ownership of `artwork` (may be null); opens centred on `parent`.
    LabelWindow(BWindow* parent, const std::string& title, const std::string& artist, int64_t durationMs,
        BBitmap* artwork);
    ~LabelWindow();
    void MessageReceived(BMessage* message) override;

private:
    struct Printer {
        std::string address;
        std::string name;
        std::string detail;    // battery and roll, once the printer was asked
    };
    void Render();
    void AddPrinter(const Printer& printer, bool select);
    void RebuildMenu();
    void SelectPrinter(int index);
    void Search();
    void Print();
    void SetStatus(const BString& text, bool error = false);
    void UpdateControls();

    LabelContent fContent;
    BBitmap* fArtwork;
    LabelPreviewView* fPreview;
    BTextControl* fTitle;
    BTextControl* fArtist;
    BPopUpMenu* fPrinterMenu;
    BMenuField* fPrinterField;
    BPopUpMenu* fCopiesMenu;
    BPopUpMenu* fDensityMenu;
    BStringView* fStatus;
    BStatusBar* fProgress;
    BButton* fPrintButton;
    std::vector<Printer> fPrinters;
    int fSelected = -1;
    bool fSearching = false;
    bool fPrinting = false;
    bool fStatusError = false;
    niimbot::Raster fRaster;
};

} // namespace amp
