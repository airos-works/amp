#include "Scanner.h"
#include "ImageCache.h"
#include "Library.h"
#include "TagStream.h"
#include "Workers.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>

#include <taglib/aifffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4properties.h>
#include <taglib/wavfile.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>

namespace amp {

namespace {

const char* kExtensions[] = {".mp3", ".mp4", ".m4a", ".m4b", ".aac", ".flac", ".wav", ".wave", ".ogg", ".oga", ".opus", nullptr};
// cover pictures next to the songs, best first; compared without regard to case
const char* kFolderArt[] = {"cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "front.png",
    "album.jpg", "album.png", "artwork.jpg", "artwork.png", nullptr};

const size_t kBatch = 200;               // tracks written to the library in one transaction
const int64_t kRebuildInterval = 3000;   // ms between two rebuilds of the index while scanning
const int64_t kProgressInterval = 250;   // ms between two progress reports
const int kMostWorkers = 8;

int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Position of `name` in kFolderArt, or -1.
int FolderArtRank(const std::string& name)
{
    std::string lower = ToLower(name);
    for (int i = 0; kFolderArt[i]; i++)
        if (lower == kFolderArt[i])
            return i;
    return -1;
}

bool IsUnder(const std::string& path, const std::string& folder)
{
    if (folder == "/")
        return true;
    return path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/';
}

std::string FolderOf(const std::string& path)
{
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

std::string Extension(const std::string& path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return "";
    return ToLower(path.substr(dot));
}

int ParseLeadingInt(const std::string& text)
{
    return atoi(text.c_str());
}

std::string FirstProperty(const TagLib::PropertyMap& props, const char* key)
{
    auto it = props.find(key);
    if (it == props.end() || it->second.isEmpty())
        return "";
    return it->second.front().to8Bit(true);
}

std::string ReadFileBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

Scanner::Scanner(Library& library, ImageCache& images)
    : fLibrary(library), fImages(images)
{
}

Scanner::~Scanner()
{
    Stop();
}

bool Scanner::IsAudioFile(const std::string& path)
{
    std::string ext = Extension(path);
    for (int i = 0; kExtensions[i]; i++)
        if (ext == kExtensions[i])
            return true;
    return false;
}

bool Scanner::ReadTrack(const std::string& path, Track& track, std::string* embeddedArt, std::string* embeddedMime)
{
    if (!IsAudioFile(path))
        return false;
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return ReadTrack(path, st.st_size, st.st_mtime, track, embeddedArt, embeddedMime);
}

bool Scanner::ReadTrack(const std::string& path, int64_t size, int64_t modified, Track& track,
    std::string* embeddedArt, std::string* embeddedMime, const std::function<bool(const Track&)>& wantArt)
{
    if (!IsAudioFile(path))
        return false;
    track.uri = path;
    track.sizeBytes = size;
    track.modifiedTime = modified;
    TagStream stream(path);
    if (!stream.isOpen())
        return false;
    std::string extension = Extension(path);
    Mp4Info mp4;
    bool isMp4 = (extension == ".m4a" || extension == ".mp4" || extension == ".m4b") && ReadMp4Info(stream, mp4);
    // everything TagLib needs is in the moov box; the fragments behind it hold the sound
    if (isMp4 && mp4.fragmented)
        stream.SetLength(mp4.moovEnd);
    TagLib::FileRef file(&stream, true, TagLib::AudioProperties::Fast);
    if (file.isNull()) {
        // still list the file by name so the user sees it
        size_t slash = path.rfind('/');
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        size_t dot = name.rfind('.');
        track.title = dot == std::string::npos ? name : name.substr(0, dot);
        return true;
    }
    TagLib::Tag* tag = file.tag();
    if (tag) {
        track.title = tag->title().to8Bit(true);
        track.artist = tag->artist().to8Bit(true);
        track.album = tag->album().to8Bit(true);
        track.genre = tag->genre().to8Bit(true);
        track.year = (int)tag->year();
        track.trackNumber = (int)tag->track();
    }
    TagLib::PropertyMap props = file.properties();
    track.albumArtist = FirstProperty(props, "ALBUMARTIST");
    std::string disc = FirstProperty(props, "DISCNUMBER");
    if (!disc.empty())
        track.discNumber = ParseLeadingInt(disc);
    if (track.trackNumber == 0) {
        std::string number = FirstProperty(props, "TRACKNUMBER");
        if (!number.empty())
            track.trackNumber = ParseLeadingInt(number);
    }
    if (track.year == 0) {
        std::string date = FirstProperty(props, "DATE");
        if (date.empty())
            date = FirstProperty(props, "ORIGINALDATE");
        if (!date.empty())
            track.year = ParseLeadingInt(date);
    }
    if (TagLib::AudioProperties* audio = file.audioProperties()) {
        track.durationMs = audio->lengthInMilliseconds();
        track.bitrate = audio->bitrate();
    }
    if (TagLib::File* raw = file.file()) {
        if (dynamic_cast<TagLib::FLAC::File*>(raw) || dynamic_cast<TagLib::RIFF::WAV::File*>(raw)
            || dynamic_cast<TagLib::RIFF::AIFF::File*>(raw))
            track.lossless = true;
        else if (TagLib::MP4::File* mp4 = dynamic_cast<TagLib::MP4::File*>(raw)) {
            TagLib::MP4::Properties* properties = mp4->audioProperties();
            track.lossless = properties && properties->codec() == TagLib::MP4::Properties::ALAC;
        }
    }
    if (isMp4) {
        if (mp4.fragmented || track.durationMs <= 0) {
            track.durationMs = mp4.durationMs;
            track.bitrate = 0; // TagLib saw the file without its sound
        }
        if (mp4.codec == "alac" || mp4.codec == "fLaC")
            track.lossless = true;
    }
    if (track.bitrate <= 0 && track.durationMs > 0)
        track.bitrate = (int)(track.sizeBytes * 8 / track.durationMs); // bytes*8/ms == kbit/s
    if (track.title.empty()) {
        size_t slash = path.rfind('/');
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        size_t dot = name.rfind('.');
        track.title = dot == std::string::npos ? name : name.substr(0, dot);
        // "01 Title" / "01 - Title" file names carry the track number
        size_t digits = 0;
        while (digits < track.title.size() && isdigit((unsigned char)track.title[digits]))
            digits++;
        if (digits > 0 && digits <= 3 && digits < track.title.size()) {
            size_t rest = digits;
            while (rest < track.title.size() && (track.title[rest] == ' ' || track.title[rest] == '-' || track.title[rest] == '.' || track.title[rest] == '_'))
                rest++;
            if (rest < track.title.size()) {
                if (track.trackNumber == 0)
                    track.trackNumber = atoi(track.title.substr(0, digits).c_str());
                track.title = track.title.substr(rest);
            }
        }
    }
    if (track.album.empty() || track.artist.empty()) {
        // untagged formats (raw AAC/ADTS, plain WAV): use "Artist/Album/file" folder names
        size_t slash = path.rfind('/');
        if (slash != std::string::npos) {
            std::string folder = path.substr(0, slash);
            size_t parent = folder.rfind('/');
            std::string albumName = parent == std::string::npos ? folder : folder.substr(parent + 1);
            std::string artistName;
            if (parent != std::string::npos) {
                std::string grand = folder.substr(0, parent);
                size_t gp = grand.rfind('/');
                artistName = gp == std::string::npos ? grand : grand.substr(gp + 1);
            }
            if (track.album.empty() && !albumName.empty())
                track.album = albumName;
            if (track.artist.empty() && !artistName.empty() && artistName != "home" && artistName != "Music")
                track.artist = artistName;
        }
    }
    if (embeddedArt && (!wantArt || wantArt(track))) {
        TagLib::List<TagLib::VariantMap> pictures = file.complexProperties("PICTURE");
        for (const TagLib::VariantMap& picture : pictures) {
            auto data = picture.find("data");
            if (data == picture.end())
                continue;
            TagLib::ByteVector bytes = data->second.toByteVector();
            if (bytes.isEmpty())
                continue;
            embeddedArt->assign(bytes.data(), bytes.size());
            auto mime = picture.find("mimeType");
            if (embeddedMime && mime != picture.end())
                *embeddedMime = mime->second.toString().to8Bit(true);
            auto type = picture.find("pictureType");
            if (type != picture.end() && type->second.toString() == "Front Cover")
                break; // prefer the front cover, otherwise keep the first picture
        }
    }
    return true;
}

void Scanner::Start(const std::vector<std::string>& folders, bool force)
{
    Stop();
    fStop = false;
    fRunning = true;
    fThread = std::thread([this, folders, force] { Run(folders, force); });
}

void Scanner::Stop()
{
    fStop = true;
    if (fThread.joinable())
        fThread.join();
    fRunning = false;
}

int Scanner::WorkerCount() const
{
    if (fWorkerCount > 0)
        return fWorkerCount;
    // reading tags is waiting for the disk or the server: two workers pay off even on one
    // processor, more than eight only crowd the server
    return std::max(2, std::min(kMostWorkers, ProcessorCount()));
}

void Scanner::Walk(const std::vector<std::string>& folders, int workers, Pass& pass)
{
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> waiting(folders.begin(), folders.end());
    int listing = 0; // workers inside a folder, which may still add subfolders
    RunWorkers(workers, "Amp folder walk", [&](int) {
        while (true) {
            std::string folder;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return fStop || !waiting.empty() || listing == 0; });
                if (fStop || waiting.empty()) {
                    wake.notify_all();
                    return;
                }
                folder = std::move(waiting.front());
                waiting.pop_front();
                listing++;
            }
            std::vector<std::string> subfolders;
            std::vector<FoundFile> files;
            std::string art;
            int artRank = -1;
            bool isRoot = std::find(folders.begin(), folders.end(), folder) != folders.end();
            bool unreadable = false;
            int entries = 0;
            DIR* dir = opendir(folder.c_str());
            // a folder that is gone took its songs along; one that cannot be read (a share
            // that dropped out) says nothing about them
            if (!dir)
                unreadable = isRoot || errno != ENOENT;
            if (dir) {
                while (struct dirent* entry = readdir(dir)) {
                    if (fStop)
                        break;
                    std::string name = entry->d_name;
                    if (name.empty() || name[0] == '.')
                        continue;
                    entries++;
                    std::string path = folder + "/" + name;
                    struct stat st;
                    if (stat(path.c_str(), &st) != 0)
                        continue;
                    if (S_ISDIR(st.st_mode)) {
                        subfolders.push_back(path);
                    } else if (S_ISREG(st.st_mode)) {
                        if (IsAudioFile(path)) {
                            files.push_back({path, (int64_t)st.st_mtime, (int64_t)st.st_size});
                        } else if (st.st_size > 0) {
                            int rank = FolderArtRank(name);
                            if (rank >= 0 && (artRank < 0 || rank < artRank)) {
                                artRank = rank;
                                art = path;
                            }
                        }
                    }
                }
                closedir(dir);
                // an empty library folder is a share that is not mounted more often than a
                // collection that was deleted
                if (isRoot && entries == 0)
                    unreadable = true;
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (unreadable)
                pass.unreadable.push_back(folder);
            for (FoundFile& file : files)
                pass.files.push_back(std::move(file));
            if (!art.empty())
                pass.folderArt[folder] = art;
            for (std::string& sub : subfolders)
                waiting.push_back(std::move(sub));
            listing--;
            wake.notify_all();
        }
    });
    // the workers finish in any order: sort, so that the songs of an album are read together
    std::sort(pass.files.begin(), pass.files.end(), [](const FoundFile& a, const FoundFile& b) { return a.path < b.path; });
    pass.files.erase(std::unique(pass.files.begin(), pass.files.end(),
        [](const FoundFile& a, const FoundFile& b) { return a.path == b.path; }), pass.files.end());
}

void Scanner::ReadFile(const FoundFile& file, Pass& pass)
{
    Track track;
    std::string art, mime;
    ArtKey key;
    bool claimed = false;
    // The picture of an album is stored once, by the first worker that gets to one of its
    // songs; the others do not even copy theirs out of the tags.
    auto wantArt = [&](const Track& read) {
        std::string artist = read.groupingArtist();
        key = MakeAlbumArtKey(artist.empty() ? "Unknown Artist" : artist, read.album.empty() ? "Unknown Album" : read.album);
        if (fImages.Has(key))
            return false;
        std::lock_guard<std::mutex> lock(pass.mutex);
        claimed = pass.artClaimed.insert(key).second;
        return claimed;
    };
    bool read = ReadTrack(file.path, file.size, file.modified, track, &art, &mime, wantArt);
    if (read && key.empty())
        wantArt(track); // no tags at all: the names came from the folders
    if (read && track.durationMs <= 0 && durationProbe) {
        std::lock_guard<std::mutex> lock(pass.probeMutex);
        track.durationMs = durationProbe(file.path);
        if (track.bitrate <= 0 && track.durationMs > 0)
            track.bitrate = (int)(track.sizeBytes * 8 / track.durationMs);
    }
    if (read) {
        track.art = key;
        if (claimed) {
            bool stored = false;
            if (!art.empty())
                stored = fImages.Store(key, art, mime);
            if (!stored) {
                std::string picture;
                {
                    std::lock_guard<std::mutex> lock(pass.mutex);
                    auto found = pass.folderArt.find(FolderOf(file.path));
                    if (found != pass.folderArt.end())
                        picture = found->second;
                }
                if (!picture.empty()) {
                    std::string bytes = ReadFileBytes(picture);
                    if (!bytes.empty())
                        stored = fImages.Store(key, bytes, Extension(picture) == ".png" ? "image/png" : "image/jpeg");
                }
            }
            if (!stored) {
                // nothing here: another song of the album may carry the picture
                std::lock_guard<std::mutex> lock(pass.mutex);
                pass.artClaimed.erase(key);
            }
        }
    }
    bool flush = false;
    {
        std::lock_guard<std::mutex> lock(pass.mutex);
        if (read) {
            pass.pending.push_back(std::move(track));
            pass.added++;
        }
        flush = pass.pending.size() >= kBatch;
    }
    pass.processed++;
    if (flush)
        Flush(pass, false);
    ReportProgress(pass);
}

void Scanner::Flush(Pass& pass, bool everything)
{
    std::vector<Track> batch;
    bool rebuild = false;
    {
        std::lock_guard<std::mutex> lock(pass.mutex);
        if (pass.pending.empty() || (!everything && pass.pending.size() < kBatch))
            return;
        batch.swap(pass.pending);
        // the views reload after every rebuild: often enough to watch the library grow,
        // not so often that the scan waits for the index
        int64_t now = NowMs();
        if (!everything && now - pass.lastRebuild >= kRebuildInterval) {
            pass.lastRebuild = now;
            rebuild = true;
        }
    }
    fLibrary.UpsertTracks(batch, rebuild);
}

void Scanner::ReportProgress(Pass& pass)
{
    fProcessed = pass.processed.load();
    if (!onProgress)
        return;
    {
        std::lock_guard<std::mutex> lock(pass.mutex);
        int64_t now = NowMs();
        if (now - pass.lastProgress < kProgressInterval)
            return;
        pass.lastProgress = now;
    }
    onProgress("Scanning: " + std::to_string(pass.processed.load()) + " of " + std::to_string(pass.files.size()) + " files", false);
}

void Scanner::Run(std::vector<std::string> folders, bool force)
{
    fProcessed = 0;
    fTotal = 0;
    if (onProgress)
        onProgress("Scanning folders", false);
    for (std::string& folder : folders)
        while (folder.size() > 1 && folder.back() == '/')
            folder.pop_back();
    int workers = WorkerCount();
    Pass pass;
    Walk(folders, workers, pass);

    std::map<std::string, std::pair<int64_t, int64_t>> known = fLibrary.LocalFileIndex();
    std::unordered_set<std::string> present;
    for (const FoundFile& file : pass.files) {
        present.insert(file.path);
        auto it = known.find(file.path);
        if (!force && it != known.end() && it->second.first == file.modified && it->second.second == file.size)
            pass.processed++;
        else
            pass.changed.push_back(&file);
    }
    pass.lastRebuild = NowMs();
    fProcessed = pass.processed.load();
    fTotal = (int)pass.files.size();
    if (!pass.changed.empty() && !fStop) {
        RunWorkers(std::min(workers, (int)pass.changed.size()), "Amp tag reader", [&](int) {
            while (!fStop) {
                size_t index = pass.next++;
                if (index >= pass.changed.size())
                    return;
                ReadFile(*pass.changed[index], pass);
            }
        });
    }
    Flush(pass, true);
    // remove files that disappeared (only when the scan was not interrupted)
    std::vector<std::string> missing;
    if (!fStop) {
        for (auto& entry : known)
            if (!present.count(entry.first)) {
                bool underFolder = false;
                for (const std::string& folder : folders)
                    if (IsUnder(entry.first, folder))
                        underFolder = true;
                for (const std::string& folder : pass.unreadable)
                    if (IsUnder(entry.first, folder))
                        underFolder = false;
                if (underFolder)
                    missing.push_back(entry.first);
            }
    }
    if (!missing.empty())
        fLibrary.RemoveTracksByUri(missing, false);
    int added = pass.added;
    fProcessed = pass.processed.load();
    if (added || !missing.empty())
        fLibrary.RebuildIndex();
    fRunning = false;
    if (onProgress)
        onProgress(added || missing.size() ? "Scan finished: " + std::to_string(added) + " new or changed, "
            + std::to_string(missing.size()) + " removed" : "Library up to date", true);
}

} // namespace amp
