// Amp's Quick Look add-on: Tracker's space bar plays a song with Amp's own decoder and
// output, shows its artwork and tags, and lets the user scrub through it. Built from the
// same sources as the application and shipped in its package, so the player exists once.
#include "player/AudioOutput.h"
#include "player/LocalDecoder.h"

#include <add-ons/quick_look/QuickLook.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <DataIO.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
#include <Slider.h>
#include <StringView.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>
#include <Window.h>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tvariant.h>
#include <taglib/audioproperties.h>

#include <algorithm>
#include <string>
#include <strings.h>

namespace {

const uint32 kMsgPlayPause = 'plps';
const uint32 kMsgSeek = 'seek';
const uint32 kMsgScrub = 'scrb';
const uint32 kMsgPulse = 'puls';
const uint32 kMsgOpenInAmp = 'oamp';
const bigtime_t kPulseInterval = 250000;
const float kArtSize = 240;
const char* const kAmpSignature = "application/x-vnd.Amp";

std::string FormatTime(int64_t ms)
{
    int seconds = (int)(ms / 1000);
    char buffer[32];
    if (seconds >= 3600)
        snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    else
        snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
    return buffer;
}

BBitmap* BitmapFromData(const void* data, size_t size)
{
    BMemoryIO io(data, size);
    BBitmapStream stream;
    if (BTranslatorRoster::Default()->Translate(&io, nullptr, nullptr, &stream, B_TRANSLATOR_BITMAP) != B_OK)
        return nullptr;
    BBitmap* bitmap = nullptr;
    if (stream.DetachBitmap(&bitmap) != B_OK)
        return nullptr;
    return bitmap;
}

// Album art next to the song, as Amp's scanner finds it.
BBitmap* FolderArt(const entry_ref& ref)
{
    BEntry entry(&ref);
    BEntry parent;
    if (entry.GetParent(&parent) != B_OK)
        return nullptr;
    BPath folder(&parent);
    static const char* names[] = {"cover.jpg", "cover.png", "folder.jpg", "folder.png",
        "Cover.jpg", "Folder.jpg", "front.jpg", "album.jpg", "AlbumArt.jpg", nullptr};
    for (int i = 0; names[i]; i++) {
        BPath path(folder.Path(), names[i]);
        BBitmap* bitmap = BTranslationUtils::GetBitmap(path.Path());
        if (bitmap)
            return bitmap;
    }
    return nullptr;
}

class ArtView : public BView {
public:
    // The picture is scaled to the view, so a resize redraws all of it.
    ArtView() : BView("art", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE) { SetViewColor(B_TRANSPARENT_COLOR); }
    void SetBitmap(BBitmap* bitmap) { fBitmap = bitmap; Invalidate(); }
    void Draw(BRect) override
    {
        BRect bounds = Bounds();
        SetHighColor(Parent()->ViewColor());
        FillRect(bounds);
        if (fBitmap) {
            BRect source = fBitmap->Bounds();
            float scale = std::min((bounds.Width() + 1) / (source.Width() + 1), (bounds.Height() + 1) / (source.Height() + 1));
            float w = floorf((source.Width() + 1) * scale), h = floorf((source.Height() + 1) * scale);
            BRect dest(0, 0, w - 1, h - 1);
            dest.OffsetTo(floorf((bounds.Width() + 1 - w) / 2), floorf((bounds.Height() + 1 - h) / 2));
            SetDrawingMode(B_OP_ALPHA);
            DrawBitmap(fBitmap, source, dest, B_FILTER_BITMAP_BILINEAR);
            SetDrawingMode(B_OP_COPY);
            SetHighColor(0, 0, 0, 60);
            SetDrawingMode(B_OP_ALPHA);
            StrokeRect(dest);
            SetDrawingMode(B_OP_COPY);
        } else {
            // A grey record sleeve with a note, for songs without a picture.
            BRect sleeve = bounds;
            sleeve.InsetBy(floorf(bounds.Width() * 0.1f), floorf(bounds.Height() * 0.1f));
            SetHighColor(214, 214, 214);
            FillRoundRect(sleeve, 6, 6);
            SetHighColor(170, 170, 170);
            StrokeRoundRect(sleeve, 6, 6);
            BFont font(be_plain_font);
            font.SetSize(sleeve.Height() * 0.45f);
            SetFont(&font);
            SetHighColor(130, 130, 130);
            const char* note = "\xE2\x99\xAA";
            float width = StringWidth(note);
            font_height fh;
            font.GetHeight(&fh);
            DrawString(note, BPoint(sleeve.left + (sleeve.Width() - width) / 2,
                sleeve.top + (sleeve.Height() + fh.ascent - fh.descent) / 2));
        }
    }
private:
    BBitmap* fBitmap = nullptr;
};

class SongPreview : public BQuickLookPreview {
public:
    explicit SongPreview(const entry_ref& ref)
        : BQuickLookPreview("Amp preview"), fRef(ref), fDecoder(fOutput)
    {
        SetViewColor(240, 240, 240);
        SetLowColor(ViewColor());
    }

    ~SongPreview() override
    {
        Stop();
        delete fPulse;
        delete fArt;
    }

    bool Open(BString* error)
    {
        BPath path(&fRef);
        std::string why;
        if (!fDecoder.Open(path.Path(), why)) {
            *error = why.c_str();
            return false;
        }
        fDuration = fDecoder.DurationMs();
        ReadTags(path.Path());
        return true;
    }

    BSize PreferredContentSize() override { return BSize(560, kArtSize + 100); }

    BString Summary() override
    {
        BString summary = fFormat.c_str();
        if (fBitRate > 0) {
            BString rate;
            rate.SetToFormat(" \xE2\x80\xA2 %d kbit/s", fBitRate);
            summary << rate;
        }
        if (fSampleRate > 0) {
            BString rate;
            rate.SetToFormat(" \xE2\x80\xA2 %.1f kHz", fSampleRate / 1000.0);
            summary << rate;
        }
        if (fChannels > 0)
            summary << " \xE2\x80\xA2 " << (fChannels == 1 ? "mono" : fChannels == 2 ? "stereo" : "surround");
        if (fDuration > 0)
            summary << " \xE2\x80\xA2 " << FormatTime(fDuration).c_str();
        return summary;
    }

    void Start() override
    {
        if (fStarted || !fDecoder.IsOpen())
            return;
        fStarted = true;
        fOutput.SetPlaying(true);
        fDecoder.Start();
        UpdateTransport();
    }

    void Stop() override
    {
        if (!fStarted)
            return;
        fStarted = false;
        fDecoder.Stop();
        fOutput.Close();
    }

    void AttachedToWindow() override
    {
        BQuickLookPreview::AttachedToWindow();
        fArtView = new ArtView();
        fArtView->SetBitmap(fArt);
        AddChild(fArtView);

        fTitle = new BStringView("title", fTitleText.c_str());
        BFont bold(be_bold_font);
        bold.SetSize(be_plain_font->Size() * 1.4f);
        fTitle->SetFont(&bold);
        fTitle->SetTruncation(B_TRUNCATE_END);
        fTitle->SetHighColor(30, 30, 30);
        AddChild(fTitle);
        fArtist = new BStringView("artist", fArtistText.c_str());
        BFont medium(be_plain_font);
        medium.SetSize(be_plain_font->Size() * 1.1f);
        fArtist->SetFont(&medium);
        fArtist->SetTruncation(B_TRUNCATE_END);
        fArtist->SetHighColor(70, 70, 70);
        AddChild(fArtist);
        fAlbum = new BStringView("album", fAlbumText.c_str());
        fAlbum->SetFont(&medium);
        fAlbum->SetTruncation(B_TRUNCATE_END);
        fAlbum->SetHighColor(110, 110, 110);
        AddChild(fAlbum);

        fPlay = new BButton("play", "\xE2\x96\xB6", new BMessage(kMsgPlayPause));
        fPlay->SetTarget(this);
        AddChild(fPlay);
        fElapsed = new BStringView("elapsed", "0:00");
        fElapsed->SetHighColor(70, 70, 70);
        AddChild(fElapsed);
        fRemaining = new BStringView("remaining", "");
        fRemaining->SetAlignment(B_ALIGN_RIGHT);
        fRemaining->SetHighColor(70, 70, 70);
        AddChild(fRemaining);
        fSlider = new BSlider("position", nullptr, new BMessage(kMsgSeek), 0, 1000, B_HORIZONTAL);
        fSlider->SetModificationMessage(new BMessage(kMsgScrub));
        fSlider->SetTarget(this);
        fSlider->SetViewColor(ViewColor());
        fSlider->SetLowColor(ViewColor());
        AddChild(fSlider);

        for (BView* view : {(BView*)fTitle, (BView*)fArtist, (BView*)fAlbum, (BView*)fElapsed, (BView*)fRemaining}) {
            view->SetViewColor(ViewColor());
            view->SetLowColor(ViewColor());
        }
        Layout();
        BMessage pulse(kMsgPulse);
        fPulse = new BMessageRunner(BMessenger(this), &pulse, kPulseInterval);
        UpdateTransport();
    }

    void DetachedFromWindow() override
    {
        delete fPulse;
        fPulse = nullptr;
        BQuickLookPreview::DetachedFromWindow();
    }

    void FrameResized(float, float) override { Layout(); }

    void MouseDown(BPoint where) override
    {
        // A double click on the picture opens the song in Amp.
        int32 clicks = 1;
        if (Window() && Window()->CurrentMessage())
            Window()->CurrentMessage()->FindInt32("clicks", &clicks);
        if (clicks >= 2)
            OpenInAmp();
    }

    void MessageReceived(BMessage* message) override
    {
        switch (message->what) {
        case kMsgPulse:
            if (fDecoder.Finished() && fPlaying) {
                fPlaying = false;
                fOutput.SetPlaying(false);
            }
            UpdateTransport();
            break;
        case kMsgPlayPause:
            if (fDecoder.Finished()) {
                fDecoder.Seek(0);
                fDecoder.Start();
                fPlaying = true;
            } else
                fPlaying = !fPlaying;
            fOutput.SetPlaying(fPlaying);
            UpdateTransport();
            break;
        case kMsgScrub:
            fScrubbing = true;
            UpdateTimes((int64_t)(fSlider->Value() / 1000.0 * fDuration));
            break;
        case kMsgSeek:
            fScrubbing = false;
            if (fDuration > 0)
                fDecoder.Seek((int64_t)(fSlider->Value() / 1000.0 * fDuration));
            if (fDecoder.Finished()) {
                fDecoder.Start();
                fPlaying = true;
                fOutput.SetPlaying(true);
            }
            break;
        case kMsgOpenInAmp:
            OpenInAmp();
            break;
        default:
            BQuickLookPreview::MessageReceived(message);
        }
    }

private:
    void ReadTags(const char* path)
    {
        BPath p(path);
        std::string name = p.Leaf() ? p.Leaf() : "";
        size_t dot = name.rfind('.');
        fTitleText = dot == std::string::npos ? name : name.substr(0, dot);
        if (dot != std::string::npos) {
            fFormat = name.substr(dot + 1);
            for (char& c : fFormat)
                c = toupper((unsigned char)c);
        }
        TagLib::FileRef file(path, true, TagLib::AudioProperties::Average);
        if (file.isNull())
            return;
        if (TagLib::Tag* tag = file.tag()) {
            if (!tag->title().isEmpty())
                fTitleText = tag->title().to8Bit(true);
            fArtistText = tag->artist().to8Bit(true);
            fAlbumText = tag->album().to8Bit(true);
            if (tag->year() > 0)
                fAlbumText += (fAlbumText.empty() ? "" : " \xE2\x80\xA2 ") + std::to_string(tag->year());
        }
        if (TagLib::AudioProperties* props = file.audioProperties()) {
            fBitRate = props->bitrate();
            fSampleRate = props->sampleRate();
            fChannels = props->channels();
            if (fDuration <= 0)
                fDuration = props->lengthInMilliseconds();
        }
        TagLib::List<TagLib::VariantMap> pictures = file.complexProperties("PICTURE");
        for (const TagLib::VariantMap& picture : pictures) {
            auto data = picture.find("data");
            if (data == picture.end())
                continue;
            TagLib::ByteVector bytes = data->second.toByteVector();
            if (bytes.isEmpty())
                continue;
            BBitmap* bitmap = BitmapFromData(bytes.data(), bytes.size());
            if (!bitmap)
                continue;
            delete fArt;
            fArt = bitmap;
            auto type = picture.find("pictureType");
            if (type != picture.end() && type->second.toString() == "Front Cover")
                break;
        }
        if (!fArt)
            fArt = FolderArt(fRef);
    }

    void Layout()
    {
        if (!fArtView)
            return;
        BRect bounds = Bounds();
        float margin = 20;
        float art = std::min(kArtSize, bounds.Height() - 2 * margin - 60);
        art = std::max(art, 64.0f);
        fArtView->MoveTo(margin, margin);
        fArtView->ResizeTo(art, art);

        float textLeft = margin + art + margin;
        float textWidth = std::max(40.0f, bounds.Width() - textLeft - margin);
        float y = margin + 8;
        for (BStringView* view : {fTitle, fArtist, fAlbum}) {
            font_height fh;
            view->GetFontHeight(&fh);
            float height = ceilf(fh.ascent + fh.descent + fh.leading);
            view->MoveTo(textLeft, y);
            view->ResizeTo(textWidth, height);
            y += height + 4;
        }

        float controlsTop = bounds.bottom - margin - 32;
        float buttonSize = 32;
        fPlay->MoveTo(textLeft, controlsTop);
        fPlay->ResizeTo(buttonSize + 8, buttonSize);
        font_height fh;
        fElapsed->GetFontHeight(&fh);
        float textHeight = ceilf(fh.ascent + fh.descent);
        float timeWidth = fElapsed->StringWidth("00:00:00") + 4;
        float sliderLeft = textLeft + buttonSize + 8 + 10 + timeWidth + 6;
        float sliderRight = bounds.right - margin - timeWidth - 6;
        fElapsed->MoveTo(textLeft + buttonSize + 8 + 10, controlsTop + (buttonSize - textHeight) / 2);
        fElapsed->ResizeTo(timeWidth, textHeight);
        fRemaining->MoveTo(bounds.right - margin - timeWidth, controlsTop + (buttonSize - textHeight) / 2);
        fRemaining->ResizeTo(timeWidth, textHeight);
        fSlider->MoveTo(sliderLeft, controlsTop + 2);
        fSlider->ResizeTo(std::max(40.0f, sliderRight - sliderLeft), buttonSize - 4);
    }

    void UpdateTransport()
    {
        if (!fPlay)
            return;
        fPlay->SetLabel(fPlaying ? "\xE2\x8F\xB8" : "\xE2\x96\xB6");
        if (!fScrubbing) {
            int64_t position = fDecoder.PositionMs();
            if (fDuration > 0)
                fSlider->SetValue((int32)(position * 1000 / fDuration));
            UpdateTimes(position);
        }
    }

    void UpdateTimes(int64_t position)
    {
        fElapsed->SetText(FormatTime(position).c_str());
        if (fDuration > 0)
            fRemaining->SetText(("-" + FormatTime(std::max<int64_t>(0, fDuration - position))).c_str());
    }

    void OpenInAmp()
    {
        entry_ref ref = fRef;
        BMessage refs(B_REFS_RECEIVED);
        refs.AddRef("refs", &ref);
        be_roster->Launch(kAmpSignature, &refs);
        if (Window())
            Window()->PostMessage(B_QUIT_REQUESTED);
    }

    entry_ref fRef;
    amp::AudioOutput fOutput;
    amp::LocalDecoder fDecoder;
    int64_t fDuration = 0;
    int fBitRate = 0;
    int fSampleRate = 0;
    int fChannels = 0;
    std::string fFormat, fTitleText, fArtistText, fAlbumText;
    BBitmap* fArt = nullptr;
    ArtView* fArtView = nullptr;
    BStringView* fTitle = nullptr;
    BStringView* fArtist = nullptr;
    BStringView* fAlbum = nullptr;
    BStringView* fElapsed = nullptr;
    BStringView* fRemaining = nullptr;
    BButton* fPlay = nullptr;
    BSlider* fSlider = nullptr;
    BMessageRunner* fPulse = nullptr;
    bool fStarted = false;
    bool fPlaying = true;
    bool fScrubbing = false;
};

class AmpAddOn : public BQuickLookAddOn {
public:
    explicit AmpAddOn(image_id image) : BQuickLookAddOn(image) {}

    float Supports(const entry_ref& ref, const char* mimeType) override
    {
        // The formats Amp's library plays; other sound is left to a player with more codecs.
        static const char* kTypes[] = {"audio/mpeg", "audio/x-mpeg", "audio/mp3", "audio/x-mp3",
            "audio/mp4", "audio/x-m4a", "audio/aac", "audio/x-aac", "audio/flac", "audio/x-flac",
            "audio/wav", "audio/x-wav", "audio/vnd.wave", "audio/ogg", "audio/x-ogg", "audio/vorbis",
            "audio/x-vorbis+ogg", "audio/x-vorbis", "audio/aiff", "audio/x-aiff", "audio/x-aifc",
            "audio/x-riff", nullptr};
        for (int i = 0; kTypes[i]; i++) {
            if (strcasecmp(mimeType, kTypes[i]) == 0)
                return 0.8f;
        }
        if (strncmp(mimeType, "audio/", 6) == 0)
            return 0.5f;
        return 0;
    }

    BQuickLookPreview* CreatePreview(const entry_ref& ref, const char*, BString* error) override
    {
        SongPreview* preview = new SongPreview(ref);
        if (!preview->Open(error)) {
            delete preview;
            return nullptr;
        }
        return preview;
    }
};

} // namespace

extern "C" BQuickLookAddOn* instantiate_quick_look_add_on(image_id image)
{
    return new AmpAddOn(image);
}
