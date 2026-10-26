# Amp

Amp is a native music player for Haiku styled after iTunes 8. It plays a local library
(MP3, MP4/M4A/AAC, FLAC, WAV, OGG) and streams tracks from a
[Music Assistant](https://www.music-assistant.io) server, registering itself as a player so the
server sends audio straight to the Haiku machine.

<!-- airos-ci:latest-builds:start -->
## Latest builds

Built automatically by air/OS CI from commit `f3696cd` on 2026-10-16 ([all files](https://github.com/jmgasper/amp/releases/tag/latest)).

| Architecture | Package |
|---|---|
| arm64 | [amp-0.3.5.alpha-5-arm64.hpkg](https://github.com/jmgasper/amp/releases/download/latest/amp-0.3.5.alpha-5-arm64.hpkg) |
| x86_64 | [amp-0.3.5.alpha-5-x86_64.hpkg](https://github.com/jmgasper/amp/releases/download/latest/amp-0.3.5.alpha-5-x86_64.hpkg) |

Install with `pkgman install <file>`, or copy the file into `/boot/system/packages`. Haiku SDK: x86_64 hrev60206-1454-g7af4af7708, arm64 hrev60206-1456-g7b9fb6a3e7.
<!-- airos-ci:latest-builds:end -->

## Features

- Local library scanned from configurable folders; tags read with TagLib, durations from the
  Media Kit when tags carry none, embedded and folder artwork picked up during the scan.
  Folders are listed and files are read by up to eight workers at once, each file in a few
  large reads, so a library of tens of thousands of songs on a network share is there in
  minutes.
- Music Assistant: username/password login, library sync (artists, albums, tracks,
  playlists), playback through the Sendspin player protocol, playlists mirrored back to the
  server. Streamed items carry a small **MA** badge everywhere. Switching Music Assistant
  off in the settings removes its songs, albums, artists and playlists from the library.
- Browse by song list, album-grouped list with artwork, album grid, artists, and playlists;
  instant search across title, artist and album. Playlists offer the same three views
  (list, album list, cover grid) and remember their own choice; the albums and artists views
  have their own layout and hide the view buttons. The column header lives in
  its own view above the table, so it stays put and is never redrawn over the rows while
  the list scrolls. Covers and artist pictures lie on a soft drop shadow. In the artists
  view, clicking the artist's picture or name opens the artist's MusicBrainz page in the
  web browser (a MusicBrainz search when the artist cannot be told apart by name).
- Playlists: create, rename, delete, add and remove songs, drag to reorder, drag songs or
  whole albums onto a playlist in the sidebar, "Sync to Music Assistant" per playlist.
- Artwork cache with online lookup through Deezer, MusicBrainz + Cover Art Archive,
  TheAudioDB and Discogs (order and keys configurable), stored under the user cache
  directory so the next launch is instant. Whatever is visible right now is fetched first.
- Transport with volume, seek bar, shuffle and repeat; play queue with "Play Next" and
  "Add to Up Next". The display shows the playing track's artwork, title, artist and album,
  elapsed and remaining time, the MA badge for streamed tracks, and the source quality
  ("Lossless" or the bit rate); a click on the artwork opens the song's album with the song
  selected. Longer jobs (a MiniDisc write, a library scan, a Music
  Assistant sync) get a page of their own in the display: while more than one thing is going
  on, the arrow beside the artwork, the dots under it or a click on the text turns between the
  song and each job, like iTunes' status display.
- MiniDisc: playlists, albums and selected songs can be written to a MiniDisc in a NetMD
  recorder, in SP. The recorder appears under DEVICES with the disc's contents and a capacity
  bar; the display shows iTunes-style progress with a cancel button. Amp asks before erasing
  a disc that has songs (or adds after them) and offers to write the songs that fit when a
  playlist is too long. See `docs/MINIDISC.md`.
- Quick Look: on air/OS, the space bar in Tracker plays a song in the Quick Look panel with
  Amp's own decoder and output, showing its artwork and tags with play/pause and a scrubber;
  a double click on the picture opens it in Amp. The add-on (`src/quicklook/`, shipped as
  `add-ons/quick_look/Amp`) is built when the SDK has `add-ons/quick_look/QuickLook.h`
  (`make QUICKLOOK=1` forces it).
- Library folders, the Music Assistant server and account and the artwork sources live in one
  Settings window, opened from the **Settings** button at the bottom right of the main window
  or File > Settings….
- The toolbar, status bar, sidebar and list buttons draw their glyphs from Font Awesome 6
  Free (SIL OFL 1.1, icons CC BY 4.0). The font ships inside the package and is registered with
  the font server on first run, so the buttons look the same on a bare Haiku install.

## Building on Haiku R1/beta6

```sh
pkgman install curl_devel sqlite_devel taglib2_devel
make -j6            # build-haiku/Amp
make package        # artifacts/amp-<version>-x86_64.hpkg
```

The development VM workflow (sync sources, build, screenshots) is described in
`docs/VM.md`; the code layout in `docs/ARCHITECTURE.md`; MiniDisc writing, its tools and
tests in `docs/MINIDISC.md`. `make check` runs the core unit tests, `make mdtool` builds a
command-line NetMD tool and `make scantool` one that times the library scanner on real
folders (`build-haiku/scantool [-j workers] folder...`, with a scratch library). `make icon`
rebuilds the vector application icon and prepares the MiniDisc pictures in
`resources/images` from `resources/branding/source` (Python 3 with Pillow). `tools/ws.sh` and `tools/ws-build.sh` do for the owner's
workstation (a bare-metal Haiku machine with the test recorder) what `tools/haiku.sh` and
`tools/sync-build.sh` do for the VM.

## Music Assistant notes

Amp talks to Music Assistant 2.10 through its HTTP API (`POST /api`) and connects to the
authenticated `/sendspin` WebSocket proxy as an unencrypted ("transition mode") Sendspin
player, which the server allows by default (*Allow legacy clients* in the Sendspin provider
settings). Music Assistant wraps that player in a "universal player" carrying the name
configured in Settings; Amp sends play and queue commands to that wrapper. Only the
built-in username/password login provider is supported.

Tracks whose music provider is not loaded on the server (for example a network share that
is offline) stay in the library but cannot be played; Music Assistant answers "there is
nothing to play here" and Amp reports it in the status bar.

`tools/fake-ma-server.py` is a small stand-in server used to exercise the integration
without a real installation.

## License

MIT. Bundles the nlohmann JSON library (MIT, see `vendor/nlohmann`) and the Font Awesome 6
Free font (fonts SIL OFL 1.1, icons CC BY 4.0, see `vendor/fontawesome`).

## Native application icon

The icon was redesigned on 26 October 2026 as an original loudspeaker cabinet and music note.
It follows the [Haiku icon guidelines](https://www.haiku-os.org/development/icon-guidelines):
upright edges, perspective with 2:1 front and 1:1 receding floor-contact edges,
light from the upper left, darker right faces, a rightward floor shadow and
a black silhouette outline one pixel wide at 32px. Broad details target 16px
and 32px. The artwork is MIT-licensed and drawn directly in vectors, without
fonts, traced bitmaps or vendor artwork.

```sh
python3 tools/make-icon.py
```

This regenerates `resources/branding/amp-icon.hvif` and its matching `.svg`.
The `.rdef` imports the HVIF for the app, Tracker and Deskbar. Optional positional
arguments select an output HVIF and a host preview PNG (the latter needs Pillow).
For authoritative previews on Haiku, use the same native renderer as Tracker:

```sh
g++ -std=c++17 tools/render-icon.cpp -o /tmp/render-amp-icon -lbe -ltranslation
/tmp/render-amp-icon resources/branding/amp-icon.hvif resources/branding/amp-icon-preview.png 256
```

The checked-in PNG preview is a native render. Check changes at 16, 32, 64 and
128px on light and dark backgrounds before replacing the icon.
[Artwork attribution](resources/ICON-ATTRIBUTION.md) records the files and licence.
Package version `0.3.5~alpha-6` includes this update.
