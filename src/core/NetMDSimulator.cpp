#include "NetMDSimulator.h"
#include <chrono>
#include <cstring>
#include <thread>

namespace amp {
namespace netmd {

namespace {

bool StartsWith(const Bytes& data, const char* hex)
{
    Bytes prefix = Query(hex).Data();
    return data.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin());
}

uint16_t Word(const Bytes& data, size_t at)
{
    return at + 1 < data.size() ? (uint16_t)(data[at] << 8 | data[at + 1]) : 0;
}

uint32_t Dword(const Bytes& data, size_t at)
{
    return (uint32_t)Word(data, at) << 16 | Word(data, at + 2);
}

uint8_t ToBcd(int value)
{
    return (uint8_t)((value / 10) << 4 | (value % 10));
}

void AppendTime(Query& query, int64_t frames)
{
    int64_t seconds = frames / kFramesPerSecond;
    query.U8(ToBcd((int)(seconds / 3600 % 100))).U8(ToBcd((int)(seconds / 60 % 60))).U8(ToBcd((int)(seconds % 60)))
        .U8(ToBcd((int)(frames % kFramesPerSecond % 100)));
}

Bytes Slice(const Bytes& data, size_t from, size_t to)
{
    to = std::min(to, data.size());
    return from < to ? Bytes(data.begin() + from, data.begin() + to) : Bytes();
}

} // namespace

Simulator::Simulator(double speed)
    : fSpeed(speed)
{
}

void Simulator::Sleep(int milliseconds)
{
    if (fSpeed > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int64_t Simulator::UsedSPFrames() const
{
    int64_t used = 0;
    for (const Track& track : tracks)
        used += track.encoding == kEncodingLP4 ? track.frames / 4 : track.encoding == kEncodingLP2 ? track.frames / 2 : track.frames;
    return used;
}

void Simulator::Reply(uint8_t status, const Bytes& body)
{
    fReply.clear();
    fReply.push_back(status);
    fReply.insert(fReply.end(), body.begin(), body.end());
    fReplyReady = true;
}

ssize_t Simulator::VendorIn(uint8_t request, void* data, size_t length)
{
    std::lock_guard<std::mutex> guard(fLock);
    uint8_t* out = static_cast<uint8_t*>(data);
    if (request == 0x01) {
        memset(out, 0, length);
        if (length >= 3 && fReplyReady)
            out[2] = (uint8_t)fReply.size();
        return (ssize_t)length;
    }
    if (request == 0x81 && fReplyReady) {
        size_t count = std::min(length, fReply.size());
        memcpy(out, fReply.data(), count);
        fReplyReady = false;
        return (ssize_t)count;
    }
    return -1;
}

ssize_t Simulator::VendorOut(uint8_t request, const void* data, size_t length)
{
    std::lock_guard<std::mutex> guard(fLock);
    if (request != 0x80 || length < 1)
        return -1;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    commands++;
    Handle(Bytes(bytes + 1, bytes + length));
    return (ssize_t)length;
}

ssize_t Simulator::BulkOut(const void* data, size_t length)
{
    if (fSpeed > 0)
        std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(length * 1e6 / (kPcmBytesPerSecond * fSpeed))));
    bool stall;
    {
        std::lock_guard<std::mutex> guard(fLock);
        stall = !fStalled && fReceiving && fUploads - 1 == stallUpload && fReceived * 3 >= fExpected;
        fStalled = fStalled || stall;
    }
    if (stall) {
        std::this_thread::sleep_for(std::chrono::milliseconds(stallMs));
        std::lock_guard<std::mutex> guard(fLock);
        fReceiving = false;
        return -1;
    }
    std::lock_guard<std::mutex> guard(fLock);
    if (!fReceiving)
        return -1;
    fReceived += length;
    if (fReceived >= fExpected) {
        fReceiving = false;
        Track track;
        track.frames = (int64_t)((fExpected - 24) * kFramesPerSecond / kPcmBytesPerSecond);
        tracks.push_back(track);
        Query reply("1800 080046 f0030103 28 00 000100 1001");
        reply.U16((unsigned)(tracks.size() - 1)).Hex("00 0000 00000000 00000000");
        reply.Append(Bytes(32, 0x5a)); // encrypted UUID and content ID
        Reply(0x09, reply.Data());
    }
    return (ssize_t)length;
}

Bytes Simulator::Handle(const Bytes& c)
{
    const uint8_t accepted = 0x09;
    if (StartsWith(c, "1808")) { // descriptor open / close
        Reply(accepted, c);
    } else if (StartsWith(c, "ff 010c") || StartsWith(c, "ff 0100")) {
        Reply(accepted, c);
    } else if (StartsWith(c, "1809 8001 0230 8800 0030 8804 00 ff00")) {
        Query reply("1809 8001 0230 8800 0030 8804 00 1000 00090000");
        Bytes status = {0, 0, 0, 0, (uint8_t)(discPresent ? 0x40 : 0x80), 0};
        reply.U16((unsigned)status.size()).Append(status);
        Reply(accepted, reply.Data());
    } else if (StartsWith(c, "1809 8001 0330 8802 0030 8805 0030 8806 00 ff00")) {
        Query reply("1809 8001 0330 8802 0030 8805 0030 8806 00 1000 00000000");
        Bytes state = {0x88, 0x06, 0x00, 0x02, (uint8_t)(discPresent ? 0xc5 : 0xff), (uint8_t)(discPresent ? 0xff : 0x10)};
        reply.U16((unsigned)state.size()).Append(state);
        Reply(accepted, reply.Data());
    } else if (StartsWith(c, "1806 01101000 ff00 0001000b")) {
        Reply(accepted, Query("1806 01101000 1000 0001000b").U8(writeProtected ? 0x50 : 0x10).Data());
    } else if (StartsWith(c, "1806 02101001 3000 1000 ff00")) {
        Reply(accepted, Query("1806 02101001 3000 1000 1000 00000000 0006 0010000200").U8((unsigned)tracks.size()).Data());
    } else if (StartsWith(c, "1806 02201801 00")) { // disc title, in one chunk
        Query reply("1806 02201801 0000 3000 0a00 1000");
        reply.U16((unsigned)discTitle.size() + 6).Hex("0000 0000000a").U16((unsigned)discTitle.size());
        reply.Append(Bytes(discTitle.begin(), discTitle.end()));
        Reply(accepted, reply.Data());
    } else if (StartsWith(c, "1806 022018 02")) { // track title
        unsigned index = Word(c, 6);
        if (index >= tracks.size()) {
            Reply(0x0a, c);
            return Bytes();
        }
        const std::string& title = tracks[index].title;
        Query reply("1806 022018 02");
        reply.U16(index).Hex("3000 0a00 1000 00000000 0000000a").U16((unsigned)title.size());
        reply.Append(Bytes(title.begin(), title.end()));
        Reply(accepted, reply.Data());
    } else if (StartsWith(c, "1806 02101000 3080 0300 ff00")) {
        int64_t used = UsedSPFrames();
        Query reply("1806 02101000 3080 0300 1000 001d0000 001b 8003 0017 8000");
        int64_t played = 0;
        for (const Track& track : tracks)
            played += track.frames;
        for (int64_t frames : {played, totalFrames, std::max<int64_t>(0, totalFrames - used)}) {
            reply.Hex("0005");
            int64_t seconds = frames / kFramesPerSecond;
            reply.U16((unsigned)(ToBcd((int)(seconds / 3600 % 100))));
            reply.U8(ToBcd((int)(seconds / 60 % 60))).U8(ToBcd((int)(seconds % 60))).U8(ToBcd((int)(frames % kFramesPerSecond % 100)));
        }
        Reply(accepted, reply.Data());
    } else if (StartsWith(c, "1806 02201001")) { // track length or encoding
        unsigned index = Word(c, 6);
        if (index >= tracks.size()) {
            Reply(0x0a, c);
            return Bytes();
        }
        Query reply("1806 02201001");
        reply.Append(Slice(c, 6, 12)).Hex("1000 00000000");
        Query info;
        if (Word(c, 8) == 0x3000) {
            info.Hex("0001 0006 0000");
            AppendTime(info, tracks[index].frames);
        } else
            info.Hex("8007 0004 0110").U8((unsigned)tracks[index].encoding).U8(0);
        reply.U16((unsigned)info.Data().size()).Append(info.Data());
        Reply(accepted, reply.Data());
    } else if (StartsWith(c, "1840 ff 0000")) {
        tracks.clear();
        discTitle.clear();
        Reply(accepted, Query("1840 00 0000").Data());
    } else if (StartsWith(c, "1807 02201801 00")) {
        unsigned length = Word(c, 14);
        Bytes text = Slice(c, 20, 20 + length);
        discTitle.assign(text.begin(), text.end());
        Reply(accepted, Slice(c, 0, 20));
    } else if (StartsWith(c, "1807 022018 02")) {
        unsigned index = Word(c, 6);
        unsigned length = Word(c, 14);
        if (index >= tracks.size()) {
            Reply(0x0a, c);
            return Bytes();
        }
        Bytes text = Slice(c, 20, 20 + length);
        tracks[index].title.assign(text.begin(), text.end());
        Reply(accepted, Slice(c, 0, 20));
    } else if (StartsWith(c, "1800 080046 f0030103")) {
        uint8_t operation = c.size() > 9 ? c[9] : 0;
        Bytes head = Slice(c, 0, 10);
        Query reply;
        reply.Append(head);
        switch (operation) {
            case 0x80: case 0x81:
                reply.U8(0x00);
                break;
            case 0x11:
                reply.U8(0x00).Hex("0102030405060708");
                break;
            case 0x12:
                reply.U8(0x01).Hex("0000 00000000");
                break;
            case 0x20:
                reply.U8(0x00).Hex("000000 1122334455667788");
                break;
            case 0x21: case 0x22:
                reply.U8(0x00).Append(Slice(c, 11, operation == 0x21 ? 14 : 13));
                break;
            case 0x2b:
                reply.U8(0x00).Append(Slice(c, 11, 13));
                break;
            case 0x28: {
                bool refuse = refuseUpload >= 0 && fUploadRequests >= refuseUpload;
                fUploadRequests++;
                if (writeProtected || !discPresent || refuse) {
                    Reply(0x0a, c);
                    return Bytes();
                }
                fExpected = Dword(c, 25);
                fReceived = 0;
                fReceiving = true;
                fUploads++;
                reply.U8(0x00).Append(Slice(c, 11, 29));
                Reply(0x0f, reply.Data()); // interim: the final reply follows the data
                return Bytes();
            }
            case 0x48:
                reply.U8(0x00).Append(Slice(c, 11, 16));
                break;
            default:
                Reply(0x08, c);
                return Bytes();
        }
        Reply(accepted, reply.Data());
    } else
        Reply(0x08, c);
    return Bytes();
}

} // namespace netmd
} // namespace amp
