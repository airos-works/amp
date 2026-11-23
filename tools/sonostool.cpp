// Sonos without the application: list the rooms, play a local file on one through Amp's
// media server, and follow it. Portable: builds on Haiku and on a development host.
//   sonostool list
//   sonostool play <room> <file> [seconds to follow] [start seconds]
//   sonostool volume <room> [0-100]
//   sonostool stop <room>
#include "core/MediaServer.h"
#include "core/Sonos.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

using namespace amp;

static bool FindRoom(const char* name, SonosDevice& device)
{
    std::string error;
    std::vector<SonosDevice> devices = sonos::Discover(1500, &error);
    for (const SonosDevice& candidate : devices)
        if (strcasecmp(candidate.name.c_str(), name) == 0) {
            device = candidate;
            return true;
        }
    fprintf(stderr, "no room \"%s\" (%zu found%s%s)\n", name, devices.size(), error.empty() ? "" : ": ",
        error.c_str());
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s list | play <room> <file> [follow s] [start s] | volume <room> [v] | stop <room>\n",
            argv[0]);
        return 2;
    }
    std::string command = argv[1];
    if (command == "list") {
        std::string error;
        std::vector<SonosDevice> devices = sonos::Discover(1500, &error);
        if (!error.empty())
            fprintf(stderr, "error: %s\n", error.c_str());
        for (const SonosDevice& device : devices)
            printf("%-24s %s %s:%d  coordinator %s %s\n", device.name.c_str(), device.uuid.c_str(),
                device.host.c_str(), device.port, device.coordinatorUuid.c_str(), device.coordinatorHost.c_str());
        return devices.empty() ? 1 : 0;
    }
    if (argc < 3)
        return 2;
    SonosDevice device;
    if (!FindRoom(argv[2], device))
        return 1;
    sonos::Client transport(device.coordinatorHost, device.coordinatorPort);
    sonos::Client room(device.host, device.port);
    if (command == "volume") {
        if (argc > 3) {
            sonos::Result result = room.SetVolume(atoi(argv[3]));
            if (!result.ok)
                fprintf(stderr, "SetVolume: %s\n", result.error.c_str());
        }
        int volume = -1;
        sonos::Result result = room.GetVolume(volume);
        printf("volume %d %s\n", volume, result.ok ? "" : result.error.c_str());
        return result.ok ? 0 : 1;
    }
    if (command == "stop") {
        sonos::Result result = transport.Stop();
        printf("stop: %s\n", result.ok ? "ok" : result.error.c_str());
        return result.ok ? 0 : 1;
    }
    if (command == "play" && argc > 3) {
        MediaServer server;
        std::string error;
        if (!server.Start(error)) {
            fprintf(stderr, "server: %s\n", error.c_str());
            return 1;
        }
        std::string address = sonos::LocalAddressFor(device.coordinatorHost);
        std::string url = "http://" + address + ":" + std::to_string(server.Port()) + server.Publish(argv[3]);
        std::string mime = MediaServer::MimeTypeFor(argv[3]);
        printf("serving %s as %s (%s)\n", argv[3], url.c_str(), mime.c_str());
        std::string metadata = sonos::TrackMetadata(url, mime, "Amp test", "Amp", "sonostool", "", 0);
        sonos::Result result = transport.SetAVTransportURI(url, metadata);
        printf("SetAVTransportURI: %s\n", result.ok ? "ok" : result.error.c_str());
        if (!result.ok)
            return 1;
        int start = argc > 5 ? atoi(argv[5]) : 0;
        if (start > 0 && getenv("SEEK_FIRST")) {
            result = transport.Seek(start * 1000LL);
            printf("Seek %d s before Play: %s\n", start, result.ok ? "ok" : result.error.c_str());
            start = 0;
        }
        result = transport.Play();
        printf("Play: %s\n", result.ok ? "ok" : result.error.c_str());
        if (start > 0) {
            result = transport.Seek(start * 1000LL);
            printf("Seek %d s: %s\n", start, result.ok ? "ok" : result.error.c_str());
        }
        int follow = argc > 4 ? atoi(argv[4]) : 10;
        for (int i = 0; i < follow; i++) {
            sleep(1);
            sonos::TransportInfo info;
            transport.GetTransportInfo(info);
            transport.GetPositionInfo(info);
            printf("%2d  %-16s %s / %s  requests %d  %s\n", i, info.state.c_str(),
                sonos::FormatTime(info.positionMs).c_str(), sonos::FormatTime(info.durationMs).c_str(),
                server.Requests(), info.uri == url ? "ours" : info.uri.c_str());
            fflush(stdout);
        }
        result = transport.Stop();
        printf("Stop: %s\n", result.ok ? "ok" : result.error.c_str());
        return 0;
    }
    return 2;
}
