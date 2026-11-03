#include "LabelPrinter.h"
#include "Messages.h"

#include <OS.h>
#include <bluetooth/DeviceClass.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/bdaddrUtils.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace amp {

using namespace niimbot;

namespace {

const int kSerialChannel = 1; // Niimbot printers serve their protocol on RFCOMM channel 1

std::string ErrorText(const Packet& packet)
{
    int code = packet.data.empty() ? 0 : packet.data[0];
    switch (code) {
        case 0x01: return "The printer's cover is open.";
        case 0x02: return "The printer is out of labels.";
        case 0x03: return "The printer's battery is low.";
        case 0x05: return "Printing was cancelled on the printer.";
        case 0x07: return "The printer is too hot. Let it cool down and try again.";
        case 0x09: return "The printer is busy.";
        case 0x10: return "The printer does not take this kind of label.";
        default: break;
    }
    return "The printer reported error " + std::to_string(code) + ".";
}

bool ParseAddress(const std::string& text, bdaddr_t& address)
{
    unsigned int b[6];
    if (sscanf(text.c_str(), "%x:%x:%x:%x:%x:%x", &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        address.b[i] = (uint8)b[i];
    return true;
}

std::string AddressText(const bdaddr_t& address)
{
    char text[18];
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", address.b[5], address.b[4], address.b[3],
        address.b[2], address.b[1], address.b[0]);
    return text;
}

} // namespace

std::string LabelPrinterInfo::DisplayName() const
{
    std::string name = model.empty() ? std::string("Niimbot") : model;
    if (!serial.empty())
        return name + " " + serial;
    return name + " (" + address + ")";
}

bool LabelPrinter::Open(const std::string& address, std::string& error)
{
    bdaddr_t bdaddr;
    if (!ParseAddress(address, bdaddr)) {
        error = "\"" + address + "\" is not a Bluetooth address.";
        return false;
    }
    fParser = Parser();
    if (!fLink.Open(bdaddr, kSerialChannel, error)) {
        if (error.empty())
            error = "no connection to the printer";
        error[0] = (char)toupper((unsigned char)error[0]);
        if (error.back() != '.' && error.back() != '?')
            error += ".";
        return false;
    }
    Packet reply;
    if (!Request(Simple(kConnect), kAnswerConnect, reply, 2000000, error)) {
        fLink.Close();
        return false;
    }
    return true;
}

bool LabelPrinter::Send(const Packet& packet)
{
    Bytes bytes = Encode(packet);
    return fLink.Write(bytes.data(), bytes.size());
}

bool LabelPrinter::Request(const Packet& packet, uint8_t answer, Packet& reply, bigtime_t timeout,
    std::string& error)
{
    if (!Send(packet)) {
        error = "The connection to the printer was lost.";
        return false;
    }
    bigtime_t end = system_time() + timeout;
    for (;;) {
        while (fParser.Next(reply)) {
            if (reply.command == answer)
                return true;
            if (reply.command == kAnswerPrintError) {
                error = ErrorText(reply);
                return false;
            }
            if (reply.command == kAnswerNotSupported) {
                error = "The printer does not understand this request.";
                return false;
            }
        }
        bigtime_t left = end - system_time();
        if (left <= 0 || !fLink.IsOpen())
            break;
        std::vector<uint8_t> data;
        if (fLink.Read(left, data))
            fParser.Feed(data.data(), data.size());
    }
    error = fLink.IsOpen() ? "The printer stopped answering." : "The connection to the printer was lost.";
    return false;
}

bool LabelPrinter::Identify(LabelPrinterInfo& info, std::string& error)
{
    Packet reply;
    if (!Request(Packet{kPrinterInfo, Bytes{kInfoModelId}}, kPrinterInfo + kInfoModelId, reply, 1000000, error))
        return false;
    if (reply.data.size() >= 2)
        info.modelId = (uint16_t)(reply.data[0] << 8 | reply.data[1]);
    const Model* model = ModelById(info.modelId);
    info.model = model ? model->name : "";
    std::string ignored;
    if (Request(Packet{kPrinterInfo, Bytes{kInfoSerial}}, kPrinterInfo + kInfoSerial, reply, 1000000, ignored))
        info.serial.assign(reply.data.begin(), reply.data.end());
    if (Request(Packet{kPrinterInfo, Bytes{kInfoBattery}}, kPrinterInfo + kInfoBattery, reply, 1000000, ignored)
        && !reply.data.empty())
        info.battery = reply.data[0];
    if (Request(Simple(kRfidInfo), kAnswerRfidInfo, reply, 1000000, ignored))
        ParseRfidInfo(reply, info.roll);
    return true;
}

bool LabelPrinter::Print(const Raster& raster, int copies, int density, int printheadDots,
    const std::function<void(float, const char*)>& progress, std::string& error)
{
    // The B1 sequence (NiimBlue's B1PrintTask): density, label type, start, page with its
    // size and copies, the rows, page end; then poll the status until the pages are out.
    Packet reply;
    progress(0, "Preparing the printer…");
    if (!Request(Simple(kSetDensity, (uint8_t)density), kAnswerSetDensity, reply, 1000000, error)
        || !Request(Simple(kSetLabelType, kLabelWithGaps), kAnswerSetLabelType, reply, 1000000, error)
        || !Request(PrintStart(copies), kAnswerPrintStart, reply, 2000000, error)
        || !Request(Simple(kPageStart), kAnswerPageStart, reply, 2000000, error)
        || !Request(PageSize(raster.rows, raster.cols, copies), kAnswerSetPageSize, reply, 2000000, error))
        return false;

    std::vector<Packet> rows = RowPackets(raster, printheadDots);
    for (size_t i = 0; i < rows.size(); i++) {
        if (!Send(rows[i])) {
            error = "The connection to the printer was lost.";
            return false;
        }
        // The printer has no flow control of its own besides RFCOMM's credits; NiimBlue
        // leaves a few milliseconds between packets as well.
        snooze(4000);
        if (i % 16 == 0)
            progress(0.3f * i / rows.size(), "Sending the label…");
    }
    bool ended = false;
    for (int attempt = 0; attempt < 50 && !ended; attempt++) {
        if (!Request(Simple(kPageEnd), kAnswerPageEnd, reply, 2000000, error))
            return false;
        ended = !reply.data.empty() && reply.data[0] != 0;
        if (!ended)
            snooze(200000);
    }
    if (!ended) {
        error = "The printer did not take the label.";
        return false;
    }

    bigtime_t end = system_time() + 20000000LL * copies;
    bool done = false;
    while (!done && system_time() < end) {
        PrintStatus status;
        if (Request(Simple(kPrintStatus), kAnswerPrintStatus, reply, 2000000, error)
            && ParsePrintStatus(reply, status)) {
            float page = std::min(1.0f, (status.page + status.printPercent / 100.0f) / copies);
            progress(0.3f + 0.7f * page, copies > 1 ? "Printing the labels…" : "Printing the label…");
            done = status.page >= copies;
        } else if (!fLink.IsOpen())
            return false;
        if (!done)
            snooze(300000);
    }
    std::string ignored;
    Request(Simple(kPrintEnd), kAnswerPrintEnd, reply, 2000000, ignored);
    if (!done) {
        error = "The printer did not report the label as printed.";
        return false;
    }
    progress(1, copies > 1 ? "Labels printed" : "Label printed");
    return true;
}

void AddPrinterInfo(BMessage& message, const LabelPrinterInfo& info)
{
    message.AddString("address", info.address.c_str());
    message.AddString("name", info.DisplayName().c_str());
    message.AddString("model", info.model.c_str());
    message.AddInt32("model_id", info.modelId);
    message.AddInt32("battery", info.battery);
    if (info.roll.present) {
        message.AddInt32("roll_total", info.roll.total);
        message.AddInt32("roll_used", info.roll.used);
    }
}

namespace {

// Collects the printers an inquiry finds; the agent runs the listener's looper.
class PrinterListener : public DiscoveryListener {
public:
    PrinterListener() : fDone(create_sem(0, "label printer inquiry")) {}
    ~PrinterListener() { delete_sem(fDone); }

    void DeviceDiscovered(RemoteDevice* device, DeviceClass deviceClass) override
    {
        // Major class 6 (imaging) with the printer bit of its minor class.
        uint32 record = deviceClass.Record();
        if (((record >> 8) & 0x1f) != 6 || (record & 0x80) == 0)
            return;
        std::lock_guard<std::mutex> lock(fMutex);
        std::string address = AddressText(device->GetBluetoothAddress());
        if (std::find(fFound.begin(), fFound.end(), address) == fFound.end())
            fFound.push_back(address);
    }
    void InquiryStarted(status_t status) override
    {
        if (status != B_OK)
            release_sem(fDone);
    }
    void InquiryResponse(int) override { release_sem(fDone); } // completed, terminated or failed

    bool Wait(bigtime_t timeout) { return acquire_sem_etc(fDone, 1, B_RELATIVE_TIMEOUT, timeout) == B_OK; }
    std::vector<std::string> Found()
    {
        std::lock_guard<std::mutex> lock(fMutex);
        return fFound;
    }

private:
    sem_id fDone;
    std::mutex fMutex;
    std::vector<std::string> fFound;
};

void Identify(const BMessenger& target, const std::string& address, bool reportFailure)
{
    LabelPrinter printer;
    LabelPrinterInfo info;
    info.address = address;
    std::string error;
    if (printer.Open(address, error) && printer.Identify(info, error)) {
        printer.Close();
        BMessage found(kMsgLabelPrinterFound);
        AddPrinterInfo(found, info);
        target.SendMessage(&found);
        return;
    }
    printer.Close();
    if (reportFailure) {
        BMessage failed(kMsgLabelSearchDone);
        failed.AddString("error", error.c_str());
        failed.AddString("address", address.c_str());
        failed.AddBool("identify", true);
        target.SendMessage(&failed);
    }
}

} // namespace

void StartLabelPrinterSearch(const BMessenger& target)
{
    std::thread([target]() {
        BMessage done(kMsgLabelSearchDone);
        LocalDevice* local = LocalDevice::GetLocalDevice();
        DiscoveryAgent* agent = local ? local->GetDiscoveryAgent() : nullptr;
        if (agent == nullptr) {
            done.AddString("error", "This computer has no Bluetooth controller, or Bluetooth is off.");
            target.SendMessage(&done);
            return;
        }
        PrinterListener* listener = new PrinterListener();
        const int32 length = 6; // 1.28 s units: about 7.7 s
        status_t status = agent->StartInquiry(BT_GIAC, listener, length);
        std::vector<std::string> found;
        if (status == B_OK) {
            listener->Wait(length * 1280000LL + 3000000);
            found = listener->Found();
        }
        if (listener->Lock())
            listener->Quit();
        if (status != B_OK) {
            done.AddString("error", "Bluetooth could not search for devices right now. Try again in a moment.");
            target.SendMessage(&done);
            return;
        }
        for (const std::string& address : found)
            Identify(target, address, false);
        done.AddInt32("count", (int32)found.size());
        target.SendMessage(&done);
    }).detach();
}

void StartLabelPrinterIdentify(const BMessenger& target, const std::string& address)
{
    std::thread([target, address]() { Identify(target, address, true); }).detach();
}

void StartLabelPrint(const BMessenger& target, const std::string& address, const Raster& raster, int copies,
    int density)
{
    std::thread([target, address, raster, copies, density]() {
        LabelPrinter printer;
        std::string error;
        auto progress = [&](float fraction, const char* text) {
            BMessage message(kMsgLabelProgress);
            message.AddFloat("fraction", fraction);
            message.AddString("text", text);
            target.SendMessage(&message);
        };
        progress(0, "Connecting to the printer…");
        bool ok = printer.Open(address, error);
        LabelPrinterInfo info;
        info.address = address;
        if (ok)
            ok = printer.Identify(info, error);
        if (ok && info.model.empty()) {
            error = "This printer (model " + std::to_string(info.modelId) + ") is not supported yet; Amp prints to "
                "the Niimbot B1.";
            ok = false;
        }
        if (ok) {
            const Model* model = ModelById(info.modelId);
            ok = printer.Print(raster, copies, density, model->printheadDots, progress, error);
        }
        printer.Close();
        BMessage finished(kMsgLabelPrinted);
        finished.AddBool("ok", ok);
        finished.AddString("error", error.c_str());
        if (ok)
            AddPrinterInfo(finished, info);
        target.SendMessage(&finished);
    }).detach();
}

} // namespace amp
