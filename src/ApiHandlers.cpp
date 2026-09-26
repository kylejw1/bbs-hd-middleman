#include "ApiHandlers.h"

#include <stdlib.h>
#include <string.h>

// The single route table. WebPortal::setupRoutes() registers one HTTP handler per
// row and WebPortal::dispatchApi() switches on the matching ApiRouteId, so the
// HTTP and BLE transports can never expose different route sets.
const ApiRoute API_ROUTES[] = {
    { ApiRouteId::Telemetry,        "/api/telemetry",    false },
    { ApiRouteId::Events,           "/api/events",       false },
    { ApiRouteId::GetConfig,        "/api/config",       false },
    { ApiRouteId::PostConfig,       "/api/config",       true  },
    { ApiRouteId::ResetConfig,      "/api/reset",        true  },
    { ApiRouteId::CalibrateVoltage, "/api/calibrate",    true  },
    { ApiRouteId::CmdPas,           "/api/cmd/pas",      true  },
    { ApiRouteId::CmdMode,          "/api/cmd/mode",     true  },
    { ApiRouteId::CmdLights,        "/api/cmd/lights",   true  },
    { ApiRouteId::WifiConfig,       "/api/wifi",         true  },
    { ApiRouteId::Info,             "/api/info",         false },
    { ApiRouteId::SerialTrace,      "/api/serial-trace", false },
    { ApiRouteId::DebugConfig,      "/api/debug",        false },
    { ApiRouteId::DebugConfig,      "/api/debug",        true  },
    { ApiRouteId::BlePin,           "/api/ble-pin",      false },
    { ApiRouteId::BlePin,           "/api/ble-pin",      true  },
};

const size_t API_ROUTE_COUNT = sizeof(API_ROUTES) / sizeof(API_ROUTES[0]);

const ApiRoute* findApiRoute(const char* path, bool post) {
    if (path == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < API_ROUTE_COUNT; ++i) {
        if (API_ROUTES[i].post == post && strcmp(API_ROUTES[i].path, path) == 0) {
            return &API_ROUTES[i];
        }
    }

    return nullptr;
}

uint32_t parseAfterParam(const char* query) {
    if (query == nullptr) {
        return 0;
    }

    const char* at = strstr(query, "after=");
    if (at == nullptr) {
        return 0;
    }

    return (uint32_t)strtoul(at + 6, nullptr, 10);
}
