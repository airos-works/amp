#include "SettingsWindow.h"
#include "App.h"
#include "player/Messages.h"
#include <Box.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringItem.h>
#include <TabView.h>

namespace amp {

namespace {
enum {
    kMsgAddFolder = 'adfo', kMsgRemoveFolder = 'rmfo', kMsgFolderChosen = 'foch', kMsgSave = 'save',
    kMsgCancel = 'cncl', kMsgSourceUp = 'srup', kMsgSourceDown = 'srdn', kMsgToggleSource = 'srtg',
    kMsgClearCache = 'clca', kMsgRescanNow = 'rscn'
};

const char* SourceTitle(const std::string& id)
{
    if (id == "deezer")
        return "Deezer (fast; album covers and artist images, no key needed)";
    if (id == "musicbrainz")
        return "MusicBrainz + Cover Art Archive (album covers)";
    if (id == "theaudiodb")
        return "TheAudioDB (artist images and album covers)";
    if (id == "discogs")
        return "Discogs (needs a personal access token for images)";
    return id.c_str();
}
}

SettingsWindow::SettingsWindow(BWindow* parent)
    : BWindow(BRect(0, 0, 560, 480), "Amp Settings", B_TITLED_WINDOW,
        B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
    fData = App()->GetSettings().Get();
    BTabView* tabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);

    // ---- Library tab
    fFolders = new BListView("folders");
    BScrollView* folderScroll = new BScrollView("folder-scroll", fFolders, 0, false, true);
    BButton* addFolder = new BButton("add", "Add Folder…", new BMessage(kMsgAddFolder));
    BButton* removeFolder = new BButton("remove", "Remove", new BMessage(kMsgRemoveFolder));
    BButton* rescan = new BButton("rescan", "Rescan Now", new BMessage(kMsgRescanNow));
    BView* library = new BView("Library", 0);
    library->SetLayout(new BGroupLayout(B_VERTICAL));
    BLayoutBuilder::Group<>(library, B_VERTICAL, 8)
        .SetInsets(12, 12, 12, 12)
        .Add(new BStringView("hint", "Folders scanned for MP3, MP4/AAC, FLAC, WAV and OGG files:"))
        .Add(folderScroll)
        .AddGroup(B_HORIZONTAL)
            .Add(addFolder)
            .Add(removeFolder)
            .AddGlue()
            .Add(rescan)
        .End();
    tabs->AddTab(library);

    // ---- Artwork tab
    fOnlineArt = new BCheckBox("online", "Look up missing artwork online", nullptr);
    fSources = new BListView("sources");
    BScrollView* sourceScroll = new BScrollView("source-scroll", fSources, 0, false, true);
    BButton* up = new BButton("up", "Move Up", new BMessage(kMsgSourceUp));
    BButton* down = new BButton("down", "Move Down", new BMessage(kMsgSourceDown));
    BButton* toggle = new BButton("toggle", "Enable/Disable", new BMessage(kMsgToggleSource));
    fAudioDbKey = new BTextControl("audiodb", "TheAudioDB API key:", "", nullptr);
    fDiscogsToken = new BTextControl("discogs", "Discogs token:", "", nullptr);
    fCacheInfo = new BStringView("cache", "");
    BButton* clear = new BButton("clear", "Clear Artwork Cache", new BMessage(kMsgClearCache));
    BView* art = new BView("Artwork", 0);
    art->SetLayout(new BGroupLayout(B_VERTICAL));
    BLayoutBuilder::Group<>(art, B_VERTICAL, 8)
        .SetInsets(12, 12, 12, 12)
        .Add(fOnlineArt)
        .Add(new BStringView("hint", "Sources are tried in this order (embedded and folder art always come first):"))
        .Add(sourceScroll)
        .AddGroup(B_HORIZONTAL)
            .Add(up)
            .Add(down)
            .Add(toggle)
            .AddGlue()
        .End()
        .AddGrid(8, 6)
            .Add(fAudioDbKey->CreateLabelLayoutItem(), 0, 0)
            .Add(fAudioDbKey->CreateTextViewLayoutItem(), 1, 0)
            .Add(fDiscogsToken->CreateLabelLayoutItem(), 0, 1)
            .Add(fDiscogsToken->CreateTextViewLayoutItem(), 1, 1)
        .End()
        .AddGroup(B_HORIZONTAL)
            .Add(fCacheInfo)
            .AddGlue()
            .Add(clear)
        .End();
    tabs->AddTab(art);

    BButton* save = new BButton("save", "Save", new BMessage(kMsgSave));
    BButton* cancel = new BButton("cancel", "Cancel", new BMessage(kMsgCancel));
    save->MakeDefault(true);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 8)
        .SetInsets(10, 10, 10, 10)
        .Add(tabs)
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(cancel)
            .Add(save)
        .End();

    // fill in
    fOnlineArt->SetValue(fData.fetchOnlineArt ? B_CONTROL_ON : B_CONTROL_OFF);
    for (const ArtSource& s : fData.artSources) {
        if (s.id == "theaudiodb")
            fAudioDbKey->SetText(s.apiKey == "2" ? "" : s.apiKey.c_str());
        if (s.id == "discogs")
            fDiscogsToken->SetText(s.apiKey.c_str());
    }
    RefreshFolders();
    RefreshSources();
    int64_t bytes = App()->Images().CacheSizeBytes();
    BString cache;
    if (bytes >= 1024 * 1024)
        cache.SetToFormat("Cache size: %.1f MB", bytes / 1048576.0);
    else
        cache.SetToFormat("Cache size: %lld KB", (long long)(bytes / 1024));
    fCacheInfo->SetText(cache.String());
    if (parent) {
        BRect frame = parent->Frame();
        MoveTo(frame.left + 80, frame.top + 60);
    } else
        CenterOnScreen();
}

SettingsWindow::~SettingsWindow()
{
    delete fFolderPanel;
}

void SettingsWindow::RefreshFolders()
{
    while (fFolders->CountItems() > 0)
        delete fFolders->RemoveItem((int32)0);
    for (const std::string& folder : fData.libraryFolders)
        fFolders->AddItem(new BStringItem(folder.c_str()));
}

void SettingsWindow::RefreshSources()
{
    int32 selected = fSources->CurrentSelection();
    while (fSources->CountItems() > 0)
        delete fSources->RemoveItem((int32)0);
    for (const ArtSource& s : fData.artSources) {
        BString label(s.enabled ? "[x] " : "[  ] ");
        label << SourceTitle(s.id);
        fSources->AddItem(new BStringItem(label.String()));
    }
    if (selected >= 0 && selected < fSources->CountItems())
        fSources->Select(selected);
}

bool SettingsWindow::QuitRequested()
{
    return true;
}

void SettingsWindow::Apply()
{
    fData.fetchOnlineArt = fOnlineArt->Value() == B_CONTROL_ON;
    for (ArtSource& s : fData.artSources) {
        if (s.id == "theaudiodb") {
            s.apiKey = fAudioDbKey->Text();
            if (s.apiKey.empty())
                s.apiKey = "2";
        }
        if (s.id == "discogs")
            s.apiKey = fDiscogsToken->Text();
    }
    App()->GetSettings().Update(fData);
    be_app->PostMessage(kMsgSettingsChanged);
}

void SettingsWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgAddFolder:
            if (!fFolderPanel) {
                fFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, B_DIRECTORY_NODE, false,
                    new BMessage(kMsgFolderChosen));
                fFolderPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Add Folder");
            }
            fFolderPanel->Show();
            break;
        case kMsgFolderChosen: {
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) {
                BPath path(&ref);
                std::string folder = path.Path();
                bool exists = false;
                for (const std::string& f : fData.libraryFolders)
                    if (f == folder)
                        exists = true;
                if (!exists)
                    fData.libraryFolders.push_back(folder);
                RefreshFolders();
            }
            break;
        }
        case kMsgRemoveFolder: {
            int32 index = fFolders->CurrentSelection();
            if (index >= 0 && index < (int32)fData.libraryFolders.size()) {
                fData.libraryFolders.erase(fData.libraryFolders.begin() + index);
                RefreshFolders();
            }
            break;
        }
        case kMsgRescanNow:
            Apply();
            be_app->PostMessage(kMsgRescan);
            break;
        case kMsgSourceUp:
        case kMsgSourceDown: {
            int32 index = fSources->CurrentSelection();
            int32 target = message->what == kMsgSourceUp ? index - 1 : index + 1;
            if (index >= 0 && target >= 0 && target < (int32)fData.artSources.size()) {
                std::swap(fData.artSources[index], fData.artSources[target]);
                RefreshSources();
                fSources->Select(target);
            }
            break;
        }
        case kMsgToggleSource: {
            int32 index = fSources->CurrentSelection();
            if (index >= 0 && index < (int32)fData.artSources.size()) {
                fData.artSources[index].enabled = !fData.artSources[index].enabled;
                RefreshSources();
            }
            break;
        }
        case kMsgClearCache:
            App()->Images().ClearAll();
            fCacheInfo->SetText("Cache size: 0 MB");
            break;
        case kMsgSave:
            Apply();
            PostMessage(B_QUIT_REQUESTED);
            break;
        case kMsgCancel:
            PostMessage(B_QUIT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(message);
    }
}

} // namespace amp
