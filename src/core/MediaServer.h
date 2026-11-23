// A small HTTP server that lets speakers on the network fetch the songs Amp sends them.
// Only files that were published are served, each under a path with a token made at start,
// so the rest of the disk stays out of reach. GET and HEAD, with byte ranges (speakers seek
// with them). Portable C++ over POSIX sockets.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace amp {

class MediaServer {
public:
    MediaServer();
    ~MediaServer();

    // Listens on any free port of every interface. False with `error` set when it cannot.
    bool Start(std::string& error);
    void Stop();
    bool Running() const { return fListen >= 0; }
    int Port() const { return fPort; }

    // Makes `path` available and returns the absolute path part of its URL
    // ("/amp/<token>/<n>.flac"); the URL is http://<address>:<Port()><that>.
    std::string Publish(const std::string& path);

    // The MIME type a speaker is told for a file, from its name (or its first bytes).
    static std::string MimeTypeFor(const std::string& path);

    // Requests served so far (the tests watch it).
    int Requests() const { return fRequests; }

private:
    void AcceptLoop();
    void Serve(int client);
    std::string Lookup(const std::string& urlPath);

    int fListen = -1;
    int fPort = 0;
    std::string fToken;
    std::thread fAcceptThread;
    std::atomic<bool> fRunning{false};
    std::mutex fMutex;
    std::condition_variable fIdle;
    int fActive = 0;                       // connections being served
    std::map<std::string, std::string> fFiles;   // url path -> file
    std::map<std::string, std::string> fByFile;  // file -> url path
    int fNext = 1;
    std::atomic<int> fRequests{0};
};

} // namespace amp
