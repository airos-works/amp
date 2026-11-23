// Sonos speakers on the local network: discovery (SSDP, then the household's zone
// topology) and the UPnP calls Amp plays through (AVTransport, RenderingControl).
// Portable C++ over POSIX sockets and libcurl; no Haiku headers.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace amp {

// A room as the Sonos app lists it. Bonded speakers (a sub, surrounds, the other half of a
// stereo pair) are part of their room and not listed. A room grouped with others plays
// what its group's coordinator plays, so the transport goes to the coordinator.
struct SonosDevice {
    std::string uuid;              // RINCON_...
    std::string name;              // the room
    std::string host;              // the room's own player: its volume
    int port = 1400;
    std::string coordinatorUuid;
    std::string coordinatorHost;   // where the transport calls go
    int coordinatorPort = 1400;
};

namespace sonos {

// Asks the network for Sonos players and returns the household's rooms, sorted by name.
// Takes about `waitMs`. An empty list and `error` set when the search itself failed.
std::vector<SonosDevice> Discover(int waitMs, std::string* error = nullptr);

// The rooms in a ZoneGroupTopology GetZoneGroupState answer (`state` unescaped once:
// the <ZoneGroupState> document itself). Public for the tests.
std::vector<SonosDevice> ParseZoneGroupState(const std::string& state);

// A SOAP call's outcome: `ok`, or the reason. `unreachable` when the speaker did not answer
// at all (as opposed to refusing the call, which carries a UPnP error code).
struct Result {
    bool ok = false;
    bool unreachable = false;
    int upnpError = 0;
    std::string error;
    std::string body;              // the answer, for the getters
};

// What a player reports about its transport.
struct TransportInfo {
    std::string state;             // PLAYING, PAUSED_PLAYBACK, STOPPED, TRANSITIONING
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    std::string uri;               // TrackURI
};

// The calls, to one player (host:port).
class Client {
public:
    Client() = default;
    Client(const std::string& host, int port) : fHost(host), fPort(port) {}

    Result SetAVTransportURI(const std::string& uri, const std::string& metadata);
    Result Play();
    Result Pause();
    Result Stop();
    Result Seek(int64_t positionMs);
    Result GetTransportInfo(TransportInfo& info);   // state only
    Result GetPositionInfo(TransportInfo& info);    // position, duration, uri
    Result GetVolume(int& volume);                  // 0..100
    Result SetVolume(int volume);
    Result GetZoneGroupState(std::string& state);

    const std::string& Host() const { return fHost; }

private:
    Result Call(const char* path, const char* service, const char* action, const std::string& arguments,
        int timeoutSeconds = 5);

    std::string fHost;
    int fPort = 1400;
};

// The DIDL-Lite description Sonos shows for a song served over HTTP.
std::string TrackMetadata(const std::string& uri, const std::string& mimeType, const std::string& title,
    const std::string& artist, const std::string& album, const std::string& artUri, int64_t durationMs);

// "H:MM:SS" <-> milliseconds; NOT_IMPLEMENTED and the like read as 0.
std::string FormatTime(int64_t ms);
int64_t ParseTime(const std::string& text);

// XML helpers (public for the tests): the text of the first <tag> (any namespace prefix),
// entities resolved; entity escaping for text and attributes.
std::string TagText(const std::string& xml, const std::string& tag);
std::string XmlEscape(const std::string& text);
std::string XmlUnescape(const std::string& text);

// The address of this machine on the route to `host`: what a speaker there reaches us at.
std::string LocalAddressFor(const std::string& host);

} // namespace sonos
} // namespace amp
