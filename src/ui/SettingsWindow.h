// Settings: library folders, artwork sources.
#pragma once
#include "core/Settings.h"
#include <Button.h>
#include <CheckBox.h>
#include <FilePanel.h>
#include <ListView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>
#include <vector>

namespace amp {

class SettingsWindow : public BWindow {
public:
    explicit SettingsWindow(BWindow* parent);
    ~SettingsWindow();
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;

private:
    void Apply();
    void RefreshFolders();
    void RefreshSources();

    SettingsData fData;
    BListView* fFolders;
    BFilePanel* fFolderPanel = nullptr;
    BCheckBox* fOnlineArt;
    BListView* fSources;
    std::vector<BCheckBox*> fSourceChecks;
    BTextControl* fAudioDbKey;
    BTextControl* fDiscogsToken;
    BStringView* fCacheInfo;
};

} // namespace amp
