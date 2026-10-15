# Amp architecture

```
src/core     portable C++17 (no Haiku headers except in Scanner's TagLib use)
  Model.h            Track / Album / Artist / Playlist structs, art keys
  Settings           JSON settings file (library folders, MA account, art sources, UI state)
  Library            SQLite store + in-memory index (albums/artists derived from tracks)
  Scanner            folder walk, TagLib tags, embedded/folder art, optional duration probe
  TagStream          the file access TagLib reads through: few large reads; MP4 box reader
  Workers            a handful of threads for one job (Kernel Kit threads on Haiku)
  ImageCache         on-disk artwork cache + fetch worker (MA proxy, online providers)
  ArtProviders       MusicBrainz/CAA, TheAudioDB, Discogs lookups
  ArtistLinks        an artist's MusicBrainz page: MBIDs seen in lookups or asked by name, cached
  Http               libcurl wrapper
  WebSocket          minimal RFC 6455 client
  TimeFilter         Kalman clock filter (port of the Sendspin reference)
  Sendspin           Sendspin player@v1 client (transition mode through MA's /sendspin proxy)
  MusicAssistant     MA HTTP API client: login, library, playlists, player/queue commands
  Queue              play queue with shuffle/repeat
  NetMD              MiniDisc recorder protocol: commands, secure download, title/capacity planning
  NetMDSimulator     a recorder in software (tests, AMP_NETMD_SIMULATE)
  Des                DES / two-key 3DES for NetMD
  Resampler          windowed-sinc sample rate conversion
  FlacDecoder, AlacDecoder   lossless packet decoders (Haiku's own drop the last frames)
src/player   Haiku Media Kit
  AudioOutput        BSoundPlayer + ring buffer + volume
  LocalDecoder       BMediaFile/BMediaTrack decode thread
  SendspinAudio      schedules Sendspin PCM chunks into the output at their local play time
  Player             controller: queue, engine switching, MA player registration, progress
  NetMDUsb           USB Kit transport and roster for NetMD recorders
  MiniDiscPcm        any local file -> 44.1 kHz 16-bit big-endian PCM for SP recording
  MiniDisc           MiniDiscManager: recorder state, write and erase jobs on one worker thread
src/quicklook  air/OS Quick Look add-on (add-ons/quick_look/Amp): LocalDecoder + AudioOutput +
               TagLib artwork and tags in Tracker's space-bar panel; built from these sources
               as position independent code when the SDK has add-ons/quick_look/QuickLook.h
src/ui       Haiku Interface Kit
  App                BApplication: owns everything, background scan/sync jobs
  MainWindow         toolbar / sidebar / content cards / status bar
  ToolbarView        transport, volume, LCD display (song and activity pages), view switcher, search
  SidebarView        LIBRARY and PLAYLISTS sources, drop target, context menu
  TrackListView      table with album-grouped mode, sorting, selection, drag & drop
  AlbumGridView      album cover grid
  ArtistsView        artist list + header (links to MusicBrainz) + grouped track list
  StatusBarView      +/shuffle/repeat, summary, MA indicator, Settings button
  Icons              Font Awesome glyphs: registers the bundled font, draws it centred;
                     the MiniDisc pictures from the resources, scaled and tinted
  SettingsWindow     Library / Music Assistant / Artwork tabs
  ArtStore           scaled BBitmap cache fed by ImageCache
  MiniDiscView       the DEVICES source: disc summary, capacity bar, tracks and pending songs
  MainWindowMiniDisc the write flow (questions, job) and its progress in the LCD and sidebar
```

MiniDisc support is described in `MINIDISC.md`.

The column header is deliberately *not* part of the scrolled table: `TrackHeaderView` is a
sibling of the `BScrollView`, so scrolling blits only the rows while the header is painted once
and stays put. `TrackListView` still owns the columns and hands them to the header through
`DrawHeader()`/`HeaderMouseDown()`, keeping it in step with `InvalidateHeader()`.

Button glyphs come from Font Awesome 6 Free. `icons::Init()` copies the font shipped in the
package into the user font directory and calls `update_font_families()`, so the family is
available on any Haiku install; `icons::Draw()` centres a glyph on its ink box, since the icons
sit on the baseline with different amounts of space around them.

The MiniDisc cartridge is artwork, not a glyph: `resources/images/minidisc.png` (colour, for
the MiniDisc view and the display) and `minidisc-glyph.png` (one colour, tinted like a glyph
for the sidebar and the status bar) are PNG resources of the application. `icons::DrawPicture`
and `icons::DrawGlyph` scale them by averaging, because the pictures are several times larger
than they are shown. The application icon, a chrome loudspeaker with a glossy blue note in
front of it, is a vector drawing: `tools/make-icon.py` builds the HVIF from ellipses fitted to
`resources/branding/source/amp-icon.png` and gradients sampled from it, traces the note, and
keeps the screws, highlights and the note's shadow for 32 or 64 pixels and up (level of detail).
`make-icon.py --import picture.png` turns new artwork on a white background into that source.

Covers (grid, album list, artists view) and the artist picture lie on a soft shadow
(`DrawArtworkOnPage`, which calls `DrawSoftShadow`): a black picture whose alpha is the blurred
outline of the cover, made once per cover size and kept.

## The display and its pages

The LCD display in the toolbar shows the playing song and, besides it, *activities*
(`ToolbarView::Activity`: an id, a picture, headline, detail, a progress fraction or an
animated bar, labels, an optional cancel command, a finished state). The window adds or
updates them by id (`SetActivity`) and takes them away (`RemoveActivity`); today they are the
MiniDisc write or erase (`minidisc`), the library scan (`scan`, from `kMsgScanProgress` with
`scan` set and the scanner's file counts) and the Music Assistant library fetch (`ma-sync`,
from `kMsgMAStatus` with `syncing`). A finished scan or sync leaves after a few seconds
(`ExpireActivity`).

The pages are the song (while one is playing or paused) followed by the activities. With more
than one page a round arrow beside the artwork and one dot per page appear; the arrow, a click
on the text or a dot turns the page. A MiniDisc write comes to the front when it starts and
again when it ends; the scan and the sync never push the song aside. A song that starts
playing is shown.

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

- The scanner and its workers, image cache, art store, Sendspin client, Sendspin feeder,
  local decoder and a progress ticker run on their own threads. They never touch views; they post BMessages
  (`player/Messages.h`) to the main window or the application.
- `Library` is guarded by a recursive mutex; views take a `Library::Locker` while they hold
  pointers into it. Node-based containers keep those pointers stable.
- Track completion is reported to the application looper (`kMsgTrackFinished`) so that no
  engine is ever stopped from its own thread.

## View modes

The toolbar's three buttons pick list, album list or cover grid. The library views and the
playlists each remember their own mode (`viewMode`, `playlistViewMode`).

- The album list groups *consecutive* rows of one album (`TrackListView::Relayout`), which for
  the album-ordered library is every album once, and for a playlist keeps the playlist order.
  Groups carry their own name, artist and art key because playlist tracks fetched from a
  provider have no library album.
- The grid shows `AlbumCell`s: library albums, or for a playlist the albums found in it in
  order of first appearance, each holding only the playlist's songs of that album. Opening a
  library cell uses `kMsgShowAlbum`; a playlist cell sends its songs with `kMsgShowTracks`.
- Album and artist ids are numbered by `Library::RebuildIndex`, afresh each time, in the
  order the songs are met; track ids are database rows and stay. A scan rebuilds the index
  every few seconds, so the window keeps the opened album by artist and name
  (`ResolveShownAlbum`, through `Library::AlbumIdFor(artist, name, ma)`) and the artists view
  keeps its selection by name (`ArtistIdFor`), finding them again after every reload.
- Rows of an editable playlist map to playlist positions (`fPositions`) when a search filter
  hides some of them, so remove and drag act on the right entries. Drag reordering is offered
  in the unfiltered list view only.

## Track quality

`Track::lossless` and `Track::bitrate` feed the quality label in the display. Local files get
them from TagLib (FLAC, WAV, AIFF and ALAC-in-MP4 count as lossless; a missing bit rate is
estimated from size and duration). Music Assistant tracks take them from the provider
mappings' `audio_format`; MP4 containers report codec "?" there, so a bit rate of 600 kbit/s
or more, or more than 16 bits, is read as ALAC. While a stream runs, the queue item's
`streamdetails.audio_format` overrides the library figures. `scannerVersion` in the settings
forces one full rescan when the scanner starts storing new fields.

## Artwork pipeline

`ArtStore::Get` is called from the views' `Draw` for visible items only. A miss queues a
request in `ImageCache`, at the front; asking again for a queued key moves it to the front,
and each queue is capped so items scrolled past long ago fall out. Direct downloads (Music
Assistant image proxy, remote CDN URLs) run on three workers of their own. When the server
cannot deliver a picture (HTTP 404, typically because the provider that owns the file is
offline) the request moves to the online lookup workers, which try the configured sources in
order and retry multi-artist albums with the first artist alone. Failed Music Assistant keys
are retried after 15 minutes, other misses after a week.

## Switching Music Assistant off

`AmpApp::DisconnectMusicAssistant` stops a streamed song, closes the Sendspin client and calls
`Library::ClearMusicAssistantData`: streamed tracks, server playlists and the album and
artist records go, in whole-table statements. Local playlists keep their local songs; one
that was mirrored to the server is unlinked, since a mirror would replace the server's copy
with what is left of it. The play queue drops the songs that are gone (`DropMissingTracks`)
and the window leaves the Music Assistant source (`kMsgMACleared`). The sidebar entry, the
status bar indicator and the "Sync to Music Assistant" menu item are shown only while
Music Assistant is switched on. Workers that were fetching from the server when the switch
was turned apply their results through `WhileMAEnabled`, which refuses them. A library that
still holds streamed content at start-up while Music Assistant is off is cleared before the
window opens.

## Music Assistant playback

1. `App::ConnectMusicAssistant` logs in (`POST /auth/login`), stores the token, and calls
   `Player::EnableMusicAssistant`, which starts the Sendspin client.
2. The client opens `ws://host:port/sendspin`, sends `{"type":"auth","token":…}` then an
   unencrypted `client/hello` (player@v1, PCM 16-bit 44.1/48 kHz), runs the time filter and
   reports `client/state` with `available: true`.
3. Music Assistant wraps the Sendspin client (a "protocol" player) in a universal player
   that owns the queue; `MusicAssistant::ResolvePlayerId` finds it through our player's
   `active_source`. Playing an MA track calls `player_queues/play_media` for that id; the server
   streams PCM chunks (9-byte header: type 4 + big-endian int64 server timestamp) which
   `SendspinAudio` schedules through the shared `AudioOutput`. `stream/end` advances the
   Amp queue; pause/seek/stop map to `players/cmd/*`.
4. Artwork for MA items is fetched from `/imageproxy/<proxy_id>?size=512&fmt=jpeg` and cached
   like everything else.
5. Server replies carry explicit nulls, so all fields are read through the type-checked
   helpers in `MusicAssistant.cpp`; every parse path is wrapped so bad data never aborts
   the app.
6. Playlist contents are fetched when a playlist is opened (cached tracks show at once, a
   refresh runs in the background). Asking the `library` pseudo provider for the tracks can
   return nothing because the server picks the playlist's first provider mapping even when
   that instance is gone, so `FetchLibraryPlaylistTracks` reads the mappings from
   `music/playlists/get` and asks the available ones directly. Tracks that only exist on a
   provider (`tidal--…://track/…`) are stored with `inLibrary = false`: playable inside the
   playlist, hidden from the Music, Albums and Artists views.
