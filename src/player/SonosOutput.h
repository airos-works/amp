// Playback on a Sonos room. One worker thread makes the speaker's UPnP calls in order
// (they wait on the network, so neither the window nor the player ever does) and follows
// what it plays: position, pause and play from elsewhere, the end of a song, the volume.
// What it learns goes to the application looper as kMsgSonosEvent (and kMsgTrackFinished),
// which hands it to the Player.
#pragma once
#include "core/Sonos.h"
#include <Message.h>
#include <Messenger.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace amp {

class MediaServer;

// What a speaker is told about a song.
struct RemoteTrack {
    std::string path;
    std::string title, artist, album;
    std::string artPath;            // a picture file for the speaker's display, or empty
    int64_t durationMs = 0;
};

// kMsgSonosEvent's "event".
enum SonosEvent {
    kSonosReady,        // the room answered: "volume" float (0..1)
    kSonosFailed,       // the room cannot be reached (any more): "error"
    kSonosLoaded,       // a song went over: "generation", "initial" bool, "ok" bool, "error"
    kSonosTransport,    // the room was paused, resumed or stopped from elsewhere: "generation", "state"
    kSonosVolume,       // the room's volume changed from elsewhere: "volume" float
    kSonosError,        // the room refused a call (it is still there): "error"
};

class SonosOutput {
public:
    SonosOutput(const SonosDevice& device, const std::shared_ptr<MediaServer>& server,
        const BMessenger& control, int32 session);
    // Releases without stopping the room, if Release() has not been called.
    ~SonosOutput();

    const SonosDevice& Device() const { return fDevice; }
    int32 Session() const { return fSession; }

    // Asks for the room's volume: kSonosReady or kSonosFailed.
    void Connect();
    // Sends a song from `startMs` on, playing or paused: kSonosLoaded. Songs that end are
    // reported as kMsgTrackFinished with `generation`.
    void Load(int32 generation, const RemoteTrack& track, int64_t startMs, bool play, bool initial);
    void Play();
    void Pause();
    void Stop();
    void Seek(int64_t positionMs);
    // 0..1; a drag's many changes are sent as one.
    void SetVolume(float volume);
    float Volume() const;           // last known, 0..1 (below zero: not known yet)
    int64_t PositionMs() const;     // follows the room between its reports

    // Lets the worker go: it stops the room first when asked, then ends. With `wait` this
    // returns when it has (at shutdown); otherwise the worker finishes on its own.
    void Release(bool stopPlayback, bool wait);

    // A device list in a message and back (discovery to the window to the player).
    static void AddDevice(BMessage& message, const SonosDevice& device);
    static std::vector<SonosDevice> Devices(const BMessage& message);
    // Looks for rooms on a thread of its own and sends kMsgSonosDevices to `target`.
    static void StartDiscovery(const BMessenger& target);

private:
    struct Shared;
    static void Run(std::shared_ptr<Shared> shared);

    SonosDevice fDevice;
    int32 fSession;
    std::shared_ptr<Shared> fShared;
    std::thread fThread;
    bool fReleased = false;
};

} // namespace amp
