# Playing on Sonos speakers

A proof of concept: one room at a time, no grouping, no Sonos queue. Amp stays the player:
it keeps the play queue, sends the room one song at a time and moves on when the room says
the song has ended.

## What the user sees

- The output button sits right of the volume slider: in the toolbar on the desktop, as a
  metal button at the right end of the top bar in the touch view (iPad Music has it there).
  Its glyph is a screen with a triangle (iTunes' AirPlay button); grey for this computer,
  a spinner while a room connects, blue while one plays.
- A press opens a list: **This Computer**, then the rooms. The rooms are searched for afresh
  each time the list opens; the ones found last time show meanwhile, with "Looking for Sonos
  speakers…" and a spinner below them. The output in use carries a blue check mark, a room
  that is connecting a spinner. The list closes after a choice, on a press anywhere else, on
  Escape or on a second press of the button.
- Choosing a room: this computer plays on until the room answers, then the song carries on in
  the room from where it was (paused if it was paused). The display shows "Connecting to …"
  with a ✕ that cancels, then "Playing on …". The volume slider now shows and sets the room's
  volume, and follows changes made in the Sonos app.
- Choosing the room again, or This Computer, stops the room and plays the song on here from
  where the room was; the slider shows this computer's volume again.
- A room that does not answer ("Cannot connect to …") or stops answering ("Lost the connection
  to …") is shown in the display (an alert in the touch view) and the song plays on here.
- Pause, play, next, previous and seeking work as on this computer. Pausing or resuming in the
  Sonos app is followed. When something else is played in that room, Amp stops.
- Quitting Amp stops the room (the songs it fetches go away with Amp).

## How it works

- `core/Sonos`: discovery sends an SSDP `M-SEARCH` for `urn:schemas-upnp-org:device:ZonePlayer:1`
  (twice; replies for 1.5 s) and asks the first player that answers for the household's
  `ZoneGroupTopology#GetZoneGroupState`. Rooms are its visible `ZoneGroupMember`s; satellites
  (a sub, surrounds), hidden members and bridges are not rooms. A room grouped with others
  plays what its group's coordinator plays, so transport calls go to the coordinator and
  volume calls to the room itself. The calls are plain SOAP over libcurl (`Http::Post`).
- `core/MediaServer`: an HTTP server on a free port of every interface. A song is published
  under `/amp/<token>/<n>.<ext>`, the token made at start, and only published files are
  served (GET and HEAD, byte ranges; the room seeks with them). The URL uses this computer's
  address on the route to the room (`sonos::LocalAddressFor`). Songs go as they are; the
  room is told the MIME type from the extension. The album's cover from the artwork cache
  goes along as `upnp:albumArtURI`.
- `player/SonosOutput`: one worker thread per connection makes the calls in order. It loads a
  song with `SetAVTransportURI` (DIDL-Lite metadata), `Seek` (before the sound starts, for a
  song moved over mid-way) and `Play`; it looks at `GetTransportInfo` and `GetPositionInfo`
  every second (every 300 ms near a song's end) and at the room's volume every three seconds.
  `STOPPED` after `PLAYING` is the song's end (`kMsgTrackFinished` with the player's
  generation); a song asked to play that never plays within six seconds is reported as one
  the room cannot play, and the player skips it. Three unanswered looks in a row end the
  connection. Volume changes from a drag are sent as one.
- `Player` hands the song over: `ConnectSonos` starts a worker; when the room has answered
  (`kSonosReady`) the local decoder stops and the song is loaded in the room at the local
  position. `DisconnectSonos` releases the worker (it stops the room on its own thread) and
  opens the song locally at the room's position. Everything the worker learns reaches the
  player as `kMsgSonosEvent` through the application looper; the window hears of each step
  as `kMsgOutputChanged`.

## Testing

- `make sonostool` (or compile `tools/sonostool.cpp` with `src/core` on any system with
  libcurl): `sonostool list`, `sonostool play <room> <file> [follow s] [start s]`,
  `sonostool volume <room> [0-100]`, `sonostool stop <room>`. It serves the file with
  `MediaServer`, so it checks the whole path a song takes.
- `make check` covers the topology parsing, the XML helpers, the DIDL-Lite metadata and the
  media server's ranges.
- `AMP_SONOS_EXTRA="Name=host;Name=host"` adds rooms the search does not find. An address
  nobody answers at (`192.0.2.10`) shows connecting, cancelling and failing without a speaker;
  that is how the Amp VM, whose user-mode network cannot be reached from the LAN, is tested.
- Tested on air/OS (the workstation on the owner's network, songs played in the Kitchen, a
  Sonos Amp) in a household of two Sonos Amps, a Beam and an Arc bonded with a Sub and a
  Connect:Amp (listed as four rooms): discovery, moving a song over mid-way, songs following
  one another, pause and resume from both sides, the volume both ways, MP3, AAC (ADTS and
  MP4), FLAC, Ogg Vorbis and WAV, disconnecting, quitting.
