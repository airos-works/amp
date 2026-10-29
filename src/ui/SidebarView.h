// Left source list: LIBRARY (Music, Artists, Albums), DEVICES and PLAYLISTS.
#pragma once
#include <String.h>
#include <View.h>
#include <vector>

namespace amp {

struct SidebarPlaylist {
    int64_t id;
    BString name;
};

class SidebarView : public BView {
public:
    SidebarView();
    void SetPlaylists(const std::vector<SidebarPlaylist>& playlists);
    // The DEVICES section: shown while a MiniDisc recorder is connected. A fraction of zero
    // or more draws the progress pie of a running write.
    // `locked`: the disc is write-protected (a lock is drawn).
    void SetMiniDisc(bool connected, const BString& label, bool busy, float fraction, bool locked = false);
    void Select(const char* source, int64_t playlistId);
    const char* SelectedSource() const { return fSelectedSource.String(); }
    int64_t SelectedPlaylist() const { return fSelectedPlaylist; }

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void MouseUp(BPoint where) override;
    void MessageReceived(BMessage* message) override;
    void FrameResized(float width, float height) override;
    void AttachedToWindow() override;
    void WindowActivated(bool active) override { Invalidate(); }

private:
    struct Item {
        BString source;      // "music", "artists", "albums", "minidisc", "playlist" or "" for headers
        BString label;
        int64_t playlistId = 0;
        bool header = false;
        BRect rect;
    };
    void Rebuild();
    void UpdateScrollBar();
    int ItemAt(BPoint where) const;
    void DrawIcon(const Item& item, BRect rect, bool selected);
    void ShowContextMenu(int index, BPoint where);

    std::vector<SidebarPlaylist> fPlaylists;
    bool fMiniDiscConnected = false;
    bool fMiniDiscBusy = false;
    bool fMiniDiscLocked = false;
    float fMiniDiscFraction = -1;
    BString fMiniDiscLabel;
    std::vector<Item> fItems;
    BString fSelectedSource = "music";
    int64_t fSelectedPlaylist = 0;
    int fDropIndex = -1;
    float fContentHeight = 0;
};

} // namespace amp
