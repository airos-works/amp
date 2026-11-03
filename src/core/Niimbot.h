// Niimbot label printer protocol: packets, the raster rows of a label, the B1 print sequence.
// Portable; the Bluetooth transport lives in src/player/LabelPrinter.
//
// A packet is 55 55 <command> <length> <data> <xor of command, length and data> aa aa.
// The printer answers most commands with a packet whose command is the request's plus 0x10
// (0xc1 -> 0xc2 and a few more are irregular, see the constants). Image rows are not answered.
// Reference: NiimBlue (github.com/MultiMote/niimbluelib, MIT), which documents the protocol.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace amp {
namespace niimbot {

using Bytes = std::vector<uint8_t>;

enum Command : uint8_t {
    kConnect = 0xc1,          // sent with a 0x03 byte in front of the packet
    kPrinterInfo = 0x40,      // data: one InfoType; answered with 0x40 + type
    kRfidInfo = 0x1a,         // the label roll's tag
    kHeartbeat = 0xdc,
    kSetDensity = 0x21,
    kSetLabelType = 0x23,
    kPrintStart = 0x01,
    kPageStart = 0x03,
    kSetPageSize = 0x13,
    kBitmapRow = 0x85,
    kBitmapRowIndexed = 0x83, // a row with at most six black dots, sent as their positions
    kEmptyRows = 0x84,
    kPageEnd = 0xe3,
    kPrintStatus = 0xa3,
    kPrintEnd = 0xf3,
};

enum Answer : uint8_t {
    kAnswerConnect = 0xc2,
    kAnswerRfidInfo = 0x1b,
    kAnswerSetDensity = 0x31,
    kAnswerSetLabelType = 0x33,
    kAnswerPrintStart = 0x02,
    kAnswerPageStart = 0x04,
    kAnswerSetPageSize = 0x14,
    kAnswerPageEnd = 0xe4,
    kAnswerPrintStatus = 0xb3,
    kAnswerPrintEnd = 0xf4,
    kAnswerPrintError = 0xdb,
    kAnswerNotSupported = 0x00,
};

enum InfoType : uint8_t {
    kInfoDensity = 1, kInfoLabelType = 3, kInfoModelId = 8, kInfoSoftware = 9, kInfoBattery = 10,
    kInfoSerial = 11, kInfoHardware = 12
};

enum LabelType : uint8_t { kLabelWithGaps = 1, kLabelBlackMarks = 2, kLabelTransparent = 5 };

struct Packet {
    uint8_t command = 0;
    Bytes data;
};

// The bytes to send for one packet.
Bytes Encode(uint8_t command, const Bytes& data);
inline Bytes Encode(const Packet& packet) { return Encode(packet.command, packet.data); }

// Collects bytes from a stream transport and cuts packets out of them. Bytes that do not
// belong to a well-formed packet are skipped.
class Parser {
public:
    void Feed(const uint8_t* data, size_t length);
    bool Next(Packet& packet);
    size_t Buffered() const { return fBuffer.size(); }
private:
    Bytes fBuffer;
};

// A one-bit picture of a label: `rows` lines in feed order, each `cols` dots across the print
// head, packed eight to a byte with the leftmost dot in the high bit. 1 is black.
struct Raster {
    int cols = 0;
    int rows = 0;
    std::vector<Bytes> lines;

    Raster() = default;
    Raster(int cols, int rows);
    bool Get(int x, int y) const { return (lines[y][x >> 3] >> (7 - (x & 7))) & 1; }
    void Set(int x, int y) { lines[y][x >> 3] |= 0x80 >> (x & 7); }
};

// The image packets for a raster: equal rows are sent once with a repeat count, blank rows
// as one empty-rows packet, sparse rows as dot positions. `printheadDots` decides how the dot
// counts in each row packet are given (three thirds of the head, or one total).
std::vector<Packet> RowPackets(const Raster& raster, int printheadDots);

// Readies a photo (grey, 0 black .. 255 white, w*h) for a one-bit print: stretches its tones
// over the whole range (covers are often pale or murky; the 1st and 99th percentile become
// black and white), sharpens a little (an unsharp mask of radius one), and lifts the mid
// tones, since a thermal head spreads every dot.
void PrepareGrey(std::vector<uint8_t>& grey, int width, int height);

// Grey (0 black .. 255 white, w*h) to dots with Atkinson error diffusion, which keeps
// highlights open on a thermal head that spreads every dot a little. `out` gets 1 for black.
void Dither(const std::vector<uint8_t>& grey, int width, int height, std::vector<uint8_t>& out);

struct Model {
    uint16_t id;
    const char* name;
    int printheadDots;
    int dpi;
};
// The models this code knows how to print to (the B1 family's print sequence), or nullptr.
const Model* ModelById(uint16_t id);

// Packets of the B1 print sequence.
Packet PrintStart(int pages);                       // 7-byte form
Packet PageSize(int rows, int cols, int copies);    // 6-byte form
inline Packet Simple(uint8_t command, uint8_t value = 1) { return Packet{command, Bytes{value}}; }

struct PrintStatus {
    int page = 0;           // pages finished
    int printPercent = 0;
    int feedPercent = 0;
};
bool ParsePrintStatus(const Packet& packet, PrintStatus& status);

struct RfidInfo {
    bool present = false;
    std::string barcode;
    std::string serial;
    int total = 0;          // labels on the roll
    int used = 0;
    int type = 0;
};
bool ParseRfidInfo(const Packet& packet, RfidInfo& info);

} // namespace niimbot
} // namespace amp
