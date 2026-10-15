# Writing to a MiniDisc

Amp writes playlists, albums and hand-picked songs to a MiniDisc in a NetMD recorder
connected by USB (Sony MZ-N/NE/NF/DN portables, NetMD decks, Sharp and Panasonic units;
the full list is `kKnownDevices` in `src/core/NetMD.cpp`). Tracks are recorded in SP: Amp
sends 16-bit 44.1 kHz PCM and the recorder does the ATRAC encoding itself.

## Using it

- When a recorder is plugged in, a **DEVICES** section appears in the sidebar with the disc
  (its title, or "MiniDisc"). Selecting it shows the disc: title, recorder, a capacity bar
  (used, being added, free, in SP time) and the tracks with their length and mode.
- To write: right-click a playlist in the sidebar or an album in the grid and choose **Write
  to MiniDisc…**, choose **Write Songs to MiniDisc…** on selected songs, press **Write to
  MiniDisc** in the status bar (shown for playlists and albums, like iTunes' *Burn Disc*),
  or drag songs or an album onto the MiniDisc in the sidebar or onto the disc view.
- If the disc already has songs, Amp asks whether to **Erase and Write** or **Add to Disc**
  (after the songs already there). Return picks *Add to Disc*; erasing takes a click.
- If the songs do not fit, Amp says how long they are and how much room there is, and offers
  to **Write Songs That Fit**: the songs in order, up to the first one that does not fit.
- Songs streamed from Music Assistant have no local file and are left out (Amp says so
  before starting). Missing files are left out the same way.
- While writing, the display shows the MiniDisc, "Writing “name” to the MiniDisc", the song
  being written, overall progress and the time left, and an ✕ button. The sidebar entry shows
  a progress pie and the disc view marks each song as waiting, converting, writing or done.
  ✕ stops after the song being transferred: once a song's length has been announced to the
  recorder, it waits for all of it.
- **Erase Disc…** (disc view, or the sidebar entry's context menu) empties the disc after
  asking.
- When a write or an erase has finished, the display shows the outcome for a few seconds
  and a system notification (Amp's icon, the "MiniDisc" title and the outcome in one line)
  says so wherever the window is: covered, on another workspace, minimized.

Capacity: the recorder reports free space in SP time. A song takes its length rounded up to
whole clusters (2.0434 s each) plus one more cluster, which matches the recorder exactly (five
songs of 3:05 took 3:22 of disc, twelve of 6:25 took 7:05).

Titles: the disc gets the playlist name, or "Artist - Album" for an album. Songs keep their
titles on an album disc; on a mixed disc they become "Artist - Title". MiniDisc titles are
single-byte: accents are dropped (Café → Cafe, Straße → Strasse), typographic punctuation
becomes ASCII and anything else (CJK, emoji) is left out. The TOC holds 255 cells of seven
characters for all titles together; when a long playlist would overflow it, the longest
titles are shortened evenly.

## How it works

| File | Role |
| --- | --- |
| `src/core/NetMD.*` | The protocol: command encoding, reply parsing, disc and track queries, titles, erase, and the secure download (session key from the open-source EKB, DES-CBC encrypted PCM). Also the planning helpers (title sanitising and budget, capacity) and the device list. Portable. |
| `src/core/Des.*` | DES and two-key triple DES, table driven (~85 MB/s). |
| `src/core/NetMDSimulator.*` | A recorder in software, for the unit tests and `AMP_NETMD_SIMULATE`. |
| `src/core/Resampler.*` | Kaiser-windowed sinc resampler (about −90 dB aliasing) for sources that are not 44.1 kHz. |
| `src/core/FlacDecoder.*`, `AlacDecoder.*`, `BitReader.h` | FLAC and Apple Lossless packet decoders (see "Haiku issues" below). |
| `src/player/NetMDUsb.*` | USB Kit transport (vendor control transfers, bulk OUT) and a `BUSBRoster` for hot-plugging. |
| `src/player/MiniDiscPcm.*` | Decodes any local file to 44.1 kHz 16-bit big-endian stereo PCM in a temporary file. 16-bit 44.1 kHz sources pass through bit-exact; everything else is dithered (TPDF). |
| `src/player/MiniDisc.*` | `MiniDiscManager`: one worker thread owns the recorder, polls the disc every four seconds, runs writes (converting the next song while the current one uploads) and erases, and reports through `kMsgMD*` messages. |
| `src/ui/MiniDiscView.*`, `MainWindowMiniDisc.cpp` | The disc view and the window's write flow and progress display. |

A write, per song: wait for the recorder to be idle, acquire it, turn off new-track
protection, open a secure session (send the EKB, exchange nonces, derive the session key with
a retail MAC), announce the upload (format, length), send a 24-byte header and the
DES-CBC encrypted PCM over the bulk pipe, then title and commit the track, close the session
and release the recorder. Capacity is checked from the recorder's own figures before each
song.

The protocol was implemented from the documentation that the linux-minidisc and netmd-js
projects provide (both GPL). No code was taken from them. The EKB values and the device IDs
are interoperability data that every NetMD implementation uses.

## Verified hardware

Sony MZ-DN430 (USB 054c:00ca, the MZ-NE410 family) on the owner's X399 workstation: an album
written with *Erase and Write* and a mixed playlist (320k MP3, 24-bit 48 kHz FLAC, mono AAC)
added after it, both at about 1.6× real time, titled, and read back from the recorder with the
right lengths. Quitting Amp with the recorder connected leaves it answering.

## Troubleshooting

- **The recorder drops off USB during a write.** Check its battery or AC adapter: a portable
  that cannot power its recording laser stops and disconnects. Try another USB port as well.
  On the X399 the first attempts failed on the ASMedia-designed controllers (the ASM2142 card
  and the chipset's ports), where every USB transaction waits for a 1 ms frame; the writes
  then worked on a port of the CPU's controller with a fresh battery. The ASMedia ports were
  not tried again with the fresh battery, so which of the two it was is open.
  `mdtool latency` shows the median time of a control transfer (about 1.4 ms on the working
  port); it also depends on how busy the recorder is, so it is a hint, not a verdict.
- **"The MiniDisc is write-protected"**: slide the record tab on the disc's edge so the hole is
  closed. The disc view warns about it and the sidebar entry shows a lock.
- **The recorder stops answering after a program was killed while talking to it.** It has to
  be reconnected. Amp waits for the command in progress (up to five seconds) when it quits.

## Testing

- `make check` runs the core tests, including a full write and read-back against the simulator.
- `make mdtool` builds `build-haiku/mdtool`: `mdtool info|list|latency|erase|title <text>|write <file> [title]|pcm <file> <out>`.
  `MDTOOL_LOG=1` prints every command and reply; `mdtool latency` times control transfers.
- `AMP_NETMD_SIMULATE=4 build-haiku/Amp` adds a simulated recorder (three songs on an
  80-minute disc, uploads at 4× real time) so the whole interface can be tried without
  hardware.
- `python3 tools/make-md-test-library.py DIR` generates albums that cover the conversion
  paths (320k MP3, 24-bit 48 kHz FLAC, mono AAC, Vorbis) and a 96-minute album that does
  not fit on an 80-minute disc.

## Haiku issues found on the way

- **Lossless files lose their ending.** Haiku's ffmpeg media plugin decodes FLAC, ALAC and
  WavPack frame-threaded and never drains the decoder at the end of the stream, so the last
  frames never come out: on the 32-thread workstation FLAC and ALAC files lose about three
  seconds and WavPack half the file. MP3, AAC, Vorbis, Opus and WAV are complete. Amp therefore
  lets the Media Kit only demux FLAC and ALAC (`BMediaTrack::ReadChunk`) and decodes the
  packets itself, bit-exact with ffmpeg. Playback through `LocalDecoder` still uses the Media
  Kit and still loses those seconds.
- **Raw USB transfers cannot be interrupted.** A bulk transfer to a recorder that stops
  answering blocks its thread in the kernel, and when the device is removed the xHCI driver
  may fail to find the pending transfer to cancel it, so the thread (and the team) cannot be
  killed. Amp keeps all device work on one worker thread and never blocks the window on it;
  when quitting it gives the worker five seconds to finish its exchange, then leaves it.
