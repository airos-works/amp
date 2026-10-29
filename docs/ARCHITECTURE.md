# Amp architecture

```
src/core     portable C++17 (no Haiku headers except in Scanner's TagLib use)
  Model.h            Track / Album / Artist / Playlist structs, art keys
  Settings           JSON settings file (library folders, art sources, UI state)
  Library            SQLite store + in-memory index (albums/artists derived from tracks)
  Scanner            folder walk, TagLib tags, embedded/folder art, optional duration probe
  TagStream          the file access TagLib reads through: few large reads; MP4 box reader
  Workers            a handful of threads for one job (Kernel Kit threads on Haiku)
  ImageCache         on-disk artwork cache + online lookup workers
  ArtProviders       MusicBrainz/CAA, TheAudioDB, Discogs lookups
  ArtistLinks        an artist's MusicBrainz page: MBIDs seen in lookups or asked by name, cached
  Http               libcurl wrapper
  Queue              play queue with shuffle/repeat
  NetMD              MiniDisc recorder protocol: commands, secure download, title/capacity planning
  NetMDSimulator     a recorder in software (tests, AMP_NETMD_SIMULATE)
  Des                DES / two-key 3DES for NetMD
  Resampler          windowed-sinc sample rate conversion
  FlacDecoder, AlacDecoder   lossless packet decoders (Haiku's own drop the last frames)
src/player   Haiku Media Kit
  AudioOutput        BSoundPlayer + ring buffer + volume
  LocalDecoder       BMediaFile/BMediaTrack decode thread
  Player             controller: queue, decoder, progress
  NetMDUsb           USB Kit transport and roster for NetMD recorders
  MiniDiscPcm        any local file -> 44.1 kHz 16-bit big-endian PCM for SP recording
  MiniDisc           MiniDiscManager: recorder state, write and erase jobs on one worker thread
src/quicklook  air/OS Quick Look add-on (add-ons/quick_look/Amp): LocalDecoder + AudioOutput +
               TagLib artwork and tags in Tracker's space-bar panel; built from these sources
               as position independent code when the SDK has add-ons/quick_look/QuickLook.h
src/ui       Haiku Interface Kit
  App                BApplication: owns everything, the background scan
  MainWindow         toolbar / sidebar / content cards / status bar
  ToolbarView        transport, volume, LCD display (song and activity pages), view switcher, search
  SidebarView        LIBRARY, DEVICES and PLAYLISTS sources, drop target, context menu
  TrackListView      table with album-grouped mode, sorting, selection, drag & drop
  AlbumGridView      album cover grid
  ArtistsView        artist list + header (links to MusicBrainz) + grouped track list
  StatusBarView      +/shuffle/repeat, summary, Write to MiniDisc and Settings buttons
  Icons              Font Awesome glyphs: registers the bundled font, draws it centred;
                     the MiniDisc pictures from the resources, scaled and tinted
  SettingsWindow     Library / Artwork tabs
  ArtStore           scaled BBitmap cache fed by ImageCache
  MiniDiscView       the DEVICES source: disc summary, capacity bar, tracks and pending songs
  MainWindowMiniDisc the write flow (questions, job) and its progress in the LCD and sidebar
```

MiniDisc support is described in `MINIDISC.md`.

The column header is deliberately *not* part of the scrolled table: `TrackHeaderView` is a
sibling of the `BScrollView`, so scrolling blits only the rows while the header is painted once
and stays put. `TrackListView` still owns the columns and hands them to the header through
`DrawHeader()`/`HeaderMouseDown()`, keeping it in step with `InvalidateHeader()`.

The scrolled views (song list, cover grid, artist list, sidebar, MiniDisc songs) set their
scroll bars themselves and carry `B_SCROLL_VIEW_AWARE`. Without it `BScrollView::FrameResized`
sets the range from the target's preferred size, which is nothing, and every resize of the
window scrolled the view back to the top. When a resize changes the number of columns in the
cover grid, `AlbumGridView::FrameResized` keeps the album that began the top row at the top.

Button glyphs come from Font Awesome 6 Free. `icons::Init()` copies the font shipped in the
package into the user font directory and calls `update_font_families()`, so the family is
available on any Haiku install; `icons::Draw()` centres a glyph on its ink box, since the icons
sit on the baseline with different amounts of space around them.

The MiniDisc cartridge is artwork, not a glyph: `resources/images/minidisc.png` (colour, for
the MiniDisc view and the display) and `minidisc-glyph.png` (one colour, tinted like a glyph
for the sidebar and the status bar) are PNG resources of the application. `icons::DrawPicture`
and `icons::DrawGlyph` scale them by averaging, because the pictures are several times larger
than they are shown. The application icon is an original rounded loudspeaker with a
flowing blue music note. `tools/make-icon.py` defines its geometry directly on the
64-unit Haiku canvas and writes the native HVIF plus a matching SVG. The speaker
has a metal mounting flange, rolled rubber surround, recessed cone and domed
dust cap; the note has one continuous curved
silhouette. A two-unit black outline, upper-left lighting and a rightward shadow
keep the icon legible at 16 and 32 pixels. It has no bitmap input.
`tools/render-icon.cpp` produces authoritative PNG previews using Haiku's
`BIconUtils`; `resources/ICON-ATTRIBUTION.md` records the MIT artwork.

Covers (grid, album list, artists view) and the artist picture lie on a soft shadow
(`DrawArtworkOnPage`, which calls `DrawSoftShadow`): a black picture whose alpha is the blurred
outline of the cover, made once per cover size and kept.

## The display and its pages

The LCD display in the toolbar shows the playing song and, besides it, *activities*
(`ToolbarView::Activity`: an id, a picture, headline, detail, a progress fraction or an
animated bar, labels, an optional cancel command, a finished state). The window adds or
updates them by id (`SetActivity`) and takes them away (`RemoveActivity`); today they are the
MiniDisc write or erase (`minidisc`) and the library scan (`scan`, from `kMsgScanProgress`
with `scan` set and the scanner's file counts). A finished scan leaves after a few seconds
(`ExpireActivity`).

The pages are the song (while one is playing or paused) followed by the activities. With more
than one page a round arrow beside the artwork and one dot per page appear; the arrow, a click
on the text or a dot turns the page. A MiniDisc write comes to the front when it starts and
again when it ends; the scan never pushes the song aside. A song that starts
playing is shown.

A click on the song page's artwork (`kMsgShowNowPlaying`) opens the playing song's album with
the song selected (`MainWindow::ShowNowPlaying`); a song the index has no album for yet (a
scan still running) shows the songs of its album in the play queue instead. The selection is
made by a message posted after the reload, so the list scrolls to the song once the window
has laid it out.

## Artist links

Clicking the picture or the name in the artists view's header (`kMsgOpenArtistPage`) opens
`https://musicbrainz.org/artist/<MBID>` with `BUrl::OpenWithPreferredApplication`, or a
MusicBrainz artist search when the MBID stays unknown. `ArtistLinks` keeps MBIDs in
`artist-mbids.json` in the cache folder. They are taken from the answers the artwork lookups
get anyway (the credited artist of a MusicBrainz release, TheAudioDB's `strMusicBrainzID`), or
looked up on the click: a MusicBrainz artist search that must find exactly one artist of that
name, then, for a name several artists share, a release search with one of the artist's
albums. A name that finds nothing is not asked about again for a week.

## Scanning

`Scanner::Run` works in two steps, each spread over `RunWorkers` threads (as many as there are
processors, two at least, eight at most; low priority, so playback and the windows stay ahead):

1. The walk. Workers take folders from a shared queue and add the subfolders they find.
   Every entry is looked at once: audio files are noted with size and modification time, a
   cover picture (`cover.jpg`, `folder.png`, …) with its folder.
2. The reading. Files whose size and time match the library are skipped; the others are read
   through `TagStream`, which serves TagLib from a few pieces it fetches itself: the first
   64 KiB, the last 4 KiB, whatever a caller asks for in one read, and growing read-ahead for
   what is read in sequence. TagLib's own stream reads a kilobyte at a time; on a network
   share each of those is a round trip. For MP4 files `ReadMp4Info` reads the length and the
   codec from the `moov` box first. Downloads from streaming services are fragmented: the
   length is in `mehd`, where TagLib does not look, and the sound follows in hundreds of
   `moof` boxes TagLib would visit one by one. For those the stream ends after `moov`.

Tracks go to the library 200 at a time; the index is rebuilt (and the views reload) every
three seconds at most. The artwork of an album is stored by the first worker that reads one
of its songs. Songs are only removed for folders that could be listed: a library folder that
cannot be opened or is empty (a share that is not mounted) keeps what the library knows.
On the owner's workstation a library of 21758 songs on an SMB share is read in under three
minutes; before, the same scan ran at about two songs a second.

## Threads and messaging

- The scanner and its workers, image cache, art store, local decoder and a progress ticker
  run on their own threads. They never touch views; they post BMessages
  (`player/Messages.h`) to the main window or the application.
- `Library` is guarded by a recursive mutex; views take a `Library::Locker` while they hold
  pointers into it. Node-based containers keep those pointers stable.
- Track completion is reported to the application looper (`kMsgTrackFinished`) so that the
  decoder is never stopped from its own thread.

## View modes

The toolbar's three buttons pick list, album list or cover grid. The library views and the
playlists each remember their own mode (`viewMode`, `playlistViewMode`). The albums, artists
and MiniDisc sources have a layout of their own: the buttons are hidden there
(`MainWindow::ViewModeApplies`, `ToolbarView::SetViewButtonsShown`) and the View menu's
shortcuts do nothing.

- The album list groups *consecutive* rows of one album (`TrackListView::Relayout`), which for
  the album-ordered library is every album once, and for a playlist keeps the playlist order.
  Groups carry their own name, artist and art key, taken from the song when it has no
  library album.
- The grid shows `AlbumCell`s: library albums, or for a playlist the albums found in it in
  order of first appearance, each holding only the playlist's songs of that album. Opening a
  library cell uses `kMsgShowAlbum`; a playlist cell sends its songs with `kMsgShowTracks`.
- Album and artist ids are numbered by `Library::RebuildIndex`, afresh each time, in the
  order the songs are met; track ids are database rows and stay. A scan rebuilds the index
  every few seconds, so the window keeps the opened album by artist and name
  (`ResolveShownAlbum`, through `Library::AlbumIdFor(artist, name)`) and the artists view
  keeps its selection by name (`ArtistIdFor`), finding them again after every reload.
- Rows of an editable playlist map to playlist positions (`fPositions`) when a search filter
  hides some of them, so remove and drag act on the right entries. Drag reordering is offered
  in the unfiltered list view only.

## Track quality

`Track::lossless` and `Track::bitrate` feed the quality label in the display. They come from
TagLib (FLAC, WAV, AIFF and ALAC-in-MP4 count as lossless; a missing bit rate is estimated
from size and duration). `scannerVersion` in the settings forces one full rescan when the
scanner starts storing new fields.

## Artwork pipeline

`ArtStore::Get` is called from the views' `Draw` for visible items only. A miss queues a
request in `ImageCache`, at the front; asking again for a queued key moves it to the front,
and the queue is capped so items scrolled past long ago fall out. Three online lookup workers
try the configured sources in order and retry multi-artist albums with the first artist
alone. Misses are retried after a week.

## Music Assistant, removed

Amp used to stream from a Music Assistant server (a Sendspin player, the server's library
and playlists, its image proxy). That was removed in `0.3.5~alpha-10` (#12); the code is in
the git history before that version. What older versions stored goes when a newer one
starts: `Library::Open` deletes the tracks and playlists with `source = 1`, the streamed songs
in local playlists and the `ma_albums` and `ma_artists` tables (the `source` and `ma_*`
columns of an old database stay, unused; a new database has none), `ImageCache::Open` the
pictures under `ma:`, `mau:` and `map:` keys, and the settings file loses the server account
the next time it is saved. A saved `selectedSource` of `ma` opens Music instead.
