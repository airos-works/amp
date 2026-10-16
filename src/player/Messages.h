// Message constants shared between the player, the application and the windows.
#pragma once
#include <SupportDefs.h>

namespace amp {

enum {
    // player -> UI
    kMsgPlayerStateChanged = 'plst',  // "state" int32 (PlayerState), "track" int64
    kMsgPlayerProgress = 'plpr',      // "position" int64 ms, "duration" int64 ms
    kMsgPlayerError = 'pler',         // "error" string
    kMsgPlayerVolume = 'plvo',        // "volume" float
    kMsgMAStatus = 'mast',            // "connected" bool, "message" string, "syncing" bool (library fetch running)
    kMsgTrackFinished = 'trfn',       // player -> app: "generation" int32
    // library / background -> UI
    kMsgLibraryChanged = 'lbch',
    kMsgScanProgress = 'scpr',        // "text" string, "done" bool; from the library scan also
                                      // "scan" true, "processed" and "total" int32
    kMsgArtReady = 'arrd',            // "key" string, "path" string
    kMsgMASyncDone = 'masd',          // "error" string
    kMsgMACleared = 'macl',           // Music Assistant was switched off and its content removed
    kMsgPlaylistLoading = 'plld',     // "playlist" int64, "loading" bool
    // UI actions
    kMsgPlayPause = 'play',
    kMsgStop = 'stop',
    kMsgNext = 'next',
    kMsgPrevious = 'prev',
    kMsgSeek = 'seek',                // "position" int64 ms
    kMsgVolumeChanged = 'volc',       // "volume" float
    kMsgViewMode = 'view',            // "mode" int32
    kMsgSearch = 'srch',              // "text" string
    kMsgSourceSelected = 'srcs',      // "source" string, "playlist" int64
    kMsgPlayTracks = 'pltr',          // "tracks" int64[], "index" int32
    kMsgNewPlaylist = 'nwpl',
    kMsgNewPlaylistNamed = 'nwpn',    // "name" string, "tracks" int64[]
    kMsgDeletePlaylist = 'dlpl',      // "playlist" int64
    kMsgRenamePlaylist = 'rnpl',      // "playlist" int64, "name" string
    kMsgAddToPlaylist = 'adpl',       // "playlist" int64, "tracks" int64[]
    kMsgRemoveFromPlaylist = 'rmpl',  // "playlist" int64, "positions" int32[]
    kMsgMovePlaylistTracks = 'mvpl',  // "playlist" int64, "positions" int32[], "target" int32
    kMsgSyncPlaylistToMA = 'sypl',    // "playlist" int64, "enable" bool
    kMsgShowSettings = 'sett',
    kMsgSettingsChanged = 'setc',
    kMsgRescan = 'rscn',
    kMsgMAResync = 'masy',
    kMsgToggleShuffle = 'shuf',
    kMsgToggleRepeat = 'rept',
    kMsgShowAlbum = 'shal',           // "album" int64
    kMsgShowArtist = 'shar',          // "artist" int64
    kMsgOpenArtistPage = 'arpg',      // "artist" int64: the artist on MusicBrainz, in the web browser
    kMsgShowTracks = 'shtr',          // "tracks" int64[]: open these songs as an album list
    kMsgShowNowPlaying = 'shnp',      // the playing song in its album (a click on the display's artwork)
    kMsgSelectionChanged = 'selc',
    kMsgRowsDropped = 'rdrp',
    kMsgTrackDrag = 'trdg',           // dragged tracks: "tracks" int64[], "playlist" int64 (source)
    // MiniDisc
    kMsgMDState = 'mdst',             // recorder or disc changed: read MiniDiscManager::State()
    kMsgMDProgress = 'mdpr',          // "phase", "track", "count" int32, "title", "name" string,
                                      // "fraction", "track_fraction" float, "eta" int32 s (-1 unknown)
    kMsgMDFinished = 'mdfn',          // "ok", "cancelled", "full", "erase" bool, "error", "name" string,
                                      // "written", "failed", "count" int32, "duration" int64 ms
    kMsgWriteToMiniDisc = 'mdwr',     // "tracks" int64[], "name" string, "kind" string (playlist, album, songs)
    kMsgMDCancel = 'mdcn',
    kMsgMDErase = 'mder',
    kMsgMDRefresh = 'mdrf',
    kMsgMDClearStatus = 'mdcl',       // the display goes back to the playing song
};

enum PlayerState { kStopped = 0, kPlaying = 1, kPaused = 2, kLoading = 3 };

} // namespace amp
