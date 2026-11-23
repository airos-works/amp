#include "MediaServer.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace amp {

namespace {

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    return text;
}

std::string Extension(const std::string& path)
{
    size_t slash = path.rfind('/');
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return std::string();
    return Lower(path.substr(dot + 1));
}

bool SendAll(int sock, const char* data, size_t size)
{
    while (size > 0) {
        ssize_t sent = send(sock, data, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return false;
        data += sent;
        size -= (size_t)sent;
    }
    return true;
}

void SetTimeouts(int sock, int seconds)
{
    timeval timeout{seconds, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void SendStatus(int sock, const char* status)
{
    std::string response = std::string("HTTP/1.1 ") + status
        + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    SendAll(sock, response.data(), response.size());
}

} // namespace

MediaServer::MediaServer()
{
    std::random_device random;
    char token[17];
    snprintf(token, sizeof(token), "%08x%08x", random(), random());
    fToken = token;
}

MediaServer::~MediaServer()
{
    Stop();
}

bool MediaServer::Start(std::string& error)
{
    if (fListen >= 0)
        return true;
    // a speaker that hangs up mid-song must not end the application
    signal(SIGPIPE, SIG_IGN);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error = strerror(errno);
        return false;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = 0;
    socklen_t length = sizeof(address);
    if (bind(sock, (sockaddr*)&address, sizeof(address)) != 0 || listen(sock, 16) != 0
        || getsockname(sock, (sockaddr*)&address, &length) != 0) {
        error = strerror(errno);
        close(sock);
        return false;
    }
    fPort = ntohs(address.sin_port);
    fListen = sock;
    fRunning = true;
    fAcceptThread = std::thread([this] { AcceptLoop(); });
    return true;
}

void MediaServer::Stop()
{
    if (fListen < 0)
        return;
    fRunning = false;
    if (fAcceptThread.joinable())
        fAcceptThread.join();
    close(fListen);
    fListen = -1;
    // connections notice within a second (their sends time out or they check fRunning)
    std::unique_lock<std::mutex> lock(fMutex);
    fIdle.wait_for(lock, std::chrono::seconds(3), [this] { return fActive == 0; });
}

std::string MediaServer::Publish(const std::string& path)
{
    std::lock_guard<std::mutex> lock(fMutex);
    auto known = fByFile.find(path);
    if (known != fByFile.end())
        return known->second;
    std::string extension = Extension(path);
    std::string urlPath = "/amp/" + fToken + "/" + std::to_string(fNext++);
    if (!extension.empty())
        urlPath += "." + extension;
    fFiles[urlPath] = path;
    fByFile[path] = urlPath;
    return urlPath;
}

std::string MediaServer::Lookup(const std::string& urlPath)
{
    std::lock_guard<std::mutex> lock(fMutex);
    auto found = fFiles.find(urlPath);
    return found == fFiles.end() ? std::string() : found->second;
}

std::string MediaServer::MimeTypeFor(const std::string& path)
{
    std::string extension = Extension(path);
    if (extension == "mp3") return "audio/mpeg";
    if (extension == "flac") return "audio/flac";
    if (extension == "m4a" || extension == "mp4" || extension == "m4b") return "audio/mp4";
    if (extension == "aac") return "audio/aac";
    if (extension == "ogg" || extension == "oga" || extension == "opus") return "audio/ogg";
    if (extension == "wav") return "audio/wav";
    if (extension == "aif" || extension == "aiff" || extension == "aifc") return "audio/aiff";
    if (extension == "wma") return "audio/x-ms-wma";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "png") return "image/png";
    // artwork in the cache has no extension: look at it
    unsigned char head[8] = {0};
    if (FILE* file = fopen(path.c_str(), "rb")) {
        size_t read = fread(head, 1, sizeof(head), file);
        fclose(file);
        if (read >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)
            return "image/jpeg";
        if (read >= 4 && head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G')
            return "image/png";
        if (read >= 4 && memcmp(head, "fLaC", 4) == 0)
            return "audio/flac";
        if (read >= 3 && (memcmp(head, "ID3", 3) == 0 || (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)))
            return "audio/mpeg";
    }
    return "application/octet-stream";
}

void MediaServer::AcceptLoop()
{
    while (fRunning) {
        pollfd pfd{fListen, POLLIN, 0};
        int ready = poll(&pfd, 1, 250);
        if (ready <= 0 || !fRunning)
            continue;
        int client = accept(fListen, nullptr, nullptr);
        if (client < 0)
            continue;
        {
            std::lock_guard<std::mutex> lock(fMutex);
            fActive++;
        }
        std::thread([this, client] {
            Serve(client);
            close(client);
            std::lock_guard<std::mutex> lock(fMutex);
            if (--fActive == 0)
                fIdle.notify_all();
        }).detach();
    }
}

void MediaServer::Serve(int client)
{
    SetTimeouts(client, 1);
    // the request head
    std::string request;
    char buffer[2048];
    int waited = 0;
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t size = recv(client, buffer, sizeof(buffer), 0);
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && fRunning && ++waited < 15)
            continue;
        if (size <= 0 || request.size() > 16384)
            return;
        request.append(buffer, size);
    }
    fRequests++;
    size_t lineEnd = request.find("\r\n");
    std::string line = request.substr(0, lineEnd);
    size_t space = line.find(' ');
    size_t space2 = line.find(' ', space + 1);
    if (space == std::string::npos || space2 == std::string::npos) {
        SendStatus(client, "400 Bad Request");
        return;
    }
    std::string method = line.substr(0, space);
    std::string urlPath = line.substr(space + 1, space2 - space - 1);
    size_t query = urlPath.find('?');
    if (query != std::string::npos)
        urlPath.erase(query);
    bool head = method == "HEAD";
    if (method != "GET" && !head) {
        SendStatus(client, "405 Method Not Allowed");
        return;
    }
    std::string path = Lookup(urlPath);
    FILE* file = path.empty() ? nullptr : fopen(path.c_str(), "rb");
    if (!file) {
        SendStatus(client, "404 Not Found");
        return;
    }
    struct stat info;
    if (fstat(fileno(file), &info) != 0) {
        fclose(file);
        SendStatus(client, "500 Internal Server Error");
        return;
    }
    int64_t size = info.st_size;
    int64_t first = 0, last = size - 1;
    bool partial = false;
    // Range: bytes=a-b, bytes=a- or bytes=-n
    std::string lower = Lower(request);
    size_t range = lower.find("\r\nrange:");
    if (range != std::string::npos) {
        size_t bytes = lower.find("bytes=", range);
        size_t end = lower.find("\r\n", range + 2);
        if (bytes != std::string::npos && bytes < end) {
            std::string spec = lower.substr(bytes + 6, end - bytes - 6);
            size_t dash = spec.find('-');
            if (dash != std::string::npos) {
                std::string a = spec.substr(0, dash), b = spec.substr(dash + 1);
                b.erase(std::min(b.find(','), b.size()));
                if (a.empty() && !b.empty()) {
                    first = std::max<int64_t>(0, size - atoll(b.c_str()));
                } else if (!a.empty()) {
                    first = atoll(a.c_str());
                    if (!b.empty())
                        last = std::min<int64_t>(size - 1, atoll(b.c_str()));
                }
                partial = true;
            }
        }
    }
    if (partial && (first >= size || first > last)) {
        fclose(file);
        std::string response = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */"
            + std::to_string(size) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        SendAll(client, response.data(), response.size());
        return;
    }
    int64_t length = size > 0 ? last - first + 1 : 0;
    std::string response = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: " + MimeTypeFor(path) + "\r\n";
    response += "Content-Length: " + std::to_string(length) + "\r\n";
    if (partial)
        response += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/"
            + std::to_string(size) + "\r\n";
    response += "Accept-Ranges: bytes\r\nConnection: close\r\nServer: Amp\r\n\r\n";
    if (!SendAll(client, response.data(), response.size()) || head || length == 0) {
        fclose(file);
        return;
    }
    SetTimeouts(client, 20);
    if (fseeko(file, (off_t)first, SEEK_SET) != 0) {
        fclose(file);
        return;
    }
    std::string chunk(64 * 1024, '\0');
    int64_t left = length;
    while (left > 0 && fRunning) {
        size_t want = (size_t)std::min<int64_t>(left, (int64_t)chunk.size());
        size_t read = fread(&chunk[0], 1, want, file);
        if (read == 0)
            break;
        if (!SendAll(client, chunk.data(), read))
            break;
        left -= (int64_t)read;
    }
    fclose(file);
}

} // namespace amp
