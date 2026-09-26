#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "ApiHandlers.h"
#include "BbsFwProtocol.h"
#include "Telemetry.h"
#include "SerialBridge.h"
#include "DebugLog.h"
#include "WebContent.h"

class WebPortal {
public:
    WebPortal();

    void begin();
    void process();

    // Transport-neutral API entry point. The HTTP routes and the BLE GATT
    // transport both call this, so a route can never behave differently between
    // the two. Returns false when the path/method pair is not a known route.
    bool dispatchApi(const ApiRequest& req, ApiResponse& out);

    // Bluetooth PIN. The BLE transport enforces it (see BlePortal); the Wi-Fi
    // page deliberately does not, because reaching it already requires being on
    // the bike's own network or access point -- and that is the recovery path if
    // the PIN is ever forgotten. An empty PIN means the BLE API is open.
    String getBlePin() const;
    void setBlePin(const String& pin);

private:
    // Wi-Fi lifecycle. The device prefers the configured router (STA) and only
    // runs its own SoftAP as a fallback, because AP+STA on one radio is slow.
    enum class WifiPhase : uint8_t {
        StaConnecting,  // trying to join the configured router
        StaConnected,   // station link up; SoftAP and captive portal are off
        ApScanning,     // async channel scan before bringing the SoftAP up
        ApOnly          // SoftAP up; DNS captive portal active
    };

    WebServer _server;
    DNSServer _dnsServer;
    Preferences _prefs;

    String _staSsid;
    String _staPass;
    bool _staConfigured;

    // Bluetooth PIN, mirrored in NVS under "ble_pin".
    String _blePin;

    WifiPhase _wifiPhase;
    bool _apActive;
    bool _mdnsStarted;
    bool _pendingStaConnect;
    uint32_t _staAttemptStartMs;
    uint32_t _scanStartMs;
    uint32_t _staDownSinceMs;
    uint32_t _pendingStaConnectMs;

    // API capture sink. The HTTP handlers never touch _server for an API reply;
    // they call sendApi(). While the BLE transport dispatches a request these
    // point at an ApiResponse instead, which lets one handler implementation
    // serve both transports byte-for-byte. Cleared again before process()
    // touches the HTTP server, so a capture can never leak between requests.
    ApiResponse* _apiOut = nullptr;
    const String* _apiBody = nullptr;
    bool _apiIsPost = false;
    uint32_t _apiAfterSeq = 0;

    void setupRoutes();
    void setupWifi();

    // Wi-Fi lifecycle helpers (all non-blocking; driven from process())
    void startStationAttempt(bool keepAccessPoint);
    void requestAccessPoint();
    void startAccessPoint(uint8_t channel);
    void stopAccessPoint();
    uint8_t pickBestApChannel();
    void startMdns();

    // Route handlers
    void handleRoot();
    void handleTelemetry();
    void handleEvents();
    void handleGetConfig();
    void handlePostConfig();
    void handleResetConfig();
    void handleCalibrateVoltage();
    void handleCmdPas();
    void handleCmdMode();
    void handleCmdLights();
    void handleWifiConfig();
    void handleInfo();
    void handleSerialTrace();
    void handleDebugConfig();
    void handleBlePin();
    void handleOtaComplete();
    void handleOtaUpload();
    void handleNotFound();

    // HTTP-side glue: fills an ApiRequest from the live WebServer request and
    // writes the ApiResponse back out.
    void serveApi(const ApiRequest& req);

    // Response sink shared by both transports. With _apiOut set the reply is
    // captured; otherwise it goes straight to the HTTP client.
    void sendApi(int status, const char* contentType, const String& body);
};

extern WebPortal Portal;

#endif // WEB_PORTAL_H
