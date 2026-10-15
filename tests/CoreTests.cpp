// Unit tests for Amp's portable core. Build and run with `make check` (on Haiku) or compile
// this file with src/core on any system with a C++17 compiler.
#include "core/ArtistLinks.h"
#include "core/Des.h"
#include "core/ImageCache.h"
#include "core/Library.h"
#include "core/MusicAssistant.h"
#include "core/NetMD.h"
#include "core/NetMDSimulator.h"
#include "core/Queue.h"
#include "core/Resampler.h"
#include "core/Scanner.h"
#include "core/Settings.h"
#include "core/TagStream.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace amp;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what, int line)
{
    if (!condition) {
        fprintf(stderr, "FAILED (line %d): %s\n", line, what);
        gFailures++;
    }
}

#define CHECK(condition) Check((condition), #condition, __LINE__)

netmd::Bytes Hex(const char* text)
{
    return netmd::Query(text).Data();
}

std::string ToHex(const netmd::Bytes& bytes)
{
    std::string text;
    char digits[3];
    for (uint8_t b : bytes) {
        snprintf(digits, sizeof(digits), "%02x", b);
        text += digits;
    }
    return text;
}

// Serves generated PCM without a file.
class ToneSource : public netmd::PcmSource {
public:
    explicit ToneSource(uint64_t bytes) : fLeft(bytes) {}
    size_t Read(uint8_t* buffer, size_t length) override
    {
        size_t count = (size_t)std::min<uint64_t>(length, fLeft);
        for (size_t i = 0; i < count; i++)
            buffer[i] = (uint8_t)(fPosition++ * 7);
        fLeft -= count;
        return count;
    }

private:
    uint64_t fLeft;
    uint64_t fPosition = 0;
};

void TestDes()
{
    netmd::Bytes key = Hex("133457799BBCDFF1");
    netmd::Bytes plain = Hex("0123456789ABCDEF");
    uint8_t out[8];
    Des(key.data()).EncryptEcb(plain.data(), out, 8);
    CHECK(ToHex(netmd::Bytes(out, out + 8)) == "85e813540f0ab405");
    uint8_t back[8];
    Des(key.data()).DecryptEcb(out, back, 8);
    CHECK(memcmp(back, plain.data(), 8) == 0);

    netmd::Bytes mac = netmd::RetailMac(Hex("123456789abcdef00fedcba987654321"), Hex("0011223344556677 8899aabbccddeeff"));
    CHECK(ToHex(mac) == "d1747712a78fe4b0");
}

void TestTitles()
{
    CHECK(netmd::SanitizeTitle("Bj\xc3\xb6rk \xe2\x80\x93 J\xc3\xb3ga") == "Bjork - Joga");
    CHECK(netmd::SanitizeTitle("  a  \\ b ~ ") == "a / b -");
    CHECK(netmd::SanitizeTitle("\xe6\x9d\xb1\xe4\xba\xac") == "");
    CHECK(netmd::TitleCells("") == 0);
    CHECK(netmd::TitleCells("1234567") == 1);
    CHECK(netmd::TitleCells("12345678") == 2);
    std::vector<std::string> titles(30, std::string(40, 'x'));
    std::vector<std::string> fitted = netmd::FitTitles(titles, 100);
    int cells = 0;
    for (const std::string& title : fitted)
        cells += netmd::TitleCells(title);
    CHECK(cells <= 100);
    CHECK(fitted[0].size() >= 14); // shortened evenly, not dropped
    fitted = netmd::FitTitles(titles, 10);
    int kept = 0;
    for (const std::string& title : fitted)
        kept += !title.empty();
    CHECK(kept == 10);
    CHECK(netmd::FormatFrames(512 * 65) == "1:05");
    // measured: songs of 34, 41, 28, 37 and 45 s took 3:22 of an erased disc
    int64_t used = 0;
    for (int seconds : {34, 41, 28, 37, 45})
        used += netmd::SPFramesForDuration(seconds * 1000 + 40);
    CHECK(used / 512 == 202);
    CHECK(netmd::FormatFrames((int64_t)512 * 3725) == "1:02:05");
}

void TestResampler()
{
    Resampler resampler(48000, 44100, 2);
    std::vector<float> input(48000 * 2), output;
    for (size_t i = 0; i < 48000; i++)
        input[i * 2] = input[i * 2 + 1] = 0.5f * (float)sin(2 * M_PI * 1000 * i / 48000.0);
    for (size_t i = 0; i < 48000; i += 1000)
        resampler.Process(&input[i * 2], 1000, output);
    resampler.Flush(output);
    CHECK(output.size() == 44100 * 2);
    float peak = 0;
    for (size_t i = 4410 * 2; i < output.size() - 4410 * 2; i++)
        peak = std::max(peak, std::fabs(output[i]));
    CHECK(std::fabs(peak - 0.5f) < 0.001f);
}

void TestProtocol()
{
    netmd::Simulator simulator;
    simulator.discTitle = "Old Disc";
    simulator.tracks.push_back({"Old Song", 512 * 200, netmd::kEncodingLP2});
    netmd::Device device(simulator);
    device.Flush();
    netmd::DiscInfo info = device.ReadDiscInfo();
    CHECK(info.present);
    CHECK(info.writable);
    CHECK(info.title == "Old Disc");
    CHECK(info.trackCount == 1);
    CHECK(info.totalFrames == (int64_t)80 * 60 * 512);
    CHECK(info.leftFrames == info.totalFrames - 512 * 100); // LP2 takes half the SP space
    device.ReadContents(info);
    CHECK(info.tracks.size() == 1 && info.tracks[0].title == "Old Song");
    CHECK(info.tracks[0].encoding == netmd::kEncodingLP2);

    device.EraseDisc();
    device.SetDiscTitle("New Disc");
    uint64_t bytes = (uint64_t)netmd::kPcmBytesPerSecond * 3 + 1000; // not a whole frame
    ToneSource source(bytes);
    uint64_t lastSent = 0;
    int track = netmd::DownloadTrack(device, source, bytes, "First Song",
        [&](const netmd::DownloadProgress& progress) { lastSent = progress.sent; });
    CHECK(track == 0);
    CHECK(lastSent % netmd::kPcmFrameSize == 0 && lastSent >= bytes);
    info = device.ReadDiscInfo();
    device.ReadContents(info);
    CHECK(info.title == "New Disc");
    CHECK(info.trackCount == 1);
    CHECK(info.tracks.size() == 1 && info.tracks[0].title == "First Song");
    CHECK(info.tracks[0].frames >= 512 * 3);

    // groups: the disc title is the part before the first "//"
    device.SetDiscTitle("0;Mix Tape//1;Side A//");
    info = device.ReadDiscInfo();
    CHECK(info.title == "Mix Tape");
    CHECK(info.rawTitle == "0;Mix Tape//1;Side A//");

    // a write-protected disc refuses the upload
    simulator.writeProtected = true;
    info = device.ReadDiscInfo();
    CHECK(info.writeProtected);
    bool rejected = false;
    try {
        ToneSource more(bytes);
        netmd::DownloadTrack(device, more, bytes, "Nope");
    } catch (const netmd::Error& error) {
        rejected = error.kind() == netmd::Error::kRejected;
        netmd::Recover(device);
    }
    CHECK(rejected);
}

// ---- library, queue and scanner -------------------------------------------

std::string TempFolder(const char* name)
{
    const char* base = getenv("TMPDIR");
    std::string path = std::string(base && *base ? base : "/tmp") + "/amp-test-" + name + "-" + std::to_string((long)getpid());
    std::string command = "rm -rf '" + path + "'";
    if (system(command.c_str()) != 0)
        fprintf(stderr, "cannot clear %s\n", path.c_str());
    mkdir(path.c_str(), 0755);
    return path;
}

void RemoveFolder(const std::string& path)
{
    std::string command = "rm -rf '" + path + "'";
    if (system(command.c_str()) != 0)
        fprintf(stderr, "cannot remove %s\n", path.c_str());
}

void WriteBytes(const std::string& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), (std::streamsize)bytes.size());
}

// One second of silence, 8 kHz mono 16 bit: the smallest file TagLib reads a length from.
std::string WavFile(int seconds = 1)
{
    uint32_t dataSize = 8000 * 2 * seconds;
    std::string wav = "RIFF";
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) wav += (char)(v >> (8 * i)); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) wav += (char)(v >> (8 * i)); };
    u32(36 + dataSize);
    wav += "WAVEfmt ";
    u32(16);
    u16(1);
    u16(1);
    u32(8000);
    u32(16000);
    u16(2);
    u16(16);
    wav += "data";
    u32(dataSize);
    wav.append(dataSize, 0);
    return wav;
}

Track MakeTrack(Source source, const std::string& uri, const std::string& album)
{
    Track track;
    track.source = source;
    track.uri = uri;
    track.title = uri;
    track.artist = "Artist";
    track.album = album;
    track.durationMs = 1000;
    return track;
}

void TestQueueRemoveIf()
{
    PlayQueue queue;
    queue.Set({1, 2, 3, 4, 5, 6}, 3); // playing 4
    queue.RemoveIf([](int64_t id) { return id % 2 == 0; });
    CHECK(queue.Size() == 3);
    CHECK(queue.Tracks() == std::vector<int64_t>({1, 3, 5}));
    CHECK(queue.Current() == 5); // 4 went: the song after it is current
    queue.RemoveIf([](int64_t id) { return id == 5; });
    CHECK(queue.Current() == 3); // nothing after it: the last one left
    queue.RemoveIf([](int64_t) { return true; });
    CHECK(queue.Empty());
    CHECK(queue.Current() == 0);

    PlayQueue kept;
    kept.Set({7, 8, 9}, 1);
    kept.RemoveIf([](int64_t id) { return id == 7; });
    CHECK(kept.Current() == 8); // a song before the current one went: the current one stays
    kept.RemoveIf([](int64_t) { return false; });
    CHECK(kept.Size() == 2 && kept.Current() == 8);
}

void TestClearMusicAssistant()
{
    std::string folder = TempFolder("library");
    {
        Library library(folder + "/library.db");
        std::string error;
        CHECK(library.Open(error));
        CHECK(!library.HasMusicAssistantData());

        std::vector<Track> local = {MakeTrack(Source::Local, "/music/a.flac", "Local Album"),
            MakeTrack(Source::Local, "/music/b.flac", "Local Album")};
        std::vector<int64_t> localIds = library.UpsertTracks(local, false);
        MASyncResult sync;
        for (int i = 0; i < 300; i++) {
            Track track = MakeTrack(Source::MusicAssistant, "library://track/" + std::to_string(i), "Streamed Album");
            track.maAlbumUri = "library://album/1";
            sync.tracks.push_back(track);
        }
        Track provider = MakeTrack(Source::MusicAssistant, "tidal--x://track/9", "Provider Album");
        provider.inLibrary = false;
        sync.tracks.push_back(provider);
        Album album;
        album.maUri = "library://album/1";
        album.maItemId = "1";
        album.name = "Streamed Album";
        album.artist = "Artist";
        sync.albums.push_back(album);
        Artist artist;
        artist.maUri = "library://artist/1";
        artist.maItemId = "1";
        artist.name = "Artist";
        sync.artists.push_back(artist);
        Playlist remote;
        remote.source = Source::MusicAssistant;
        remote.name = "Server Playlist";
        remote.maItemId = "77";
        remote.maUri = "library://playlist/77";
        sync.playlists.push_back(remote);
        sync.playlistTrackUris.push_back({"library://track/1", "library://track/2"});
        library.ApplyMASync(sync);
        CHECK(library.HasMusicAssistantData());
        CHECK(library.TrackCount() == 303);

        int64_t streamed = 0;
        {
            Library::Locker locker(library);
            const Track* track = library.TrackByUri("library://track/5");
            CHECK(track != nullptr);
            streamed = track ? track->id : 0;
        }
        int64_t mixed = library.CreatePlaylist("Mixed");
        library.AddToPlaylist(mixed, {localIds[0], streamed, localIds[1]});
        library.LinkPlaylistToMA(mixed, "88", "library://playlist/88", true);
        int64_t plain = library.CreatePlaylist("Plain");
        library.AddToPlaylist(plain, {localIds[1]});

        library.ClearMusicAssistantData();
        CHECK(!library.HasMusicAssistantData());
        CHECK(library.TrackCount() == 2);
        CHECK(library.AllAlbumIds().size() == 1);
        CHECK(library.AllPlaylistIds().size() == 2);
        Library::Locker locker(library);
        CHECK(library.TrackByUri("library://track/5") == nullptr);
        CHECK(library.TrackByUri("tidal--x://track/9") == nullptr);
        const Playlist* playlist = library.PlaylistById(mixed);
        CHECK(playlist && playlist->trackIds == localIds);
        CHECK(playlist && !playlist->syncToMA && playlist->maItemId.empty());
        for (int64_t id : library.AllAlbumIds())
            CHECK(!library.AlbumById(id)->isMA());
    }
    {
        // and the file on disk agrees
        Library library(folder + "/library.db");
        std::string error;
        CHECK(library.Open(error));
        CHECK(!library.HasMusicAssistantData());
        CHECK(library.TrackCount() == 2);
        std::vector<int64_t> playlists = library.AllPlaylistIds();
        CHECK(playlists.size() == 2);
        Library::Locker locker(library);
        for (int64_t id : playlists) {
            const Playlist* playlist = library.PlaylistById(id);
            CHECK(playlist && !playlist->isMA() && !playlist->syncToMA);
            if (playlist && playlist->name == "Mixed")
                CHECK(playlist->trackIds.size() == 2);
        }
    }
    RemoveFolder(folder);
}

// Album and artist ids are handed out afresh by every index rebuild; a view that opened an
// album finds it again by artist and name (#9).
void TestAlbumIdsAcrossRebuild()
{
    std::string folder = TempFolder("library-ids");
    {
        Library library(folder + "/library.db");
        std::string error;
        CHECK(library.Open(error));
        std::vector<Track> first = {MakeTrack(Source::Local, "/music/m/1.flac", "Middle Album")};
        library.UpsertTracks(first, true);
        int64_t middle = library.AlbumIdFor("Artist", "Middle Album", false);
        CHECK(middle != 0);
        CHECK(library.AlbumById(middle) != nullptr && library.AlbumById(middle)->name == "Middle Album");
        CHECK(library.AlbumIdFor("Artist", "Middle Album", true) == 0); // not a Music Assistant album
        CHECK(library.AlbumIdFor("Artist", "No Such Album", false) == 0);
        // songs read before it in the next scan push the album to another id
        std::vector<Track> earlier = {MakeTrack(Source::Local, "/music/a/1.flac", "Album A"),
            MakeTrack(Source::Local, "/music/a/2.flac", "Album B")};
        library.UpsertTracks(earlier, true);
        int64_t again = library.AlbumIdFor("artist", "middle album", false); // the key ignores case
        CHECK(again != 0);
        CHECK(library.AlbumById(again)->name == "Middle Album");
        CHECK(library.AlbumById(again)->trackIds.size() == 1);
        bool moved = again != middle || library.AlbumById(middle)->name != "Middle Album";
        CHECK(moved || library.AllAlbumIds().size() == 3);
        CHECK(library.ArtistIdFor("Artist") != 0);
    }
    unlink((folder + "/library.db").c_str());
    rmdir(folder.c_str());
}

void TestTagStream()
{
    std::string folder = TempFolder("stream");
    std::string bytes;
    for (int i = 0; i < 700 * 1024 + 123; i++)
        bytes += (char)((i * 131 + (i >> 9)) & 0xff);
    WriteBytes(folder + "/data.bin", bytes);
    TagStream stream(folder + "/data.bin");
    CHECK(stream.isOpen());
    CHECK(stream.length() == (TagLib::offset_t)bytes.size());
    struct Read { long position; size_t length; };
    const Read reads[] = {{0, 10}, {10, 4000}, {(long)bytes.size() - 128, 128}, {(long)bytes.size() - 160, 32},
        {300000, 8}, {300008, 100000}, {299990, 40}, {60000, 10000}, {(long)bytes.size() - 5, 50}, {123456, 300000},
        {0, bytes.size()}};
    for (const Read& read : reads) {
        stream.seek(read.position);
        TagLib::ByteVector block = stream.readBlock(read.length);
        size_t expected = std::min(read.length, bytes.size() - (size_t)read.position);
        CHECK(block.size() == expected);
        CHECK(memcmp(block.data(), bytes.data() + read.position, block.size()) == 0);
        CHECK(stream.tell() == (TagLib::offset_t)(read.position + expected));
    }
    stream.seek(-16, TagLib::IOStream::End);
    CHECK(stream.readBlock(64).size() == 16);
    CHECK(stream.readBlock(64).isEmpty());
    // everything was read once at most
    CHECK(stream.BytesRead() == (off_t)bytes.size());
    TagStream missing(folder + "/nothing.bin");
    CHECK(!missing.isOpen());
    CHECK(missing.readBlock(10).isEmpty());
    RemoveFolder(folder);
}

// ---- MP4 ------------------------------------------------------------------

std::string Be32(uint32_t value)
{
    std::string bytes;
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes += (char)(value >> shift);
    return bytes;
}

std::string Mp4Box(const char* type, const std::string& body)
{
    return Be32((uint32_t)(8 + body.size())) + type + body;
}

// An audio file in MP4: `fragments` > 0 puts the sound into that many moof/mdat pairs and
// the length into mehd, as streaming services write their downloads.
std::string Mp4File(const char* codec, uint32_t timescale, uint32_t duration, int fragments)
{
    std::string mvhd = Be32(0) + Be32(0) + Be32(0) + Be32(timescale) + Be32(fragments ? 0 : duration) + std::string(80, 0);
    std::string mdhd = Be32(0) + Be32(0) + Be32(0) + Be32(timescale) + Be32(fragments ? 0 : duration) + Be32(0);
    std::string hdlr = Be32(0) + Be32(0) + "soun" + std::string(12, 0) + std::string(1, 0);
    std::string entry = Be32(36) + codec + std::string(6, 0) + std::string("\0\1", 2) + std::string(8, 0)
        + std::string("\0\2\0\x10", 4) + std::string(4, 0) + Be32(timescale << 16);
    std::string stsd = Be32(0) + Be32(1) + entry;
    std::string stbl = Mp4Box("stsd", stsd) + Mp4Box("stts", Be32(0) + Be32(0)) + Mp4Box("stsc", Be32(0) + Be32(0))
        + Mp4Box("stsz", Be32(0) + Be32(0) + Be32(0)) + Mp4Box("stco", Be32(0) + Be32(0));
    std::string trak = Mp4Box("tkhd", std::string(84, 0))
        + Mp4Box("mdia", Mp4Box("mdhd", mdhd) + Mp4Box("hdlr", hdlr) + Mp4Box("minf", Mp4Box("smhd", std::string(8, 0)) + Mp4Box("stbl", stbl)));
    std::string moov = Mp4Box("mvhd", mvhd) + Mp4Box("trak", trak);
    if (fragments)
        moov += Mp4Box("mvex", Mp4Box("mehd", Be32(0) + Be32(duration)) + Mp4Box("trex", std::string(24, 0)));
    std::string file = Mp4Box("ftyp", std::string("M4A ") + Be32(0) + "M4A mp42isom") + Mp4Box("moov", moov);
    if (!fragments)
        file += Mp4Box("mdat", std::string(20000, 'a'));
    for (int i = 0; i < fragments; i++)
        file += Mp4Box("moof", Mp4Box("mfhd", Be32(0) + Be32(i + 1))) + Mp4Box("mdat", std::string(20000, 'a'));
    return file;
}

void TestMp4()
{
    std::string folder = TempFolder("mp4");
    {
        WriteBytes(folder + "/fragmented.m4a", Mp4File("fLaC", 96000, 96000 * 312, 60));
        TagStream stream(folder + "/fragmented.m4a");
        Mp4Info info;
        CHECK(ReadMp4Info(stream, info));
        CHECK(info.fragmented);
        CHECK(info.durationMs == 312000);
        CHECK(info.codec == "fLaC");
        CHECK(stream.ReadCount() <= 4); // not one read for each fragment
        Track track;
        CHECK(Scanner::ReadTrack(folder + "/fragmented.m4a", track, nullptr, nullptr));
        CHECK(track.durationMs == 312000);
        CHECK(track.lossless);
        CHECK(track.bitrate == (int)(track.sizeBytes * 8 / 312000));
    }
    {
        WriteBytes(folder + "/plain.m4a", Mp4File("mp4a", 44100, 44100 * 200, 0));
        TagStream stream(folder + "/plain.m4a");
        Mp4Info info;
        CHECK(ReadMp4Info(stream, info));
        CHECK(!info.fragmented);
        CHECK(info.durationMs == 200000);
        CHECK(info.codec == "mp4a");
        Track track;
        CHECK(Scanner::ReadTrack(folder + "/plain.m4a", track, nullptr, nullptr));
        CHECK(track.durationMs == 200000);
        CHECK(!track.lossless);
    }
    {
        // not an MP4 file at all, and one that ends inside a box
        WriteBytes(folder + "/noise.m4a", std::string(5000, 'x'));
        TagStream noise(folder + "/noise.m4a");
        Mp4Info info;
        CHECK(!ReadMp4Info(noise, info));
        std::string cut = Mp4File("alac", 44100, 44100, 3);
        WriteBytes(folder + "/cut.m4a", cut.substr(0, 300));
        TagStream stream(folder + "/cut.m4a");
        CHECK(!ReadMp4Info(stream, info));
        Track track;
        Scanner::ReadTrack(folder + "/cut.m4a", track, nullptr, nullptr); // must not crash
    }
    RemoveFolder(folder);
}

void TestScanner()
{
    std::string folder = TempFolder("scan");
    std::string music = folder + "/music";
    mkdir(music.c_str(), 0755);
    const int kArtists = 6, kAlbums = 3, kSongs = 5;
    // a PNG header is all the cache looks at
    std::string picture = std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, 'x');
    for (int a = 0; a < kArtists; a++) {
        std::string artist = music + "/Artist " + std::to_string(a);
        mkdir(artist.c_str(), 0755);
        for (int b = 0; b < kAlbums; b++) {
            std::string album = artist + "/Album " + std::to_string(b);
            mkdir(album.c_str(), 0755);
            for (int c = 0; c < kSongs; c++)
                WriteBytes(album + "/0" + std::to_string(c + 1) + " Song " + std::to_string(c) + ".wav", WavFile());
            if (b != 2)
                WriteBytes(album + (b == 0 ? "/cover.png" : "/Folder.PNG"), picture);
            WriteBytes(album + "/notes.txt", "not music");
        }
    }
    const int kTotal = kArtists * kAlbums * kSongs;
    {
        Settings settings(folder + "/settings.json");
        Library library(folder + "/library.db");
        std::string error;
        CHECK(library.Open(error));
        ImageCache images(folder + "/art", settings);
        CHECK(images.Open());
        Scanner scanner(library, images);
        scanner.SetWorkerCount(6);
        std::mutex mutex;
        std::string last;
        bool done = false;
        scanner.onProgress = [&](const std::string& text, bool finished) {
            std::lock_guard<std::mutex> lock(mutex);
            last = text;
            done = finished;
        };
        auto scan = [&](bool force) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = false;
            }
            scanner.Start({music + "/"}, force);
            while (scanner.IsRunning())
                usleep(10000);
            scanner.Stop();
            std::lock_guard<std::mutex> lock(mutex);
            CHECK(done);
            return last;
        };
        std::string result = scan(false);
        CHECK(result == "Scan finished: " + std::to_string(kTotal) + " new or changed, 0 removed");
        CHECK(library.TrackCount() == (size_t)kTotal);
        CHECK(library.AllAlbumIds().size() == (size_t)(kArtists * kAlbums));
        CHECK(library.AllArtistIds().size() == (size_t)kArtists);
        {
            Library::Locker locker(library);
            const Track* track = library.TrackByUri(music + "/Artist 2/Album 1/03 Song 2.wav");
            CHECK(track != nullptr);
            if (track) {
                // untagged files take their names from the folders and the file
                CHECK(track->artist == "Artist 2");
                CHECK(track->album == "Album 1");
                CHECK(track->title == "Song 2");
                CHECK(track->trackNumber == 3);
                CHECK(track->durationMs == 1000);
                CHECK(track->lossless);
                CHECK(images.Has(track->art));
            }
            const Track* bare = library.TrackByUri(music + "/Artist 2/Album 2/01 Song 0.wav");
            CHECK(bare && !images.Has(bare->art)); // the album without a picture
        }
        CHECK(scan(false) == "Library up to date");
        CHECK(scan(true) == "Scan finished: " + std::to_string(kTotal) + " new or changed, 0 removed");
        CHECK(library.TrackCount() == (size_t)kTotal);

        // one song changes, one goes, one album is added
        WriteBytes(music + "/Artist 0/Album 0/01 Song 0.wav", WavFile(2));
        unlink((music + "/Artist 0/Album 0/02 Song 1.wav").c_str());
        std::string added = music + "/Artist 0/Album 9";
        mkdir(added.c_str(), 0755);
        WriteBytes(added + "/01 New.wav", WavFile());
        CHECK(scan(false) == "Scan finished: 2 new or changed, 1 removed");
        CHECK(library.TrackCount() == (size_t)kTotal);
        {
            Library::Locker locker(library);
            const Track* changed = library.TrackByUri(music + "/Artist 0/Album 0/01 Song 0.wav");
            CHECK(changed && changed->durationMs == 2000);
            CHECK(library.TrackByUri(music + "/Artist 0/Album 0/02 Song 1.wav") == nullptr);
            CHECK(library.TrackByUri(added + "/01 New.wav") != nullptr);
        }

        // a library folder that cannot be read (a share that is not mounted) keeps its songs
        std::string away = folder + "/music-away";
        CHECK(rename(music.c_str(), away.c_str()) == 0);
        CHECK(scan(false) == "Library up to date");
        CHECK(library.TrackCount() == (size_t)kTotal);
        mkdir(music.c_str(), 0755);
        CHECK(scan(false) == "Library up to date"); // mounted, but empty
        CHECK(library.TrackCount() == (size_t)kTotal);
        rmdir(music.c_str());
        CHECK(rename(away.c_str(), music.c_str()) == 0);
        // a folder that was deleted takes its songs along
        RemoveFolder(music + "/Artist 5");
        CHECK(scan(false) == "Scan finished: 0 new or changed, " + std::to_string(kAlbums * kSongs) + " removed");
        CHECK(library.TrackCount() == (size_t)(kTotal - kAlbums * kSongs));
    }
    RemoveFolder(folder);
}

} // namespace

void TestArtistLinks()
{
    const std::string tool = "b0b1a3ea-3fe1-4e3f-8a4b-0a8d9f5ee7a4";
    const std::string other = "5b11f4ce-a62d-471e-81fc-a69a8278c7da";
    CHECK(ArtistLinks::IsMbid(tool));
    CHECK(!ArtistLinks::IsMbid("b0b1a3ea-3fe1-4e3f-8a4b-0a8d9f5ee7a"));
    CHECK(!ArtistLinks::IsMbid("b0b1a3ea_3fe1-4e3f-8a4b-0a8d9f5ee7a4"));
    CHECK(!ArtistLinks::IsMbid("g0b1a3ea-3fe1-4e3f-8a4b-0a8d9f5ee7a4"));
    CHECK(ArtistLinks::ArtistUrl(tool) == "https://musicbrainz.org/artist/" + tool);
    CHECK(ArtistLinks::SearchUrl("Sigur Rós & Co") ==
        "https://musicbrainz.org/search?query=Sigur%20R%C3%B3s%20%26%20Co&type=artist&method=indexed");
    CHECK(ArtistLinks::PageUrl("Tool", tool) == ArtistLinks::ArtistUrl(tool));
    CHECK(ArtistLinks::PageUrl("Tool", "") == ArtistLinks::SearchUrl("Tool"));
    // an artist search: exactly one artist of the name is taken, a shared name is not
    std::string one = "{\"artists\":[{\"id\":\"" + tool + "\",\"name\":\"TOOL\",\"score\":100},"
        "{\"id\":\"" + other + "\",\"name\":\"Toolshed\",\"score\":80}]}";
    CHECK(ArtistLinks::PickArtist("Tool", one) == tool);
    std::string two = "{\"artists\":[{\"id\":\"" + tool + "\",\"name\":\"Nirvana\"},"
        "{\"id\":\"" + other + "\",\"name\":\"Nirvana\"}]}";
    CHECK(ArtistLinks::PickArtist("Nirvana", two).empty());
    CHECK(ArtistLinks::PickArtist("Tool", "not json").empty());
    CHECK(ArtistLinks::PickArtist("Tool", "{\"artists\":null}").empty());
    // a release search names the credited artist
    std::string releases = "{\"releases\":[{\"id\":\"x\",\"artist-credit\":[{\"name\":\"AC/DC\","
        "\"artist\":{\"id\":\"" + other + "\",\"name\":\"AC/DC\"}}]}]}";
    CHECK(ArtistLinks::PickCreditedArtist("ACDC", releases) == other);
    CHECK(ArtistLinks::PickCreditedArtist("Tool", releases).empty());
    // what is learned is kept in the cache directory
    char folder[] = "/tmp/amp-links-XXXXXX";
    CHECK(mkdtemp(folder) != nullptr);
    ArtistLinks& links = ArtistLinks::Shared();
    links.Open(folder);
    links.Remember("Tool", tool);
    links.Remember("Nobody", "not an id");
    CHECK(links.Known("tool") == tool);
    CHECK(links.Known("Nobody").empty());
    std::ifstream saved(std::string(folder) + "/artist-mbids.json");
    std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    CHECK(text.find(tool) != std::string::npos);
    unlink((std::string(folder) + "/artist-mbids.json").c_str());
    rmdir(folder);
}

int main()
{
    TestDes();
    TestTitles();
    TestResampler();
    TestProtocol();
    TestQueueRemoveIf();
    TestClearMusicAssistant();
    TestAlbumIdsAcrossRebuild();
    TestTagStream();
    TestMp4();
    TestScanner();
    TestArtistLinks();
    if (gFailures) {
        fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
