// Haiku's public <bluetooth/RemoteDevice.h> includes the Bluetooth server's private header,
// which the SDK does not ship. RemoteDevice.h uses nothing from it, so this empty stand-in
// lets the header compile. It is searched after the system directories (-idirafter), so a
// real private header, where there is one, still wins.
#pragma once
