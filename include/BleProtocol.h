#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <Arduino.h>

// GATT identifiers for the BLE transport.
//
// The dashboard hardcodes these same four strings in web/index.html for
// navigator.bluetooth.requestDevice({filters:[{services:[...]}]}) -- change them
// in both places or discovery silently stops working.
#define BLE_SERVICE_UUID        "6b9a0001-7c3f-4a5e-9c8d-1e2f3a4b5c6d"
#define BLE_CHAR_INFO_UUID      "6b9a0002-7c3f-4a5e-9c8d-1e2f3a4b5c6d"  // read
#define BLE_CHAR_REQUEST_UUID   "6b9a0003-7c3f-4a5e-9c8d-1e2f3a4b5c6d"  // write
#define BLE_CHAR_RESPONSE_UUID  "6b9a0004-7c3f-4a5e-9c8d-1e2f3a4b5c6d"  // notify

// -----------------------------------------------------------------------------
// Both directions are byte streams rather than single packets: a request path is
// short, but a v6 config body is roughly 3 KB, which exceeds even a 517 byte ATT
// MTU. The client therefore writes consecutive slices and the device notifies
// consecutive slices, and each side reassembles them in order.
//
// All integers are little endian.
//
// REQUEST (client -> device), written to BLE_CHAR_REQUEST_UUID:
//     u16 seq | u8 method | u8 reserved | u16 pathLen | u16 bodyLen | path | body
//   method: 0 = GET, 1 = POST. The path may carry a query string
//   (e.g. "/api/serial-trace?after=12"); the device parses it.
//
// RESPONSE (device -> client), notified on BLE_CHAR_RESPONSE_UUID. The header is
// repeated on every slice so each notification is self-describing:
//     u16 seq | u16 status | u16 totalLen | body slice
//
// `seq` is echoed from the request. Sequence numbers are split by direction so a
// push can never be mistaken for a reply:
//     client requests : 1 .. BLE_REQUEST_SEQ_MAX
//     device pushes   : BLE_PUSH_SEQ_BASE .. 0xFFFF
//
// Each push gets its own incrementing seq rather than a fixed id, so the client
// reassembles pushes per message. A push whose chunks are partially lost then
// self-heals when the next one arrives, instead of corrupting a shared buffer.
//
// For a push, the `status` field carries BLE_PUSH_* (the push kind) and
// `totalLen` plus the body slice work exactly as for a reply.
#define BLE_REQUEST_SEQ_MAX     0x7fff
#define BLE_PUSH_SEQ_BASE       0x8000

#define BLE_PUSH_TELEMETRY      1
#define BLE_PUSH_EVENTS         2

#define BLE_REQUEST_HEADER_LEN  8
#define BLE_RESPONSE_HEADER_LEN 6

#define BLE_METHOD_GET          0
#define BLE_METHOD_POST         1

// BLE-only route: the client presents the device PIN here to unlock the rest of
// the API for the current connection. Deliberately *not* in API_ROUTES, because
// the HTTP transport has no equivalent gate -- reaching the Wi-Fi page already
// requires being on the bike's own network or access point, and that page is the
// recovery path if the PIN is ever forgotten.
#define BLE_AUTH_PATH           "/api/auth"

#endif // BLE_PROTOCOL_H
