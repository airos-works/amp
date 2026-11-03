#include "LabelWindow.h"
#include "App.h"
#include "player/LabelPrinter.h"
#include "player/Messages.h"
#include "Theme.h"

#include <Bitmap.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>

#include <algorithm>
#include <cmath>

namespace amp {

namespace {

enum {
    kMsgTextChanged = 'lbtx', kMsgPrinterChosen = 'lbpc', kMsgSearchPrinters = 'lbsp', kMsgPrintNow = 'lbpn',
    kMsgCopies = 'lbcp', kMsgDensity = 'lbdn'
};


} // namespace

// The label as it will come out: the roll's white label on a grey ground, the printed dots in
// the head's 48 mm across it.
class LabelPreviewView : public BView {
public:
    LabelPreviewView() : BView("preview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
    {
        fScale = std::max(1.0f, roundf(be_plain_font->Size() / 12.0f * 4) / 4);
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    }
    ~LabelPreviewView() { delete fBitmap; }

    void SetRaster(const niimbot::Raster& raster)
    {
        delete fBitmap;
        fBitmap = RasterPreview(raster, 1);
        fCols = raster.cols;
        fRows = raster.rows;
        Invalidate();
    }

    BSize MinSize() override { return BSize(PaperWidth() + 2 * Pad(), fRows * fScale + 2 * Pad()); }
    BSize PreferredSize() override { return MinSize(); }
    BSize MaxSize() override { return BSize(B_SIZE_UNLIMITED, MinSize().height); }

    void Draw(BRect) override
    {
        BRect bounds = Bounds();
        SetHighColor(tint_color(ViewColor(), B_DARKEN_2_TINT));
        FillRoundRect(bounds, 6, 6);
        float paperWidth = PaperWidth(), paperHeight = fRows * fScale;
        BRect paper(0, 0, paperWidth - 1, paperHeight - 1);
        paper.OffsetBy(floorf((bounds.Width() - paperWidth) / 2), Pad());
        SetHighColor(255, 255, 255);
        FillRoundRect(paper, 3 * fScale, 3 * fScale);
        if (fBitmap) {
            BRect print(0, 0, fCols * fScale - 1, paperHeight - 1);
            print.OffsetBy(paper.left + floorf((paperWidth - fCols * fScale) / 2), paper.top);
            SetDrawingMode(B_OP_MIN); // white paper stays, dots darken it
            DrawBitmap(fBitmap, fBitmap->Bounds(), print);
            SetDrawingMode(B_OP_COPY);
        }
    }

private:
    // The label is 50 mm wide, the head prints 48 of it: 400 dots at 8 per millimetre.
    float PaperWidth() const { return fCols * 50 / 48 * fScale; }
    float Pad() const { return 12 * fScale; }

    BBitmap* fBitmap = nullptr;
    int fCols = 384, fRows = 240;
    float fScale;
};

LabelWindow::LabelWindow(BWindow* parent, const std::string& title, const std::string& artist, int64_t durationMs,
    BBitmap* artwork)
    : BWindow(BRect(0, 0, 100, 100), "Print Label", B_TITLED_WINDOW,
        B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
      fArtwork(artwork)
{
    fContent.title = title;
    fContent.artist = artist;
    fContent.durationMs = durationMs;
    fContent.artwork = artwork;

    fPreview = new LabelPreviewView();
    fTitle = new BTextControl("title", "Title:", title.c_str(), nullptr);
    fTitle->SetModificationMessage(new BMessage(kMsgTextChanged));
    fArtist = new BTextControl("artist", "Artist:", artist.c_str(), nullptr);
    fArtist->SetModificationMessage(new BMessage(kMsgTextChanged));

    fPrinterMenu = new BPopUpMenu("No printer");
    fPrinterField = new BMenuField("printer", "Printer:", fPrinterMenu);
    fCopiesMenu = new BPopUpMenu("copies");
    for (int i = 1; i <= 10; i++) {
        BMessage* message = new BMessage(kMsgCopies);
        message->AddInt32("copies", i);
        BMenuItem* item = new BMenuItem(std::to_string(i).c_str(), message);
        item->SetMarked(i == 1);
        fCopiesMenu->AddItem(item);
    }
    BMenuField* copies = new BMenuField("copies", "Copies:", fCopiesMenu);
    SettingsData settings = App()->GetSettings().Get();
    fDensityMenu = new BPopUpMenu("density");
    static const char* kDensityNames[] = {"1 (lightest)", "2", "3 (normal)", "4", "5 (darkest)"};
    for (int i = 1; i <= 5; i++) {
        BMessage* message = new BMessage(kMsgDensity);
        message->AddInt32("density", i);
        BMenuItem* item = new BMenuItem(kDensityNames[i - 1], message);
        item->SetMarked(i == settings.labelDensity);
        fDensityMenu->AddItem(item);
    }
    BMenuField* density = new BMenuField("density", "Darkness:", fDensityMenu);

    fStatus = new BStringView("status", "");
    fStatus->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 30, B_SIZE_UNSET));
    fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    fProgress = new BStatusBar("progress");
    fProgress->SetBarHeight(be_plain_font->Size() * 0.8f);
    fProgress->Hide();
    fPrintButton = new BButton("print", "Print", new BMessage(kMsgPrintNow));
    BButton* close = new BButton("close", "Close", new BMessage(B_QUIT_REQUESTED));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_SPACING)
        .Add(fPreview)
        .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
            .AddTextControl(fTitle, 0, 0, B_ALIGN_RIGHT, 1, 3)
            .AddTextControl(fArtist, 0, 1, B_ALIGN_RIGHT, 1, 3)
            .AddMenuField(fPrinterField, 0, 2, B_ALIGN_RIGHT, 1, 3)
            .AddMenuField(copies, 0, 3)
            .AddMenuField(density, 2, 3)
        .End()
        .Add(fStatus)
        .Add(fProgress)
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(close)
            .Add(fPrintButton)
        .End();
    SetDefaultButton(fPrintButton);

    // The printer printed to last time; a search when there is none.
    if (!settings.labelPrinterAddress.empty())
        AddPrinter({settings.labelPrinterAddress,
            settings.labelPrinterName.empty() ? settings.labelPrinterAddress : settings.labelPrinterName, ""}, true);
    Render();
    UpdateControls();
    if (fPrinters.empty())
        Search();
    else {
        SetStatus("Checking the printer…");
        StartLabelPrinterIdentify(BMessenger(this), fPrinters[0].address);
    }
    ResizeToPreferred();
    if (parent != nullptr)
        CenterIn(parent->Frame());
    else
        CenterOnScreen();
}

LabelWindow::~LabelWindow()
{
    delete fArtwork;
}

void LabelWindow::Render()
{
    fContent.title = fTitle->Text();
    fContent.artist = fArtist->Text();
    fRaster = RenderLabel(fContent, DefaultLabelSize());
    fPreview->SetRaster(fRaster);
}

void LabelWindow::AddPrinter(const Printer& printer, bool select)
{
    int index = -1;
    for (size_t i = 0; i < fPrinters.size(); i++)
        if (fPrinters[i].address == printer.address)
            index = (int)i;
    if (index < 0) {
        index = (int)fPrinters.size();
        fPrinters.push_back(printer);
    } else {
        fPrinters[index].name = printer.name;
        if (!printer.detail.empty())
            fPrinters[index].detail = printer.detail;
    }
    RebuildMenu();
    SelectPrinter(select || fSelected < 0 ? index : fSelected);
}

void LabelWindow::RebuildMenu()
{
    // the printers, then the search
    while (BMenuItem* item = fPrinterMenu->RemoveItem((int32)0))
        delete item;
    for (size_t i = 0; i < fPrinters.size(); i++) {
        BMessage* message = new BMessage(kMsgPrinterChosen);
        message->AddInt32("index", (int32)i);
        fPrinterMenu->AddItem(new BMenuItem(fPrinters[i].name.c_str(), message));
    }
    if (!fPrinters.empty())
        fPrinterMenu->AddSeparatorItem();
    BMenuItem* search = new BMenuItem("Search for Printers", new BMessage(kMsgSearchPrinters));
    search->SetEnabled(!fSearching);
    fPrinterMenu->AddItem(search);
    fPrinterMenu->SetTargetForItems(this);
}

void LabelWindow::SelectPrinter(int index)
{
    fSelected = index;
    for (int32 i = 0; BMenuItem* item = fPrinterMenu->ItemAt(i); i++)
        item->SetMarked(i == index);
    if (index < 0 && fPrinterMenu->Superitem() != nullptr)
        fPrinterMenu->Superitem()->SetLabel(fSearching ? "Searching…" : "No printer");
    UpdateControls();
}

void LabelWindow::Search()
{
    if (fSearching)
        return;
    fSearching = true;
    SetStatus("Searching for Bluetooth label printers…");
    if (BMenuItem* search = fPrinterMenu->FindItem(kMsgSearchPrinters))
        search->SetEnabled(false);
    StartLabelPrinterSearch(BMessenger(this));
}

void LabelWindow::Print()
{
    if (fPrinting || fSelected < 0)
        return;
    int copies = 1, density = 3;
    if (BMenuItem* item = fCopiesMenu->FindMarked())
        copies = item->Message()->GetInt32("copies", 1);
    if (BMenuItem* item = fDensityMenu->FindMarked())
        density = item->Message()->GetInt32("density", 3);
    fPrinting = true;
    fProgress->Reset();
    fProgress->SetMaxValue(1);
    fProgress->Show();
    SetStatus("Connecting to the printer…");
    StartLabelPrint(BMessenger(this), fPrinters[fSelected].address, fRaster, copies, density);
    UpdateControls();
}

void LabelWindow::SetStatus(const BString& text, bool error)
{
    fStatus->SetText(text.String());
    fStatusError = error;
    if (error)
        fStatus->SetHighColor(theme::Colors().error);
    else
        fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
}

void LabelWindow::UpdateControls()
{
    fPrintButton->SetEnabled(!fPrinting && fSelected >= 0);
    fPrinterField->SetEnabled(!fPrinting);
}

void LabelWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case B_COLORS_UPDATED:
            // the error red has a lighter shade for dark panels
            if (fStatusError)
                fStatus->SetHighColor(theme::Colors().error);
            BWindow::MessageReceived(message);
            break;
        case kMsgTextChanged:
            Render();
            break;
        case kMsgPrinterChosen: {
            int index = message->GetInt32("index", -1);
            SelectPrinter(index);
            if (index >= 0) {
                if (!fPrinters[index].detail.empty())
                    SetStatus(fPrinters[index].detail.c_str());
                else {
                    SetStatus("Checking the printer…");
                    StartLabelPrinterIdentify(BMessenger(this), fPrinters[index].address);
                }
            }
            break;
        }
        case kMsgPrintNow:
            Print();
            break;
        case kMsgSearchPrinters:
            Search();
            SelectPrinter(fSelected); // the search item is no choice of printer
            break;
        case kMsgDensity:
            App()->GetSettings().Modify([&](SettingsData& data) {
                data.labelDensity = message->GetInt32("density", 3);
            });
            break;
        case kMsgCopies:
            break;
        case kMsgLabelPrinterFound: {
            Printer printer;
            printer.address = message->GetString("address", "");
            printer.name = message->GetString("name", printer.address.c_str());
            BString detail;
            const char* model = message->GetString("model", "");
            if (model[0] == '\0')
                detail << "This printer is not supported yet; Amp prints to the Niimbot B1.";
            else {
                detail << printer.name.c_str() << " is ready";
                int32 battery = message->GetInt32("battery", -1);
                if (battery >= 0)
                    detail << ", battery " << battery << " of 4";
                if (message->HasInt32("roll_total")) {
                    int32 left = message->GetInt32("roll_total", 0) - message->GetInt32("roll_used", 0);
                    detail << ", " << left << (left == 1 ? " label" : " labels") << " left on the roll";
                }
                detail << ".";
            }
            printer.detail = detail.String();
            AddPrinter(printer, fSelected < 0);
            if (fSelected >= 0 && fPrinters[fSelected].address == printer.address && !fPrinting)
                SetStatus(detail, model[0] == '\0');
            break;
        }
        case kMsgLabelSearchDone: {
            if (message->GetBool("identify")) {
                // asking one printer failed
                if (!fPrinting)
                    SetStatus(message->GetString("error", ""), true);
                break;
            }
            fSearching = false;
            RebuildMenu();
            SelectPrinter(fSelected);
            if (message->HasString("error"))
                SetStatus(message->GetString("error", ""), true);
            else if (fPrinters.empty())
                SetStatus("No label printer found. Switch the printer on and search again.", true);
            else if (fSelected >= 0 && !fPrinters[fSelected].detail.empty())
                SetStatus(fPrinters[fSelected].detail.c_str());
            break;
        }
        case kMsgLabelProgress:
            fProgress->Update(message->GetFloat("fraction", 0) - fProgress->CurrentValue());
            SetStatus(message->GetString("text", ""));
            break;
        case kMsgLabelPrinted: {
            fPrinting = false;
            fProgress->Hide();
            if (message->GetBool("ok")) {
                SetStatus("The label is printed.");
                std::string address = message->GetString("address", "");
                std::string name = message->GetString("name", address.c_str());
                App()->GetSettings().Modify([&](SettingsData& data) {
                    data.labelPrinterAddress = address;
                    data.labelPrinterName = name;
                });
            } else
                SetStatus(message->GetString("error", "The label could not be printed."), true);
            UpdateControls();
            break;
        }
        default:
            BWindow::MessageReceived(message);
    }
}

} // namespace amp
