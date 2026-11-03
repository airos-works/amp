// Niimbot label printers over Bluetooth: finding them, asking what they are, printing a label.
// Every call that talks to a printer blocks for seconds; the Start* functions run one on a
// thread of its own and report with messages (see Messages.h, kMsgLabel*).
#pragma once
#include "Rfcomm.h"
#include "core/Niimbot.h"

#include <Messenger.h>
#include <functional>
#include <string>

namespace amp {

struct LabelPrinterInfo {
    std::string address;        // classic Bluetooth address, "13:11:08:58:6E:B3"
    uint16_t modelId = 0;
    std::string model;          // "B1", or "" for a model this code cannot print to
    std::string serial;
    int battery = -1;           // 0..4 as the printer reports it
    niimbot::RfidInfo roll;
    // "B1 I811131558" (or the address when nothing better is known)
    std::string DisplayName() const;
};

class LabelPrinter {
public:
    bool Open(const std::string& address, std::string& error);
    void Close() { fLink.Close(); }
    bool Identify(LabelPrinterInfo& info, std::string& error);
    // Prints `copies` of the raster. `progress` gets 0..1 and a line of text.
    bool Print(const niimbot::Raster& raster, int copies, int density, int printheadDots,
        const std::function<void(float, const char*)>& progress, std::string& error);

private:
    bool Send(const niimbot::Packet& packet);
    bool Request(const niimbot::Packet& packet, uint8_t answer, niimbot::Packet& reply,
        bigtime_t timeout, std::string& error);

    Rfcomm fLink;
    niimbot::Parser fParser;
};

// A Bluetooth inquiry for printers (devices of the imaging class with the printer bit), then
// a short connection to each to learn its model: kMsgLabelPrinterFound per printer, then
// kMsgLabelSearchDone ("error" string when the inquiry could not run).
void StartLabelPrinterSearch(const BMessenger& target);
// kMsgLabelPrinterFound with the printer's details, or kMsgLabelSearchDone with "error".
void StartLabelPrinterIdentify(const BMessenger& target, const std::string& address);
// kMsgLabelProgress ("fraction" float, "text" string) while printing, then kMsgLabelPrinted
// ("ok" bool, "error" string).
void StartLabelPrint(const BMessenger& target, const std::string& address, const niimbot::Raster& raster,
    int copies, int density);

void AddPrinterInfo(BMessage& message, const LabelPrinterInfo& info);

} // namespace amp
