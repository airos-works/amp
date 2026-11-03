#include "Rfcomm.h"

#include <Messenger.h>
#include <OS.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/L2CAP/btL2CAP.h>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef B_L2CAP_OUTGOING_MTU
// Older SDKs cannot tell the channel's MTU; asking then fails and the default is used.
#define B_L2CAP_OUTGOING_MTU 1
#endif

namespace amp {

namespace {

// Frame types (control field without the P/F bit) and multiplexer commands.
enum : uint8_t { kSABM = 0x2f, kUA = 0x63, kDM = 0x0f, kDISC = 0x43, kUIH = 0xef, kPF = 0x10 };
enum : uint8_t {
    kParameters = 0x20, kModemStatus = 0x38, kPortSettings = 0x24, kLineStatus = 0x14, kTest = 0x08,
    kFlowOn = 0x28, kFlowOff = 0x18, kNotSupported = 0x04
};

// The Bluetooth server's private protocol (headers/private/bluetooth/bluetoothserver_p.h).
const char* kBluetoothServer = "application/x-vnd.Haiku-bluetooth_server";
const uint32 kAcquireLocalDevice = 'btAd';
const uint32 kCreateConnection = 'rdCN';

uint8_t sCrc[256];

uint8_t Fcs(const uint8_t* data, int length)
{
    if (sCrc[1] == 0) {
        for (int i = 0; i < 256; i++) {
            uint8_t c = (uint8_t)i;
            for (int bit = 0; bit < 8; bit++)
                c = (c & 1) ? (c >> 1) ^ 0xe0 : c >> 1;
            sCrc[i] = c;
        }
    }
    uint8_t fcs = 0xff;
    while (length-- > 0)
        fcs = sCrc[fcs ^ *data++];
    return 0xff - fcs;
}

status_t RequestLink(const bdaddr_t& address)
{
    BMessenger server(kBluetoothServer);
    if (!server.IsValid())
        return B_NAME_NOT_FOUND;
    BMessage request(kAcquireLocalDevice), reply;
    int32 hci = -1;
    if (server.SendMessage(&request, &reply, 5000000, 5000000) != B_OK
        || reply.FindInt32("hci_id", &hci) != B_OK || hci < 0)
        return B_DEV_NOT_READY;
    request.MakeEmpty();
    request.what = kCreateConnection;
    request.AddInt32("hci_id", hci);
    request.AddData("bdaddr", B_ANY_TYPE, &address, sizeof(bdaddr_t));
    request.AddString("name", "Label printer");
    request.AddUInt32("record", 0x040680);
    request.AddUInt16("packet type", HCI_DM1 | HCI_DH1 | HCI_DM3 | HCI_DH3 | HCI_DM5 | HCI_DH5);
    request.AddUInt8("pscan_rep_mode", 1);
    request.AddUInt8("pscan_mode", 0);
    request.AddUInt16("clock_offset", 0);
    request.AddUInt8("role_switch", 1);
    reply.MakeEmpty();
    return server.SendMessage(&request, &reply, 5000000, 5000000);
}

} // namespace

Rfcomm::~Rfcomm()
{
    Close();
}

bool Rfcomm::SendFrame(uint8_t dlci, uint8_t control, bool command, const uint8_t* data, size_t length,
    int credits)
{
    if (fSocket < 0 || length > 2048)
        return false;
    uint8_t frame[2048 + 8];
    size_t n = 0;
    // Address: we are the initiator, so C/R is 1 on our commands and 0 on our answers.
    frame[n++] = (uint8_t)(dlci << 2 | (command ? 2 : 0) | 1);
    frame[n++] = control;
    if (length > 127) {
        frame[n++] = (uint8_t)((length & 0x7f) << 1);
        frame[n++] = (uint8_t)(length >> 7);
    } else
        frame[n++] = (uint8_t)(length << 1 | 1);
    size_t header = n;
    if (credits >= 0)
        frame[n++] = (uint8_t)credits;
    if (length > 0)
        memcpy(frame + n, data, length);
    n += length;
    // UIH frames check only address and control; the others the length too.
    frame[n++] = Fcs(frame, (control & ~kPF) == kUIH ? 2 : (int)header);
    if (send(fSocket, frame, n, 0) != (ssize_t)n) {
        Drop();
        return false;
    }
    return true;
}

bool Rfcomm::SendControl(uint8_t type, bool command, const uint8_t* data, size_t length)
{
    uint8_t message[64];
    if (length > sizeof(message) - 2)
        return false;
    message[0] = (uint8_t)(type << 2 | (command ? 2 : 0) | 1);
    message[1] = (uint8_t)(length << 1 | 1);
    if (length > 0)
        memcpy(message + 2, data, length);
    // Multiplexer messages go on DLCI 0 in UIH frames with the initiator's C/R.
    return SendFrame(0, kUIH, true, message, length + 2);
}

void Rfcomm::HandleControl(const uint8_t* data, size_t length)
{
    if (length < 2)
        return;
    uint8_t type = data[0] >> 2;
    bool command = (data[0] & 2) != 0;
    size_t valueLength = data[1] >> 1;
    const uint8_t* value = data + 2;
    if (2 + valueLength > length)
        return;
    switch (type) {
        case kParameters:
            if (valueLength < 8)
                break;
            if (!command) {
                memcpy(fParameters, value, 8);
                fParametersAnswered = true;
            } else {
                uint8_t answer[8];
                memcpy(answer, value, 8);
                answer[1] = value[1] == 0xf0 ? 0xe0 : 0;
                SendControl(kParameters, false, answer, 8);
            }
            break;
        case kModemStatus:
            if (command) {
                fModemStatusFromPeer = true;
                SendControl(kModemStatus, false, value, valueLength);
            } else
                fModemStatusAnswered = true;
            break;
        case kPortSettings:
        case kLineStatus:
        case kTest:
            if (command)
                SendControl(type, false, value, valueLength);
            break;
        case kFlowOn:
        case kFlowOff:
            if (command)
                SendControl(type, false, nullptr, 0);
            break;
        default:
            if (command)
                SendControl(kNotSupported, false, data, 1);
            break;
    }
}

bool Rfcomm::Receive(bigtime_t timeout, std::vector<uint8_t>* buffer, uint8_t* frameDlci, uint8_t* frameType)
{
    if (fSocket < 0)
        return false;
    struct pollfd pfd = {fSocket, POLLIN, 0};
    int result = poll(&pfd, 1, timeout < 0 ? -1 : (int)(timeout / 1000));
    if (result <= 0)
        return false;
    uint8_t frame[4096];
    ssize_t n = recv(fSocket, frame, sizeof(frame), 0);
    if (n <= 0) {
        Drop();
        return false;
    }
    if (n < 4)
        return true;
    uint8_t dlci = frame[0] >> 2;
    uint8_t control = frame[1];
    uint8_t type = control & ~kPF;
    size_t pos = 2;
    size_t length = frame[pos] >> 1;
    if (frame[pos++] & 1) {
        // one length byte
    } else
        length |= (size_t)frame[pos++] << 7;
    if (frameDlci)
        *frameDlci = dlci;
    if (frameType)
        *frameType = type;
    if (type == kUIH) {
        if ((control & kPF) && fCreditFlow && dlci != 0 && pos < (size_t)n)
            fSendCredits += frame[pos++];
        if (pos + length > (size_t)n - 1)
            length = pos < (size_t)n - 1 ? (size_t)n - 1 - pos : 0;
        if (dlci == 0)
            HandleControl(frame + pos, length);
        else if (length > 0 && buffer) {
            buffer->insert(buffer->end(), frame + pos, frame + pos + length);
            if (fCreditFlow && --fReceiveCredits < 4) {
                SendFrame(fDlci, kUIH | kPF, true, nullptr, 0, 16);
                fReceiveCredits += 16;
            }
        }
    } else if (type == kDISC) {
        SendFrame(dlci, kUA | kPF, false, nullptr, 0);
        if (dlci == 0 || dlci == fDlci)
            fOpen = false;
    } else if (type == kDM && dlci == fDlci)
        fOpen = false;
    return true;
}

bool Rfcomm::WaitFor(uint8_t dlci, uint8_t type, bigtime_t timeout)
{
    bigtime_t end = system_time() + timeout;
    while (fSocket >= 0) {
        bigtime_t left = end - system_time();
        if (left <= 0)
            return false;
        uint8_t frameDlci = 0xff, frameType = 0;
        if (!Receive(left, &fPending, &frameDlci, &frameType))
            continue;
        if (frameDlci == dlci && frameType == type)
            return true;
        if (frameDlci == dlci && frameType == kDM)
            return false;
    }
    return false;
}

bool Rfcomm::Open(const bdaddr_t& address, int channel, std::string& error)
{
    Close();
    sockaddr_l2cap remote = {};
    remote.l2cap_len = sizeof(remote);
    remote.l2cap_family = AF_BLUETOOTH;
    remote.l2cap_bdaddr = address;
    remote.l2cap_psm = B_HOST_TO_LENDIAN_INT16(3);

    // L2CAP needs a link to the device; the Bluetooth server makes one (pairing on the first
    // connection) when asked.
    bigtime_t start = system_time();
    bool asked = false;
    for (;;) {
        fSocket = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
        if (fSocket < 0) {
            error = std::string("Bluetooth is not available (") + strerror(errno) + ")";
            return false;
        }
        if (connect(fSocket, (sockaddr*)&remote, sizeof(remote)) == 0)
            break;
        int reason = errno;
        close(fSocket);
        fSocket = -1;
        if ((reason == ENETUNREACH || reason == EHOSTUNREACH) && system_time() - start < 20000000) {
            if (!asked) {
                status_t status = RequestLink(address);
                if (status != B_OK) {
                    error = std::string("the Bluetooth server did not answer (") + strerror(status) + ")";
                    return false;
                }
                asked = true;
            }
            snooze(250000);
            continue;
        }
        error = reason == ENETUNREACH || reason == EHOSTUNREACH
            ? std::string("the printer did not answer. Is it switched on and in reach?")
            : std::string("no connection to the printer (") + strerror(reason) + ")";
        return false;
    }
    uint16 outgoingMtu = 672;
    socklen_t size = sizeof(outgoingMtu);
    if (getsockopt(fSocket, BLUETOOTH_PROTO_L2CAP, B_L2CAP_OUTGOING_MTU, &outgoingMtu, &size) != 0)
        outgoingMtu = 672;

    // Start the multiplexer, agree on the parameters of the channel, open it.
    if (!SendFrame(0, kSABM | kPF, true, nullptr, 0) || !WaitFor(0, kUA, 5000000)) {
        error = "the printer refused the serial connection";
        Close();
        return false;
    }
    fDlci = (uint8_t)(channel << 1);
    uint16_t mtu = outgoingMtu > 1006 ? 1000 : outgoingMtu - 6;
    uint8_t parameters[8] = {fDlci, 0xf0, 7, 0, (uint8_t)mtu, (uint8_t)(mtu >> 8), 0, 7};
    fParametersAnswered = false;
    SendControl(kParameters, true, parameters, 8);
    bigtime_t end = system_time() + 3000000;
    while (!fParametersAnswered && fSocket >= 0 && system_time() < end)
        Receive(end - system_time(), &fPending);
    if (fParametersAnswered) {
        fCreditFlow = fParameters[1] == 0xe0;
        fMtu = fParameters[4] | fParameters[5] << 8;
        if (fMtu == 0 || fMtu > mtu)
            fMtu = mtu;
        fSendCredits = fCreditFlow ? fParameters[7] : 0;
        fReceiveCredits = 7;
    }
    if (!SendFrame(fDlci, kSABM | kPF, true, nullptr, 0) || !WaitFor(fDlci, kUA, 5000000)) {
        error = "the printer has no serial port on channel " + std::to_string(channel);
        Close();
        return false;
    }
    uint8_t status[2] = {(uint8_t)(fDlci << 2 | 2 | 1), 0x8d}; // RTC, RTR, DV
    fModemStatusAnswered = fModemStatusFromPeer = false;
    SendControl(kModemStatus, true, status, 2);
    end = system_time() + 2000000;
    while ((!fModemStatusAnswered || !fModemStatusFromPeer) && fSocket >= 0 && system_time() < end)
        Receive(end - system_time(), &fPending);
    fOpen = fSocket >= 0;
    if (!fOpen)
        error = "the printer closed the connection";
    return fOpen;
}

void Rfcomm::Drop()
{
    if (fSocket >= 0)
        close(fSocket);
    fSocket = -1;
    fOpen = false;
}

void Rfcomm::Close()
{
    if (fSocket < 0)
        return;
    if (fOpen && SendFrame(fDlci, kDISC | kPF, true, nullptr, 0))
        WaitFor(fDlci, kUA, 1000000);
    if (fSocket >= 0 && SendFrame(0, kDISC | kPF, true, nullptr, 0))
        WaitFor(0, kUA, 1000000);
    Drop();
    fPending.clear();
}

bool Rfcomm::Write(const uint8_t* data, size_t length)
{
    while (length > 0) {
        if (!fOpen)
            return false;
        if (fCreditFlow) {
            bigtime_t end = system_time() + 5000000;
            while (fSendCredits <= 0 && fOpen && system_time() < end)
                Receive(end - system_time(), &fPending);
            if (fSendCredits <= 0)
                return false;
        }
        size_t chunk = length > fMtu ? fMtu : length;
        if (!SendFrame(fDlci, kUIH, true, data, chunk))
            return false;
        if (fCreditFlow)
            fSendCredits--;
        data += chunk;
        length -= chunk;
    }
    return true;
}

bool Rfcomm::Read(bigtime_t timeout, std::vector<uint8_t>& buffer)
{
    if (!fPending.empty()) {
        buffer.insert(buffer.end(), fPending.begin(), fPending.end());
        fPending.clear();
        return true;
    }
    size_t before = buffer.size();
    bigtime_t end = system_time() + timeout;
    while (fOpen) {
        bigtime_t left = end - system_time();
        if (left <= 0)
            return false;
        if (Receive(left, &buffer) && buffer.size() > before)
            return true;
    }
    return false;
}

} // namespace amp
