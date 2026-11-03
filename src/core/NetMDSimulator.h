// A NetMD recorder in software: answers the commands Amp uses and records uploaded tracks into
// an in-memory disc. Used by the unit tests and, with AMP_NETMD_SIMULATE set, by the app itself
// so the MiniDisc interface can be tried without hardware.
#pragma once
#include "NetMD.h"
#include <mutex>

namespace amp {
namespace netmd {

class Simulator : public Transport {
public:
    struct Track {
        std::string title;
        int64_t frames = 0;
        int encoding = kEncodingSP;
    };

    // `speed`: how many times faster than real time uploads are accepted (0: no delay).
    explicit Simulator(double speed = 0);

    ssize_t VendorIn(uint8_t request, void* data, size_t length) override;
    ssize_t VendorOut(uint8_t request, const void* data, size_t length) override;
    ssize_t BulkOut(const void* data, size_t length) override;
    void Sleep(int milliseconds) override;

    // Disc contents, for tests and for setting up a scenario.
    std::string discTitle;
    std::vector<Track> tracks;
    int64_t totalFrames = (int64_t)80 * 60 * kFramesPerSecond;
    bool discPresent = true;
    bool writeProtected = false;
    int commands = 0;
    // A recorder that stops taking data, like a portable whose battery runs low: upload number
    // `stallUpload` (from 0) blocks a third of the way in for `stallMs`, then fails the way a
    // transfer cancelled by unplugging fails. -1: never.
    int stallUpload = -1;
    int stallMs = 0;
    // A recorder whose battery is too low to record: from upload number `refuseUpload` on, the
    // command that starts an upload is rejected. -1: never.
    int refuseUpload = -1;

private:
    Bytes Handle(const Bytes& command);
    void Reply(uint8_t status, const Bytes& body);
    int64_t UsedSPFrames() const;

    std::mutex fLock;
    Bytes fReply;
    bool fReplyReady = false;
    // an upload in progress
    bool fReceiving = false;
    uint64_t fExpected = 0, fReceived = 0;
    Bytes fUploadCommand;
    std::string fPendingTitle;
    double fSpeed;
    int fUploads = 0;
    int fUploadRequests = 0;
    bool fStalled = false;
};

} // namespace netmd
} // namespace amp
