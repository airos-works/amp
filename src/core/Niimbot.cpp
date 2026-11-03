#include "Niimbot.h"
#include <algorithm>
#include <cmath>

namespace amp {
namespace niimbot {

Bytes Encode(uint8_t command, const Bytes& data)
{
    Bytes out;
    out.reserve(data.size() + 8);
    if (command == kConnect)
        out.push_back(0x03);
    out.push_back(0x55);
    out.push_back(0x55);
    out.push_back(command);
    out.push_back((uint8_t)data.size());
    uint8_t check = command ^ (uint8_t)data.size();
    for (uint8_t byte : data) {
        out.push_back(byte);
        check ^= byte;
    }
    out.push_back(check);
    out.push_back(0xaa);
    out.push_back(0xaa);
    return out;
}

void Parser::Feed(const uint8_t* data, size_t length)
{
    fBuffer.insert(fBuffer.end(), data, data + length);
}

bool Parser::Next(Packet& packet)
{
    for (;;) {
        size_t start = 0;
        while (start + 1 < fBuffer.size() && !(fBuffer[start] == 0x55 && fBuffer[start + 1] == 0x55))
            start++;
        if (start > 0)
            fBuffer.erase(fBuffer.begin(), fBuffer.begin() + start);
        if (fBuffer.size() < 7)
            return false;
        size_t length = fBuffer[3];
        if (fBuffer.size() < length + 7)
            return false;
        uint8_t check = fBuffer[2] ^ fBuffer[3];
        for (size_t i = 0; i < length; i++)
            check ^= fBuffer[4 + i];
        if (check != fBuffer[4 + length] || fBuffer[5 + length] != 0xaa || fBuffer[6 + length] != 0xaa) {
            fBuffer.erase(fBuffer.begin()); // not a packet after all: look further on
            continue;
        }
        packet.command = fBuffer[2];
        packet.data.assign(fBuffer.begin() + 4, fBuffer.begin() + 4 + length);
        fBuffer.erase(fBuffer.begin(), fBuffer.begin() + 7 + length);
        return true;
    }
}

Raster::Raster(int c, int r)
    : cols((c + 7) / 8 * 8), rows(r), lines(r, Bytes((c + 7) / 8, 0))
{
}

static void PutU16(Bytes& out, int value)
{
    out.push_back((uint8_t)(value >> 8));
    out.push_back((uint8_t)value);
}

std::vector<Packet> RowPackets(const Raster& raster, int printheadDots)
{
    std::vector<Packet> packets;
    const int third = printheadDots / 8 / 3; // bytes in each third of the head
    int row = 0;
    while (row < raster.rows) {
        const Bytes& line = raster.lines[row];
        int repeat = 1;
        while (row + repeat < raster.rows && repeat < 255 && raster.lines[row + repeat] == line)
            repeat++;
        int total = 0;
        int parts[3] = {0, 0, 0};
        bool split = third > 0 && (int)line.size() <= third * 3;
        for (size_t i = 0; i < line.size(); i++) {
            int dots = __builtin_popcount(line[i]);
            total += dots;
            if (split)
                parts[std::min<size_t>(i / third, 2)] += dots;
        }
        Packet packet;
        PutU16(packet.data, row);
        if (total == 0) {
            packet.command = kEmptyRows;
            packet.data.push_back((uint8_t)repeat);
        } else {
            if (split) {
                for (int part : parts)
                    packet.data.push_back((uint8_t)std::min(part, 255));
            } else {
                packet.data.push_back(0);
                packet.data.push_back((uint8_t)total);
                packet.data.push_back((uint8_t)(total >> 8));
            }
            packet.data.push_back((uint8_t)repeat);
            if (total <= 6) {
                packet.command = kBitmapRowIndexed;
                for (size_t i = 0; i < line.size(); i++)
                    for (int bit = 0; bit < 8; bit++)
                        if (line[i] & (0x80 >> bit))
                            PutU16(packet.data, (int)i * 8 + bit);
            } else {
                packet.command = kBitmapRow;
                packet.data.insert(packet.data.end(), line.begin(), line.end());
            }
        }
        packets.push_back(std::move(packet));
        row += repeat;
    }
    return packets;
}

void PrepareGrey(std::vector<uint8_t>& grey, int width, int height)
{
    const size_t count = (size_t)width * height;
    if (count == 0 || grey.size() < count)
        return;
    // 1. levels
    size_t histogram[256] = {};
    for (size_t i = 0; i < count; i++)
        histogram[grey[i]]++;
    size_t clip = count / 100, sum = 0;
    int low = 0, high = 255;
    for (int v = 0; v < 256; v++) {
        sum += histogram[v];
        if (sum > clip) {
            low = v;
            break;
        }
    }
    sum = 0;
    for (int v = 255; v >= 0; v--) {
        sum += histogram[v];
        if (sum > clip) {
            high = v;
            break;
        }
    }
    // At most three times the contrast: a flat picture (a plain white cover) keeps its tone
    // instead of turning into noise or black.
    if (high - low < 85) {
        int middle = (low + high) / 2;
        low = std::min(255 - 85, std::max(0, middle - 42));
        high = low + 85;
    }
    std::vector<float> value(count);
    float range = (float)(high - low);
    for (size_t i = 0; i < count; i++)
        value[i] = std::min(255.0f, std::max(0.0f, (grey[i] - low) * 255.0f / range));

    // 2. unsharp mask: the picture plus 0.8 of its difference from a blurred copy
    static const float kTaps[5] = {1 / 16.0f, 4 / 16.0f, 6 / 16.0f, 4 / 16.0f, 1 / 16.0f};
    std::vector<float> across(count), blurred(count);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            float total = 0;
            for (int k = -2; k <= 2; k++)
                total += kTaps[k + 2] * value[(size_t)y * width + std::min(width - 1, std::max(0, x + k))];
            across[(size_t)y * width + x] = total;
        }
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            float total = 0;
            for (int k = -2; k <= 2; k++)
                total += kTaps[k + 2] * across[(size_t)std::min(height - 1, std::max(0, y + k)) * width + x];
            blurred[(size_t)y * width + x] = total;
        }

    // 3. mid tones up (gamma 0.85)
    uint8_t curve[256];
    for (int v = 0; v < 256; v++)
        curve[v] = (uint8_t)std::lround(255.0 * std::pow(v / 255.0, 0.85));
    for (size_t i = 0; i < count; i++) {
        float sharp = value[i] + 0.8f * (value[i] - blurred[i]);
        int v = (int)std::lround(std::min(255.0f, std::max(0.0f, sharp)));
        grey[i] = curve[v];
    }
}

void Dither(const std::vector<uint8_t>& grey, int width, int height, std::vector<uint8_t>& out)
{
    out.assign((size_t)width * height, 0);
    // Three rows of error are enough for Atkinson's kernel.
    std::vector<int> error((size_t)(width + 4) * 3, 0);
    auto at = [&](int x, int y) -> int& { return error[(size_t)(y % 3) * (width + 4) + x + 2]; };
    for (int y = 0; y < height; y++) {
        for (int x = -2; x < width + 2; x++)
            at(x, y + 2) = 0;
        for (int x = 0; x < width; x++) {
            int value = grey[(size_t)y * width + x] + at(x, y);
            int target = value < 128 ? 0 : 255;
            out[(size_t)y * width + x] = target == 0;
            int spread = (value - target) / 8; // Atkinson passes on 6/8 of the error
            at(x + 1, y) += spread;
            at(x + 2, y) += spread;
            at(x - 1, y + 1) += spread;
            at(x, y + 1) += spread;
            at(x + 1, y + 1) += spread;
            at(x, y + 2) += spread;
        }
    }
}

const Model* ModelById(uint16_t id)
{
    static const Model kModels[] = {
        {4096, "B1", 384, 203},
    };
    for (const Model& model : kModels)
        if (model.id == id)
            return &model;
    return nullptr;
}

Packet PrintStart(int pages)
{
    Packet packet{kPrintStart, {}};
    PutU16(packet.data, pages);
    packet.data.insert(packet.data.end(), {0, 0, 0, 0, 0}); // ..., page colour 0: one colour
    return packet;
}

Packet PageSize(int rows, int cols, int copies)
{
    Packet packet{kSetPageSize, {}};
    PutU16(packet.data, rows);
    PutU16(packet.data, cols);
    PutU16(packet.data, copies);
    return packet;
}

bool ParsePrintStatus(const Packet& packet, PrintStatus& status)
{
    if (packet.command != kAnswerPrintStatus || packet.data.size() < 4)
        return false;
    status.page = packet.data[0] << 8 | packet.data[1];
    status.printPercent = packet.data[2];
    status.feedPercent = packet.data[3];
    return true;
}

bool ParseRfidInfo(const Packet& packet, RfidInfo& info)
{
    info = RfidInfo();
    if (packet.command != kAnswerRfidInfo)
        return false;
    const Bytes& d = packet.data;
    if (d.size() <= 1)
        return true; // no tag
    size_t pos = 8; // tag id
    auto string = [&](std::string& out) {
        if (pos >= d.size())
            return false;
        size_t length = d[pos++];
        if (pos + length > d.size())
            return false;
        out.assign(d.begin() + pos, d.begin() + pos + length);
        pos += length;
        return true;
    };
    if (!string(info.barcode) || !string(info.serial) || pos + 5 > d.size())
        return false;
    info.total = d[pos] << 8 | d[pos + 1];
    info.used = d[pos + 2] << 8 | d[pos + 3];
    info.type = d[pos + 4];
    info.present = true;
    return true;
}

} // namespace niimbot
} // namespace amp
