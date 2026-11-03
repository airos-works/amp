// Print Label…: what goes on a disc label for an album, a playlist or the MiniDisc in the
// recorder, then the LabelWindow.
#include "MainWindow.h"
#include "App.h"
#include "LabelWindow.h"
#include "core/NetMD.h"

#include <Bitmap.h>
#include <TranslationUtils.h>

#include <map>

namespace amp {

namespace {

struct LabelSource {
    std::string title;
    std::string artist;
    int64_t durationMs = 0;
    ArtKey art;
};

// Title, artist, time and artwork of a set of songs: the artist they share (or Various
// Artists), the artwork of the album most of them come from.
LabelSource FromTracks(const std::vector<int64_t>& trackIds, const std::string& title)
{
    LabelSource source;
    source.title = title;
    Library& library = App()->GetLibrary();
    Library::Locker locker(library);
    std::map<ArtKey, int> artCounts;
    bool sameArtist = true;
    for (int64_t id : trackIds) {
        const Track* track = library.TrackById(id);
        if (!track)
            continue;
        source.durationMs += track->durationMs;
        const std::string& artist = track->groupingArtist();
        if (source.artist.empty() && sameArtist)
            source.artist = artist;
        else if (artist != source.artist)
            sameArtist = false;
        if (!track->art.empty())
            artCounts[track->art]++;
    }
    if (!sameArtist)
        source.artist = "Various Artists";
    int best = 0;
    for (const auto& [key, count] : artCounts)
        if (count > best) {
            best = count;
            source.art = key;
        }
    return source;
}

// The library album or playlist a MiniDisc was written from, going by its title: Amp names
// a disc "Artist - Album" or after the playlist.
bool FromDiscTitle(const std::string& title, LabelSource& source)
{
    Library& library = App()->GetLibrary();
    std::vector<int64_t> tracks;
    std::string name;
    {
        Library::Locker locker(library);
        for (int64_t id : library.AllAlbumIds()) {
            const Album* album = library.AlbumById(id);
            if (!album)
                continue;
            std::string full = album->artist.empty() ? album->name : album->artist + " - " + album->name;
            if (netmd::SanitizeTitle(full) == title || netmd::SanitizeTitle(album->name) == title) {
                tracks = album->trackIds;
                name = album->name;
                break;
            }
        }
        if (tracks.empty()) {
            for (int64_t id : library.AllPlaylistIds()) {
                const Playlist* playlist = library.PlaylistById(id);
                if (playlist && netmd::SanitizeTitle(playlist->name) == title) {
                    tracks = playlist->trackIds;
                    name = playlist->name;
                    break;
                }
            }
        }
    }
    if (tracks.empty())
        return false;
    source = FromTracks(tracks, name);
    return true;
}

} // namespace

void MainWindow::OpenLabelWindow(BMessage* request)
{
    std::string kind = request->GetString("kind", "");
    LabelSource source;
    if (kind == "playlist") {
        std::vector<int64_t> tracks;
        std::string name;
        {
            Library& library = App()->GetLibrary();
            Library::Locker locker(library);
            if (const Playlist* playlist = library.PlaylistById(request->GetInt64("playlist", 0))) {
                tracks = playlist->trackIds;
                name = playlist->name;
            }
        }
        source = FromTracks(tracks, name);
    } else if (kind == "minidisc") {
        MiniDiscState state = App()->MiniDisc().State();
        const netmd::DiscInfo& disc = state.disc;
        if (!FromDiscTitle(disc.title, source))
            source.title = disc.title;
        // the disc's own time: what was written is what is on the label
        int64_t frames = 0;
        for (const netmd::TrackInfo& track : disc.tracks)
            frames += track.frames;
        if (frames == 0)
            frames = disc.usedFrames;
        if (frames > 0)
            source.durationMs = frames * 1000 / netmd::kFramesPerSecond;
    } else {
        std::vector<int64_t> tracks;
        int64_t id;
        for (int32 i = 0; request->FindInt64("tracks", i, &id) == B_OK; i++)
            tracks.push_back(id);
        source = FromTracks(tracks, request->GetString("name", ""));
        if (request->HasString("artist"))
            source.artist = request->GetString("artist", "");
    }

    BBitmap* artwork = nullptr;
    if (!source.art.empty()) {
        std::string path = App()->Images().PathFor(source.art);
        if (!path.empty())
            artwork = BTranslationUtils::GetBitmap(path.c_str());
    }
    (new LabelWindow(this, source.title, source.artist, source.durationMs, artwork))->Show();
}

} // namespace amp
