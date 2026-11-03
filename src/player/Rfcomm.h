// A minimal RFCOMM client (Bluetooth serial port, TS 07.10 with credit based flow control) in
// user land, on top of a classic L2CAP channel to PSM 3. Haiku's Bluetooth stack has no
// RFCOMM; this is enough to talk to one serial port of one device.
#pragma once
#include <SupportDefs.h>
#include <bluetooth/bluetooth.h>
#include <cstdint>
#include <string>
#include <vector>

namespace amp {

class Rfcomm {
public:
    Rfcomm() = default;
    ~Rfcomm();

    // Opens server channel `channel` of the device, asking the Bluetooth server for a link
    // (and so for pairing) when there is none yet. `error` says why it failed.
    bool Open(const bdaddr_t& address, int channel, std::string& error);
    void Close();
    bool IsOpen() const { return fOpen; }

    bool Write(const uint8_t* data, size_t length);
    // Appends what arrives within `timeout` (microseconds) to `buffer`. False on a timeout
    // with nothing new, or when the link went away.
    bool Read(bigtime_t timeout, std::vector<uint8_t>& buffer);

private:
    bool SendFrame(uint8_t dlci, uint8_t control, bool command, const uint8_t* data, size_t length,
        int credits = -1);
    bool SendControl(uint8_t type, bool command, const uint8_t* data, size_t length);
    bool Receive(bigtime_t timeout, std::vector<uint8_t>* buffer, uint8_t* frameDlci = nullptr,
        uint8_t* frameType = nullptr);
    bool WaitFor(uint8_t dlci, uint8_t type, bigtime_t timeout);
    void HandleControl(const uint8_t* data, size_t length);
    void Drop();

    int fSocket = -1;
    uint8_t fDlci = 0;
    size_t fMtu = 127;
    bool fCreditFlow = false;
    int fSendCredits = 0;
    int fReceiveCredits = 0;
    bool fOpen = false;
    bool fParametersAnswered = false;
    uint8_t fParameters[8] = {};
    bool fModemStatusAnswered = false;
    bool fModemStatusFromPeer = false;
    std::vector<uint8_t> fPending; // data that came while waiting for credits
};

} // namespace amp
