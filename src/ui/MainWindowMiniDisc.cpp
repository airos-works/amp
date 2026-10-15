// MainWindow's MiniDisc handling: starting a write (with the questions it may need to ask),
// and showing its progress in the display, the sidebar, the status bar and the disc view.
#include "MainWindow.h"
#include "App.h"
#include "core/NetMD.h"
#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Entry.h>
#include <MessageRunner.h>
#include <NodeInfo.h>
#include <Notification.h>
#include <Roster.h>
#include <set>

namespace amp {

namespace {

BString Quoted(const std::string& text)
{
    BString quoted;
    quoted << "“" << text.c_str() << "”";
    return quoted;
}

BString SongCount(int count)
{
    BString text;
    text << count << (count == 1 ? " song" : " songs");
    return text;
}

// Shows an alert without blocking the window; used for results nobody has to answer.
void Inform(const char* title, const BString& text, alert_type type = B_INFO_ALERT)
{
    BAlert* alert = new BAlert(title, text.String(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, type);
    alert->SetShortcut(0, B_ESCAPE);
    alert->Go(nullptr);
}

// A system notification about a finished MiniDisc job, so the outcome is seen when Amp's
// window is covered or on another workspace (#8). The outcome goes in the body as one
// sentence: the body wraps, the title line is cut short by the panel and a line break is
// shown as a space. It carries Amp's icon, and one replaces the last: a write never stacks
// up behind an erase.
void NotifyMiniDisc(const BString& headline, const BString& detail, bool failed)
{
    BNotification notification(failed ? B_ERROR_NOTIFICATION : B_INFORMATION_NOTIFICATION);
    notification.SetGroup("Amp");
    notification.SetTitle("MiniDisc");
    BString content(headline);
    if (!detail.IsEmpty())
        content << ". " << detail;
    notification.SetContent(content);
    notification.SetMessageID("amp-minidisc");
    app_info info;
    BBitmap icon(BRect(0, 0, 31, 31), B_RGBA32);
    if (be_app->GetAppInfo(&info) == B_OK && BNodeInfo::GetTrackerIcon(&info.ref, &icon, B_LARGE_ICON) == B_OK)
        notification.SetIcon(&icon);
    notification.Send();
}

// The MiniDisc's page in the display.
ToolbarView::Activity MiniDiscActivity()
{
    ToolbarView::Activity activity;
    activity.id = "minidisc";
    activity.kind = ToolbarView::kActivityMiniDisc;
    return activity;
}

} // namespace

void MainWindow::MiniDiscChanged()
{
    MiniDiscManager& manager = App()->MiniDisc();
    MiniDiscState state = manager.State();
    BString label = "MiniDisc";
    if (state.known && state.disc.present && !state.disc.title.empty())
        label = state.disc.title.c_str();
    bool locked = state.known && state.disc.present && state.disc.writeProtected;
    fSidebar->SetMiniDisc(state.connected, label, state.busy, state.busy ? std::max(0.0f, fMDFraction) : -1, locked);
    fMiniDiscView->SetState(state);
    if (fSource == "minidisc") {
        if (!state.connected && !state.busy) {
            SelectSource("music", 0);
            return;
        }
        BString summary;
        if (state.known && state.disc.present) {
            summary << SongCount(state.disc.trackCount);
            if (state.disc.trackCount > 0)
                summary << ", " << netmd::FormatFrames(state.disc.usedFrames).c_str();
            summary << " — " << netmd::FormatFrames(state.disc.leftFrames).c_str() << " free";
        }
        fStatus->SetSummary(summary);
    }
    UpdateMiniDiscButton();
}

void MainWindow::UpdateMiniDiscButton()
{
    MiniDiscManager& manager = App()->MiniDisc();
    MiniDiscState state = manager.State();
    // like iTunes' Burn Disc button: offered where a playlist or an album is showing
    bool writable = fSource == "playlist" || fShownAlbum != 0 || !fDrillTracks.empty();
    fStatus->SetMiniDiscButton(state.connected && writable, !state.busy && !fTrackList->Tracks().empty());
}

void MainWindow::StartMiniDiscWrite(BMessage* request)
{
    MiniDiscManager& manager = App()->MiniDisc();
    if (manager.Busy()) {
        Inform("MiniDisc", "Amp is already writing to the MiniDisc. Wait until it has finished.");
        return;
    }
    MiniDiscState state = manager.State();
    if (!state.connected) {
        Inform("MiniDisc", "No MiniDisc recorder is connected.\n\nConnect a NetMD recorder with USB and insert a recordable disc.");
        return;
    }
    if (!state.known) {
        Inform("MiniDisc", "Amp is still reading the MiniDisc. Try again in a moment.");
        return;
    }
    const netmd::DiscInfo& disc = state.disc;
    if (!disc.present) {
        Inform("MiniDisc", "There is no disc in the MiniDisc recorder.\n\nInsert a recordable MiniDisc and try again.");
        return;
    }
    if (disc.writeProtected) {
        Inform("MiniDisc Write-Protected", "The MiniDisc in the recorder is write-protected, so nothing can be recorded on it.\n\n"
            "Take the disc out, slide the record tab on its edge so the hole is closed, and put it back.", B_WARNING_ALERT);
        return;
    }
    if (!disc.writable) {
        Inform("MiniDisc", "This MiniDisc cannot be recorded on.", B_WARNING_ALERT);
        return;
    }

    // 1. what to write: the message's songs, a playlist, or what the window shows
    Library& library = App()->GetLibrary();
    std::string kind = request->GetString("kind", "songs");
    std::string name = request->GetString("name", "");
    std::vector<int64_t> ids;
    int64 id;
    for (int32 i = 0; request->FindInt64("tracks", i, &id) == B_OK; i++)
        ids.push_back(id);
    int64 playlistId = request->GetInt64("playlist", 0);
    if (kind == "view") {
        ids = fTrackList->Tracks();
        if (fSource == "playlist" && fShownAlbum == 0 && fDrillTracks.empty())
            playlistId = fPlaylistId;
    }
    {
        Library::Locker locker(library);
        if (const Playlist* playlist = playlistId ? library.PlaylistById(playlistId) : nullptr) {
            if (ids.empty())
                ids = playlist->trackIds;
            // the whole playlist, dragged or chosen: the disc takes its name
            if (name.empty() && (kind == "playlist" || kind == "view" || ids == playlist->trackIds))
                name = playlist->name;
        }
    }
    if (ids.empty())
        return;

    // 2. local files only; streamed songs have no file to read
    struct Candidate {
        MiniDiscWriteItem item;
        BString display;
        std::string artist, album, albumArtist;
    };
    std::vector<Candidate> candidates;
    int streamed = 0, missing = 0;
    {
        Library::Locker locker(library);
        for (int64_t trackId : ids) {
            const Track* track = library.TrackById(trackId);
            if (!track)
                continue;
            if (track->isMA()) {
                streamed++;
                continue;
            }
            if (!BEntry(track->uri.c_str()).Exists()) {
                missing++;
                continue;
            }
            Candidate candidate;
            candidate.item.trackId = trackId;
            candidate.item.path = track->uri;
            candidate.item.durationMs = track->durationMs;
            candidate.display = track->title.c_str();
            candidate.artist = track->artist;
            candidate.album = track->album;
            candidate.albumArtist = track->groupingArtist();
            candidates.push_back(candidate);
        }
    }
    if (candidates.empty()) {
        BString text("None of these songs can be written to a MiniDisc.");
        if (streamed)
            text << "\n\nSongs streamed from Music Assistant have no local file for Amp to record.";
        if (missing)
            text << "\n\nThe files of " << SongCount(missing) << " could not be found.";
        Inform("MiniDisc", text, B_WARNING_ALERT);
        return;
    }
    if (streamed || missing) {
        BString text;
        int skipped = streamed + missing;
        text << skipped << " of " << (int)(skipped + candidates.size()) << " songs cannot be written to a MiniDisc";
        if (streamed && !missing)
            text << ": they are streamed from Music Assistant and have no local file.";
        else if (missing && !streamed)
            text << ": their files could not be found.";
        else
            text << ": they are streamed from Music Assistant or their files are missing.";
        text << "\n\nAmp can write the other " << SongCount((int)candidates.size()) << ".";
        BAlert* alert = new BAlert("MiniDisc", text.String(), "Cancel", "Continue", nullptr, B_WIDTH_AS_USUAL, B_INFO_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        if (alert->Go() != 1)
            return;
    }

    // 3. titles: an album keeps plain song titles, a mix shows the artist as well
    bool oneAlbum = true, oneArtist = true;
    for (const Candidate& c : candidates) {
        oneAlbum = oneAlbum && c.album == candidates.front().album && c.albumArtist == candidates.front().albumArtist;
        oneArtist = oneArtist && c.artist == candidates.front().artist;
    }
    if (name.empty() && oneAlbum && !candidates.front().album.empty()) {
        name = candidates.front().album;
        if (!candidates.front().albumArtist.empty())
            name = candidates.front().albumArtist + " - " + name;
    }
    for (Candidate& c : candidates) {
        std::string title = c.display.String();
        if (!oneArtist && !c.artist.empty())
            title = c.artist + " - " + title;
        c.item.title = netmd::SanitizeTitle(title);
    }

    // 4. a disc that already holds songs: erase it, or add after them?
    bool erase = false;
    BString discName = disc.title.empty() ? BString("The MiniDisc") : BString("The MiniDisc ") << Quoted(disc.title);
    std::string jobName = name.empty() ? "songs" : name;
    if (disc.trackCount > 0) {
        BString text;
        text << discName << " already has " << SongCount(disc.trackCount) << " ("
             << netmd::FormatFrames(disc.usedFrames).c_str() << ") on it.\n\n"
             << "Do you want to erase it and write " << (name.empty() ? BString("these songs") : Quoted(name))
             << ", or add the new songs after the ones already on the disc?";
        BAlert* alert = new BAlert("MiniDisc Not Empty", text.String(), "Cancel", "Add to Disc", "Erase and Write",
            B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        alert->SetDefaultButton(alert->ButtonAt(1)); // Return adds; erasing takes a deliberate click
        int32 choice = alert->Go();
        if (choice == 0 || choice < 0)
            return;
        erase = choice == 2;
    }

    // 5. too long for the space there is? offer the songs that fit, in order
    int64_t available = erase ? disc.totalFrames : disc.leftFrames;
    int64_t needed = 0, fitting = 0, fittingMs = 0, totalMs = 0;
    size_t fitCount = 0;
    bool stillFits = true;
    for (const Candidate& c : candidates) {
        int64_t frames = netmd::SPFramesForDuration(c.item.durationMs);
        needed += frames;
        totalMs += c.item.durationMs;
        if (stillFits && fitting + frames <= available) {
            fitting += frames;
            fittingMs += c.item.durationMs;
            fitCount++;
        } else
            stillFits = false;
    }
    if (needed > available) {
        BString what = name.empty() ? BString("These songs") : Quoted(name);
        BString text;
        text << what << " " << (name.empty() ? "are " : "is ") << FormatDuration(totalMs).c_str()
             << " long, but the MiniDisc has room for " << netmd::FormatFrames(available).c_str() << ".";
        if (fitCount == 0) {
            text << "\n\nNot even the first song fits.";
            if (!erase && disc.trackCount > 0)
                text << " Erasing the disc would make room.";
            Inform("MiniDisc Full", text, B_WARNING_ALERT);
            return;
        }
        text << "\n\nThe first " << SongCount((int)fitCount) << " (" << FormatDuration(fittingMs).c_str()
             << ") fit. Do you want to write just those?";
        BAlert* alert = new BAlert("Not Enough Room", text.String(), "Cancel", "Write Songs That Fit", nullptr,
            B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        if (alert->Go() != 1)
            return;
        candidates.resize(fitCount);
    }

    // 6. go
    MiniDiscJob job;
    job.name = jobName;
    job.erase = erase;
    if (!name.empty() && (erase || disc.trackCount == 0))
        job.discTitle = netmd::SanitizeTitle(name);
    fMDTitles.clear();
    std::vector<int64_t> durations;
    for (const Candidate& c : candidates) {
        job.items.push_back(c.item);
        fMDTitles.push_back(c.display);
        durations.push_back(c.item.durationMs);
    }
    if (!manager.Write(job)) {
        Inform("MiniDisc", "The MiniDisc recorder is busy or was disconnected.");
        return;
    }
    fMDJobName = job.name;
    fMDFraction = 0;
    delete fMDClearRunner;
    fMDClearRunner = nullptr;
    fMiniDiscView->BeginWrite(fMDTitles, durations, erase);
    ToolbarView::Activity status = MiniDiscActivity();
    status.headline << "Writing " << (name.empty() ? BString("songs") : Quoted(name)) << " to the MiniDisc";
    status.detail = erase ? "Erasing the disc…" : "Preparing…";
    status.cancelCommand = kMsgMDCancel;
    fToolbar->SetActivity(status);
    fToolbar->ShowPage("minidisc");
    MiniDiscChanged();
}

void MainWindow::MiniDiscProgress(BMessage* message)
{
    int32 phase = message->GetInt32("phase", 0);
    int32 track = message->GetInt32("track", 0);
    int32 count = message->GetInt32("count", 0);
    float fraction = message->GetFloat("fraction", 0);
    float trackFraction = message->GetFloat("track_fraction", 0);
    int32 eta = message->GetInt32("eta", -1);
    bool cancelling = App()->MiniDisc().Cancelling();
    BString song = track >= 1 && track <= (int32)fMDTitles.size() ? fMDTitles[track - 1] : BString(message->GetString("title", ""));

    ToolbarView::Activity status = MiniDiscActivity();
    status.headline << "Writing " << (fMDJobName == "songs" ? BString("songs") : Quoted(fMDJobName)) << " to the MiniDisc";
    status.cancelCommand = cancelling ? 0 : kMsgMDCancel;
    switch (phase) {
        case kMDPreparing:
            status.detail = "Preparing…";
            break;
        case kMDErasing:
            status.detail = "Erasing the disc…";
            break;
        case kMDConverting:
            status.detail << "Converting song " << track << " of " << count << ": " << song;
            status.fraction = fraction;
            break;
        case kMDWriting:
            status.detail << "Song " << track << " of " << count << ": " << song;
            status.fraction = fraction;
            break;
        case kMDFinishing:
            status.detail = "Finishing…";
            status.fraction = 1;
            break;
    }
    if (cancelling)
        status.detail = phase == kMDWriting ? BString("Stopping after ") << Quoted(song.String()) << "…" : BString("Stopping…");
    if (status.fraction >= 0) {
        status.leftLabel << (int)(status.fraction * 100 + 0.5f) << "%";
        if (eta >= 0 && !cancelling) {
            BString remaining("-");
            remaining << FormatDuration((int64_t)eta * 1000).c_str();
            status.rightLabel = remaining;
        }
    }
    fToolbar->SetActivity(status);
    fMDFraction = status.fraction >= 0 ? status.fraction : 0;
    MiniDiscState state = App()->MiniDisc().State();
    BString label = state.known && state.disc.present && !state.disc.title.empty() ? BString(state.disc.title.c_str()) : BString("MiniDisc");
    fSidebar->SetMiniDisc(state.connected, label, true, fMDFraction);
    fMiniDiscView->SetWriteProgress(track, phase, trackFraction);
}

void MainWindow::MiniDiscFinished(BMessage* message)
{
    bool ok = message->GetBool("ok", false);
    BString error = message->GetString("error", "");
    ToolbarView::Activity status = MiniDiscActivity();
    status.done = true;
    if (message->GetBool("erase", false)) {
        status.headline = ok ? "The MiniDisc was erased" : "The MiniDisc could not be erased";
        status.failed = !ok;
        status.detail = ok ? BString("It is empty now.") : error;
        fToolbar->SetActivity(status);
        fToolbar->ShowPage("minidisc"); // the outcome shows even when the song was on show
        ClearMiniDiscStatus(6000000);
        NotifyMiniDisc(status.headline, status.detail, !ok);
        if (!ok)
            Inform("MiniDisc", error, B_WARNING_ALERT);
        MiniDiscChanged();
        return;
    }
    bool cancelled = message->GetBool("cancelled", false);
    bool full = message->GetBool("full", false);
    int32 written = message->GetInt32("written", 0);
    int32 failed = message->GetInt32("failed", 0);
    int32 count = message->GetInt32("count", 0);
    int64 duration = message->GetInt64("duration", 0);
    BString name = fMDJobName == "songs" ? BString("The songs") : Quoted(fMDJobName);
    if (!error.IsEmpty()) {
        status.headline = "Writing to the MiniDisc failed";
        status.failed = true;
    } else if (cancelled)
        status.headline = "Writing to the MiniDisc stopped";
    else if (written == count)
        status.headline << name << (fMDJobName == "songs" ? " are" : " is") << " on the MiniDisc";
    else {
        status.headline = "Writing to the MiniDisc finished";
        status.failed = true;
    }
    status.detail << "Wrote " << (written == count ? SongCount(written) : BString() << written << " of " << SongCount(count));
    if (duration > 0)
        status.detail << " (" << FormatDuration(duration).c_str() << ")";
    fToolbar->SetActivity(status);
    fToolbar->ShowPage("minidisc"); // the outcome shows even when the song was on show
    ClearMiniDiscStatus(10000000);
    NotifyMiniDisc(status.headline, status.detail, status.failed);
    fMiniDiscView->EndWrite();
    fMDFraction = -1;
    fStatus->SetTransient(BString(status.headline) << ". " << status.detail, 10);
    MiniDiscChanged();

    if (!error.IsEmpty()) {
        BString text(error);
        if (written > 0)
            text << "\n\n" << SongCount(written) << " were written before the problem.";
        Inform("MiniDisc", text, B_WARNING_ALERT);
    } else if (failed > 0) {
        BString text;
        text << SongCount(failed) << " could not be converted and " << (failed == 1 ? "was" : "were") << " skipped.";
        BString failure = message->GetString("failure", "");
        if (!failure.IsEmpty())
            text << "\n\n" << failure;
        Inform("MiniDisc", text, B_WARNING_ALERT);
    } else if (full && !cancelled)
        Inform("MiniDisc", BString("The MiniDisc filled up after ") << SongCount(written) << ".", B_WARNING_ALERT);
}

void MainWindow::ConfirmMiniDiscErase()
{
    MiniDiscManager& manager = App()->MiniDisc();
    MiniDiscState state = manager.State();
    if (manager.Busy() || !state.connected || !state.known || !state.disc.present)
        return;
    BString text;
    text << "Erase " << (state.disc.title.empty() ? BString("the MiniDisc") : BString("the MiniDisc ") << Quoted(state.disc.title))
         << "?\n\n"
         << "All " << SongCount(state.disc.trackCount) << " on it will be deleted. This cannot be undone.";
    BAlert* alert = new BAlert("Erase MiniDisc", text.String(), "Cancel", "Erase", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    alert->SetShortcut(0, B_ESCAPE);
    if (alert->Go() != 1)
        return;
    manager.Erase();
    ToolbarView::Activity status = MiniDiscActivity();
    status.headline = "Erasing the MiniDisc";
    status.detail = state.disc.title.empty() ? BString() : Quoted(state.disc.title);
    fToolbar->SetActivity(status);
    fToolbar->ShowPage("minidisc");
    delete fMDClearRunner;
    fMDClearRunner = nullptr;
    MiniDiscChanged();
}

void MainWindow::ClearMiniDiscStatus(bigtime_t after)
{
    delete fMDClearRunner;
    BMessage clear(kMsgMDClearStatus);
    fMDClearRunner = new BMessageRunner(BMessenger(this), &clear, after, 1);
}

} // namespace amp
