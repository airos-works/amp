// Amp's Quick Look add-on: Tracker's space bar plays a song with Amp's own decoder and
// output, shows its artwork and tags, and lets the user scrub through it. Built from the
// same sources as the application and shipped in its package, so the player exists once.
#include "player/AudioOutput.h"
#include "player/LocalDecoder.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <add-ons/quick_look/QuickLook.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <DataIO.h>
#include <GradientLinear.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
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

// Sizes are laid out for a 12 point interface font and grow with it, so the
// panel keeps its proportions on a scaled desktop.
float UiScale()
{
    return std::max(1.0f, be_plain_font->Size() / 12.0f);
}
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

// The play button, the times and the groove with the playhead, drawn the way
// Amp's own toolbar draws them, so the panel and the application look alike.
// A click on the button sends kMsgPlayPause; a drag on the groove sends
// kMsgScrub as it goes and kMsgSeek when the mouse is let go, both with the
// "position" (int64, ms).
class TransportView : public BView {
public:
    TransportView(const BMessenger& target, float scale)
        : BView("transport", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE), fTarget(target), fScale(scale)
    {
        SetViewColor(B_TRANSPARENT_COLOR);
    }

    void SetState(bool playing, int64_t position, int64_t duration)
    {
        bool changed = playing != fPlaying || duration != fDuration;
        fPlaying = playing;
        fDuration = duration;
        // A drag owns the playhead until the mouse is let go.
        if (fTracking != kGroove && position != fPosition) {
            fPosition = position;
            changed = true;
        }
        if (changed)
            Invalidate();
    }

    void Draw(BRect) override
    {
        ComputeRects();
        SetHighColor(Parent()->ViewColor());
        FillRect(Bounds());
        DrawButton();
        DrawGroove();
        DrawTimes();
    }

    void MouseDown(BPoint where) override
    {
        ComputeRects();
        if (fButton.Contains(where)) {
            fTracking = kButton;
            fInside = true;
            Invalidate(fButton.InsetByCopy(-2, -3));
        } else if (fDuration > 0 && GrooveHit().Contains(where)) {
            fTracking = kGroove;
            PositionFrom(where);
            Send(kMsgScrub);
        } else
            return;
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
    }

    void MouseMoved(BPoint where, uint32, const BMessage*) override
    {
        if (fTracking == kButton) {
            bool inside = fButton.Contains(where);
            if (inside != fInside) {
                fInside = inside;
                Invalidate(fButton.InsetByCopy(-2, -3));
            }
        } else if (fTracking == kGroove) {
            PositionFrom(where);
            Send(kMsgScrub);
        }
    }

    void MouseUp(BPoint where) override
    {
        if (fTracking == kButton) {
            fTracking = kNone;
            Invalidate(fButton.InsetByCopy(-2, -3));
            if (fButton.Contains(where))
                fTarget.SendMessage(kMsgPlayPause);
        } else if (fTracking == kGroove) {
            PositionFrom(where);
            fTracking = kNone;
            Send(kMsgSeek);
        }
    }

private:
    enum Tracking { kNone, kButton, kGroove };

    void ComputeRects()
    {
        BRect bounds = Bounds();
        float diameter = floorf(30 * fScale);
        float cy = floorf(bounds.top + bounds.Height() / 2);
        fButton.Set(bounds.left + 1, cy - floorf(diameter / 2), bounds.left + 1 + diameter, cy - floorf(diameter / 2) + diameter);
        BFont small = TimeFont();
        // Room for the longest time the song can show.
        std::string longest = "-" + FormatTime(std::max<int64_t>(fDuration, 10 * 60 * 1000));
        float timeWidth = ceilf(small.StringWidth(longest.c_str()) + 2 * fScale);
        float gap = floorf(10 * fScale);
        float half = floorf(9 * fScale);
        fElapsed.Set(fButton.right + gap, cy - half, fButton.right + gap + timeWidth, cy + half);
        fRemaining.Set(bounds.right - timeWidth, cy - half, bounds.right, cy + half);
        float grooveHeight = std::max(3.0f, floorf(5 * fScale));
        float top = cy - floorf(grooveHeight / 2);
        fGroove.Set(fElapsed.right + gap, top, fRemaining.left - gap, top + grooveHeight - 1);
        fKnobRadius = std::max(4.0f, floorf(5 * fScale));
    }

    BFont TimeFont() const
    {
        BFont font(be_plain_font);
        font.SetSize(be_plain_font->Size() * 0.85f);
        return font;
    }

    BRect GrooveHit() const
    {
        return fGroove.InsetByCopy(-fKnobRadius, -(fKnobRadius + 4 * fScale));
    }

    float KnobX() const
    {
        float fraction = fDuration > 0 ? std::min(1.0f, std::max(0.0f, (float)fPosition / (float)fDuration)) : 0.0f;
        return floorf(fGroove.left + fKnobRadius + (fGroove.Width() - 2 * fKnobRadius) * fraction);
    }

    void PositionFrom(BPoint where)
    {
        float span = std::max(1.0f, fGroove.Width() - 2 * fKnobRadius);
        float fraction = (where.x - (fGroove.left + fKnobRadius)) / span;
        fraction = std::min(1.0f, std::max(0.0f, fraction));
        fPosition = (int64_t)(fraction * fDuration);
        Invalidate();
    }

    void Send(uint32 what)
    {
        BMessage message(what);
        message.AddInt64("position", fPosition);
        fTarget.SendMessage(&message);
    }

    void DrawButton()
    {
        BRect rect = fButton;
        bool pressed = fTracking == kButton && fInside;
        rgb_color top = pressed ? amp::Rgb(190, 190, 190) : amp::Rgb(252, 252, 252);
        rgb_color bottom = pressed ? amp::Rgb(150, 150, 150) : amp::Rgb(196, 196, 196);
        SetDrawingMode(B_OP_ALPHA);
        SetHighColor(0, 0, 0, 40);
        FillEllipse(rect.OffsetByCopy(0, 1.5f * fScale));
        SetDrawingMode(B_OP_COPY);
        BGradientLinear gradient(BPoint(rect.left, rect.top), BPoint(rect.left, rect.bottom));
        gradient.AddColor(top, 0);
        gradient.AddColor(bottom, 255);
        FillEllipse(rect, gradient);
        SetHighColor(118, 118, 118);
        SetPenSize(1);
        StrokeEllipse(rect);
        rgb_color ink = amp::Rgb(60, 60, 60);
        if (amp::icons::Available()) {
            amp::icons::Draw(this, fPlaying ? amp::icons::kPause : amp::icons::kPlay, rect, rect.Height() * 0.42f, ink);
            return;
        }
        // Without the icon font: the shapes themselves.
        SetHighColor(ink);
        float cx = floorf(rect.left + rect.Width() / 2), cy = floorf(rect.top + rect.Height() / 2);
        float size = floorf(rect.Height() * 0.2f);
        if (fPlaying) {
            float bar = std::max(2.0f, floorf(size * 0.6f));
            FillRect(BRect(cx - size * 0.9f, cy - size, cx - size * 0.9f + bar, cy + size));
            FillRect(BRect(cx + size * 0.9f - bar, cy - size, cx + size * 0.9f, cy + size));
        } else {
            FillTriangle(BPoint(cx - size * 0.8f, cy - size), BPoint(cx - size * 0.8f, cy + size), BPoint(cx + size * 1.1f, cy));
        }
    }

    void DrawGroove()
    {
        // A slim groove with the played part filled and a small round playhead.
        BRect bar = fGroove;
        float radius = bar.Height() / 2;
        rgb_color track = amp::Rgb(204, 204, 204);
        SetHighColor(track);
        FillRoundRect(bar, radius, radius);
        SetHighColor(amp::Blend(track, amp::Rgb(0, 0, 0), 0.22f));
        StrokeLine(BPoint(bar.left + 2, bar.top), BPoint(bar.right - 2, bar.top));
        float knobX = KnobX();
        if (fDuration > 0 && knobX > bar.left + 2) {
            BRect done(bar.left, bar.top, knobX, bar.bottom);
            amp::FillRoundGradient(this, done, radius, amp::Rgb(150, 150, 150), amp::Rgb(112, 112, 112));
        }
        SetHighColor(amp::Rgb(150, 150, 150));
        StrokeRoundRect(bar, radius, radius);
        if (fDuration <= 0)
            return;
        float cy = bar.top + floorf(bar.Height() / 2);
        float r = fKnobRadius;
        BRect knob(knobX - r, cy - r, knobX + r, cy + r);
        SetDrawingMode(B_OP_ALPHA);
        SetHighColor(0, 0, 0, 55);
        FillEllipse(knob.OffsetByCopy(0, 1));
        SetDrawingMode(B_OP_COPY);
        BGradientLinear gradient(knob.LeftTop(), knob.LeftBottom());
        gradient.AddColor(amp::Rgb(255, 255, 255), 0);
        gradient.AddColor(amp::Rgb(214, 214, 214), 255);
        FillEllipse(knob, gradient);
        SetHighColor(amp::Rgb(120, 120, 120));
        StrokeEllipse(knob);
    }

    void DrawTimes()
    {
        BFont small = TimeFont();
        SetFont(&small);
        SetHighColor(amp::Rgb(70, 70, 70));
        std::string elapsed = FormatTime(fPosition);
        std::string remaining = "-" + FormatTime(std::max<int64_t>(0, fDuration - fPosition));
        amp::DrawTruncated(this, elapsed.c_str(), fElapsed, B_ALIGN_RIGHT, 0);
        amp::DrawTruncated(this, remaining.c_str(), fRemaining, B_ALIGN_LEFT, 0);
    }

    BMessenger fTarget;
    float fScale;
    bool fPlaying = false;
    int64_t fPosition = 0;
    int64_t fDuration = 0;
    Tracking fTracking = kNone;
    bool fInside = false;
    BRect fButton, fElapsed, fRemaining, fGroove;
    float fKnobRadius = 5;
};

class SongPreview : public BQuickLookPreview {
public:
    explicit SongPreview(const entry_ref& ref)
        : BQuickLookPreview("Amp preview"), fRef(ref), fDecoder(fOutput), fScale(UiScale())
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

    BSize PreferredContentSize() override { return BSize(floorf(560 * fScale), floorf((kArtSize + 100) * fScale)); }

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

        // The glyphs come from the icon font Amp ships; once per process.
        static bool sIconsReady = amp::icons::Init();
        (void)sIconsReady;
        fTransport = new TransportView(BMessenger(this), fScale);
        AddChild(fTransport);

        for (BView* view : {(BView*)fTitle, (BView*)fArtist, (BView*)fAlbum}) {
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
            break;
        case kMsgSeek:
            fScrubbing = false;
            if (fDuration > 0)
                fDecoder.Seek(message->GetInt64("position", 0));
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
        float margin = floorf(20 * fScale);
        float controlsHeight = floorf(36 * fScale);
        float art = std::min(floorf(kArtSize * fScale), bounds.Height() - 2 * margin - controlsHeight - margin);
        art = std::max(art, floorf(64 * fScale));
        fArtView->MoveTo(margin, margin);
        fArtView->ResizeTo(art, art);

        float textLeft = margin + art + margin;
        float textWidth = std::max(40.0f, bounds.Width() - textLeft - margin);
        float y = margin + floorf(8 * fScale);
        for (BStringView* view : {fTitle, fArtist, fAlbum}) {
            font_height fh;
            view->GetFontHeight(&fh);
            float height = ceilf(fh.ascent + fh.descent + fh.leading);
            view->MoveTo(textLeft, y);
            view->ResizeTo(textWidth, height);
            y += height + 4;
        }

        // The transport row sits at the bottom, from the text's left edge.
        fTransport->MoveTo(textLeft, bounds.bottom - margin - controlsHeight);
        fTransport->ResizeTo(std::max(80.0f, bounds.right - margin - textLeft), controlsHeight);
    }

    void UpdateTransport()
    {
        if (!fTransport)
            return;
        fTransport->SetState(fPlaying, fDecoder.PositionMs(), fDuration);
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
    TransportView* fTransport = nullptr;
    BMessageRunner* fPulse = nullptr;
    float fScale = 1;
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
