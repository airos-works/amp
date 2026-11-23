#include "Sonos.h"
#include "Http.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <set>
#include <sys/socket.h>
#include <unistd.h>

namespace amp {
namespace sonos {

namespace {

const char* kAVTransport = "urn:schemas-upnp-org:service:AVTransport:1";
const char* kRendering = "urn:schemas-upnp-org:service:RenderingControl:1";
const char* kTopology = "urn:schemas-upnp-org:service:ZoneGroupTopology:1";
const char* kAVTransportPath = "/MediaRenderer/AVTransport/Control";
const char* kRenderingPath = "/MediaRenderer/RenderingControl/Control";
const char* kTopologyPath = "/ZoneGroupTopology/Control";

// http://192.168.1.40:1400/xml/device_description.xml -> host and port
bool ParseLocation(const std::string& location, std::string& host, int& port)
{
    size_t start = location.find("://");
    if (start == std::string::npos)
        return false;
    start += 3;
    size_t end = location.find_first_of(":/", start);
    host = location.substr(start, end == std::string::npos ? std::string::npos : end - start);
    port = 1400;
    if (end != std::string::npos && location[end] == ':')
        port = atoi(location.c_str() + end + 1);
    return !host.empty() && port > 0;
}

// The attributes of one start tag: name="value" pairs, values unescaped.
std::map<std::string, std::string> Attributes(const std::string& tag)
{
    std::map<std::string, std::string> attributes;
    size_t i = tag.find_first_of(" \t\r\n");
    while (i != std::string::npos && i < tag.size()) {
        size_t nameStart = tag.find_first_not_of(" \t\r\n/>", i);
        if (nameStart == std::string::npos)
            break;
        size_t equals = tag.find('=', nameStart);
        if (equals == std::string::npos)
            break;
        std::string name = tag.substr(nameStart, equals - nameStart);
        name.erase(name.find_last_not_of(" \t\r\n") + 1);
        size_t quote = tag.find_first_of("\"'", equals);
        if (quote == std::string::npos)
            break;
        size_t close = tag.find(tag[quote], quote + 1);
        if (close == std::string::npos)
            break;
        attributes[name] = XmlUnescape(tag.substr(quote + 1, close - quote - 1));
        i = close + 1;
    }
    return attributes;
}

std::string HeaderValue(const std::string& response, const char* name)
{
    // case-insensitive "\r\nNAME:" search
    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::string key = std::string("\n") + name + ":";
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    size_t at = lower.find(key);
    if (at == std::string::npos)
        return std::string();
    size_t start = response.find_first_not_of(" \t", at + key.size());
    size_t end = response.find_first_of("\r\n", start);
    if (start == std::string::npos)
        return std::string();
    return response.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

const char* UpnpErrorText(int code)
{
    switch (code) {
        case 701: return "the speaker cannot do that now";
        case 711: return "the position is out of range";
        case 714: return "the speaker cannot play this kind of file";
        case 716: return "the speaker cannot fetch the song";
        case 800: return "the speaker is not the coordinator of its group";
        default: return nullptr;
    }
}

} // namespace

std::string XmlEscape(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string XmlUnescape(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        size_t semi = text.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out.push_back('&');
            continue;
        }
        std::string entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') {
            unsigned long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                ? strtoul(entity.c_str() + 2, nullptr, 16) : strtoul(entity.c_str() + 1, nullptr, 10);
            // UTF-8
            if (code < 0x80)
                out.push_back((char)code);
            else if (code < 0x800) {
                out.push_back((char)(0xC0 | (code >> 6)));
                out.push_back((char)(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back((char)(0xE0 | (code >> 12)));
                out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (code & 0x3F)));
            } else {
                out.push_back((char)(0xF0 | (code >> 18)));
                out.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (code & 0x3F)));
            }
        } else {
            out.append(text, i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

std::string TagText(const std::string& xml, const std::string& tag)
{
    // <tag>, <tag attr=...> or <prefix:tag ...>
    size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        size_t nameStart = pos + 1;
        size_t nameEnd = xml.find_first_of(" \t\r\n/>", nameStart);
        if (nameEnd == std::string::npos)
            return std::string();
        std::string name = xml.substr(nameStart, nameEnd - nameStart);
        size_t colon = name.find(':');
        std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
        if (local != tag) {
            pos = nameEnd;
            continue;
        }
        size_t open = xml.find('>', nameEnd);
        if (open == std::string::npos)
            return std::string();
        if (xml[open - 1] == '/')
            return std::string(); // <tag/>
        std::string closing = "</" + name + ">";
        size_t close = xml.find(closing, open + 1);
        if (close == std::string::npos)
            return std::string();
        return XmlUnescape(xml.substr(open + 1, close - open - 1));
    }
    return std::string();
}

std::string FormatTime(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    int64_t seconds = ms / 1000;
    char text[32];
    snprintf(text, sizeof(text), "%d:%02d:%02d", (int)(seconds / 3600), (int)(seconds / 60 % 60),
        (int)(seconds % 60));
    return text;
}

int64_t ParseTime(const std::string& text)
{
    int hours = 0, minutes = 0;
    double seconds = 0;
    if (sscanf(text.c_str(), "%d:%d:%lf", &hours, &minutes, &seconds) != 3)
        return 0;
    return (int64_t)hours * 3600000 + (int64_t)minutes * 60000 + (int64_t)(seconds * 1000);
}

std::string TrackMetadata(const std::string& uri, const std::string& mimeType, const std::string& title,
    const std::string& artist, const std::string& album, const std::string& artUri, int64_t durationMs)
{
    std::string didl =
        "<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
        "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
        "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
        "<item id=\"-1\" parentID=\"-1\" restricted=\"true\">";
    didl += "<dc:title>" + XmlEscape(title) + "</dc:title>";
    if (!artist.empty())
        didl += "<dc:creator>" + XmlEscape(artist) + "</dc:creator>";
    if (!album.empty())
        didl += "<upnp:album>" + XmlEscape(album) + "</upnp:album>";
    if (!artUri.empty())
        didl += "<upnp:albumArtURI>" + XmlEscape(artUri) + "</upnp:albumArtURI>";
    didl += "<upnp:class>object.item.audioItem.musicTrack</upnp:class>";
    didl += "<res protocolInfo=\"http-get:*:" + XmlEscape(mimeType) + ":*\"";
    if (durationMs > 0)
        didl += " duration=\"" + FormatTime(durationMs) + "\"";
    didl += ">" + XmlEscape(uri) + "</res></item></DIDL-Lite>";
    return didl;
}

std::vector<SonosDevice> ParseZoneGroupState(const std::string& state)
{
    std::vector<SonosDevice> devices;
    struct Member {
        std::string uuid, name, host;
        int port = 1400;
    };
    std::string coordinator;
    std::vector<Member> members;
    auto finishGroup = [&]() {
        const Member* lead = nullptr;
        for (const Member& member : members)
            if (member.uuid == coordinator)
                lead = &member;
        if (lead) {
            for (const Member& member : members) {
                SonosDevice device;
                device.uuid = member.uuid;
                device.name = member.name;
                device.host = member.host;
                device.port = member.port;
                device.coordinatorUuid = lead->uuid;
                device.coordinatorHost = lead->host;
                device.coordinatorPort = lead->port;
                devices.push_back(device);
            }
        }
        members.clear();
        coordinator.clear();
    };
    size_t pos = 0;
    while ((pos = state.find('<', pos)) != std::string::npos) {
        size_t end = state.find('>', pos);
        if (end == std::string::npos)
            break;
        std::string tag = state.substr(pos, end - pos + 1);
        pos = end + 1;
        if (tag.compare(0, 11, "<ZoneGroup ") == 0 || tag == "<ZoneGroup>") {
            finishGroup();
            coordinator = Attributes(tag)["Coordinator"];
        } else if (tag == "</ZoneGroup>") {
            finishGroup();
        } else if (tag.compare(0, 17, "<ZoneGroupMember ") == 0) {
            // satellites (<Satellite>) are part of their room: never listed
            std::map<std::string, std::string> attributes = Attributes(tag);
            if (attributes["Invisible"] == "1" || attributes["IsZoneBridge"] == "1")
                continue;
            Member member;
            member.uuid = attributes["UUID"];
            member.name = attributes["ZoneName"];
            if (member.uuid.empty() || member.name.empty()
                || !ParseLocation(attributes["Location"], member.host, member.port))
                continue;
            members.push_back(member);
        }
    }
    finishGroup();
    std::sort(devices.begin(), devices.end(), [](const SonosDevice& a, const SonosDevice& b) {
        return a.name != b.name ? a.name < b.name : a.uuid < b.uuid;
    });
    return devices;
}

std::vector<SonosDevice> Discover(int waitMs, std::string* error)
{
    std::vector<SonosDevice> devices;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        if (error)
            *error = strerror(errno);
        return devices;
    }
    unsigned char ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &group.sin_addr);
    const char* search =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n";
    // UDP gets lost: ask twice, a moment apart
    bool sent = false;
    for (int i = 0; i < 2; i++) {
        if (sendto(sock, search, strlen(search), 0, (sockaddr*)&group, sizeof(group)) > 0)
            sent = true;
        if (i == 0)
            usleep(120000);
    }
    if (!sent) {
        if (error)
            *error = std::string("cannot search the network: ") + strerror(errno);
        close(sock);
        return devices;
    }
    std::vector<std::pair<std::string, int>> players;
    std::set<std::string> seen;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    for (;;) {
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0)
            break;
        pollfd pfd{sock, POLLIN, 0};
        if (poll(&pfd, 1, left) <= 0)
            break;
        char buffer[2048];
        ssize_t size = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (size <= 0)
            continue;
        buffer[size] = 0;
        std::string response(buffer, size);
        if (response.find("ZonePlayer") == std::string::npos && response.find("Sonos") == std::string::npos)
            continue;
        std::string host;
        int port;
        if (!ParseLocation(HeaderValue(response, "LOCATION"), host, port))
            continue;
        if (seen.insert(host).second)
            players.push_back({host, port});
    }
    close(sock);
    if (players.empty())
        return devices;
    // any player knows the whole household
    std::string lastError;
    for (const auto& player : players) {
        Client client(player.first, player.second);
        std::string state;
        Result result = client.GetZoneGroupState(state);
        if (result.ok) {
            devices = ParseZoneGroupState(state);
            if (!devices.empty())
                return devices;
        } else
            lastError = result.error;
    }
    if (error)
        *error = lastError.empty() ? std::string("the speakers did not describe their rooms") : lastError;
    return devices;
}

std::string LocalAddressFor(const std::string& host)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return std::string();
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(1400);
    std::string address;
    if (inet_pton(AF_INET, host.c_str(), &remote.sin_addr) == 1
        && connect(sock, (sockaddr*)&remote, sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t length = sizeof(local);
        char text[INET_ADDRSTRLEN];
        if (getsockname(sock, (sockaddr*)&local, &length) == 0
            && inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
            address = text;
    }
    close(sock);
    return address;
}

// #pragma mark - Client

Result Client::Call(const char* path, const char* service, const char* action, const std::string& arguments,
    int timeoutSeconds)
{
    Result result;
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", fHost.c_str(), fPort, path);
    std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
        "<u:" + std::string(action) + " xmlns:u=\"" + service + "\">" + arguments
        + "</u:" + action + "></s:Body></s:Envelope>";
    std::vector<std::string> headers = {
        "Content-Type: text/xml; charset=\"utf-8\"",
        std::string("SOAPACTION: \"") + service + "#" + action + "\"",
        "Expect:",
    };
    HttpResponse response = Http::Post(url, body, headers, timeoutSeconds, 2);
    result.body = response.body;
    if (response.status == 0) {
        result.unreachable = true;
        result.error = "the speaker did not answer";
        if (!response.error.empty())
            result.error += " (" + response.error + ")";
        return result;
    }
    if (response.ok()) {
        result.ok = true;
        return result;
    }
    result.upnpError = atoi(TagText(response.body, "errorCode").c_str());
    const char* known = UpnpErrorText(result.upnpError);
    char text[96];
    if (known)
        snprintf(text, sizeof(text), "%s", known);
    else if (result.upnpError)
        snprintf(text, sizeof(text), "%s failed (UPnP error %d)", action, result.upnpError);
    else
        snprintf(text, sizeof(text), "%s failed (HTTP %ld)", action, response.status);
    result.error = text;
    return result;
}

Result Client::SetAVTransportURI(const std::string& uri, const std::string& metadata)
{
    return Call(kAVTransportPath, kAVTransport, "SetAVTransportURI",
        "<InstanceID>0</InstanceID><CurrentURI>" + XmlEscape(uri) + "</CurrentURI><CurrentURIMetaData>"
        + XmlEscape(metadata) + "</CurrentURIMetaData>", 8);
}

Result Client::Play()
{
    return Call(kAVTransportPath, kAVTransport, "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>", 8);
}

Result Client::Pause()
{
    return Call(kAVTransportPath, kAVTransport, "Pause", "<InstanceID>0</InstanceID>");
}

Result Client::Stop()
{
    return Call(kAVTransportPath, kAVTransport, "Stop", "<InstanceID>0</InstanceID>");
}

Result Client::Seek(int64_t positionMs)
{
    return Call(kAVTransportPath, kAVTransport, "Seek",
        "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>" + FormatTime(positionMs) + "</Target>", 8);
}

Result Client::GetTransportInfo(TransportInfo& info)
{
    Result result = Call(kAVTransportPath, kAVTransport, "GetTransportInfo", "<InstanceID>0</InstanceID>", 3);
    if (result.ok)
        info.state = TagText(result.body, "CurrentTransportState");
    return result;
}

Result Client::GetPositionInfo(TransportInfo& info)
{
    Result result = Call(kAVTransportPath, kAVTransport, "GetPositionInfo", "<InstanceID>0</InstanceID>", 3);
    if (result.ok) {
        info.positionMs = ParseTime(TagText(result.body, "RelTime"));
        info.durationMs = ParseTime(TagText(result.body, "TrackDuration"));
        info.uri = TagText(result.body, "TrackURI");
    }
    return result;
}

Result Client::GetVolume(int& volume)
{
    Result result = Call(kRenderingPath, kRendering, "GetVolume",
        "<InstanceID>0</InstanceID><Channel>Master</Channel>", 3);
    if (result.ok)
        volume = atoi(TagText(result.body, "CurrentVolume").c_str());
    return result;
}

Result Client::SetVolume(int volume)
{
    volume = std::max(0, std::min(100, volume));
    return Call(kRenderingPath, kRendering, "SetVolume",
        "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>" + std::to_string(volume)
        + "</DesiredVolume>", 3);
}

Result Client::GetZoneGroupState(std::string& state)
{
    Result result = Call(kTopologyPath, kTopology, "GetZoneGroupState", "", 4);
    if (result.ok)
        state = TagText(result.body, "ZoneGroupState");
    return result;
}

} // namespace sonos
} // namespace amp
