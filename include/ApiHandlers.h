#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include <Arduino.h>

// Transport-neutral request/response types for the dashboard API.
//
// Both the HTTP routes (WebPortal) and the BLE GATT transport (BlePortal) funnel
// through WebPortal::dispatchApi(), so a client gets byte-identical JSON whether
// it is on Wi-Fi or Bluetooth. Never give one transport its own copy of a route:
// the whole point of this indirection is that a fix or a new field lands on both
// at once.
struct ApiRequest {
    const char* path = "";         // route only, e.g. "/api/telemetry"
    bool post = false;             // true for POST, false for GET
    const String* body = nullptr;  // POST body; null when the client sent none
    uint32_t afterSeq = 0;         // /api/serial-trace cursor, from ?after=N
};

struct ApiResponse {
    int status = 200;
    String body;
    bool html = false;  // true only for the dashboard document itself
};

// One row per REST route. The same table registers the HTTP handlers and drives
// the BLE dispatch switch, so a route cannot exist on one transport alone.
enum class ApiRouteId : uint8_t {
    Telemetry,
    Events,
    GetConfig,
    PostConfig,
    ResetConfig,
    CalibrateVoltage,
    CmdPas,
    CmdMode,
    CmdLights,
    WifiConfig,
    Info,
    SerialTrace,
    DebugConfig,
    BlePin,
};

struct ApiRoute {
    ApiRouteId id;
    const char* path;
    bool post;  // false = GET
};

extern const ApiRoute API_ROUTES[];
extern const size_t API_ROUTE_COUNT;

// Returns the matching route, or nullptr when the path/method pair is unknown.
// `path` must not contain a query string.
const ApiRoute* findApiRoute(const char* path, bool post);

// Parses the `after` cursor out of a raw query string ("after=12&other=x").
// The HTTP server parses its own args, so this exists for the BLE transport.
uint32_t parseAfterParam(const char* query);

#endif // API_HANDLERS_H
