#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "Config.h"
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

    WifiPhase _wifiPhase;
    bool _apActive;
    bool _mdnsStarted;
    bool _pendingStaConnect;
    uint32_t _staAttemptStartMs;
    uint32_t _scanStartMs;
    uint32_t _staDownSinceMs;
    uint32_t _pendingStaConnectMs;

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
    void handleOtaComplete();
    void handleOtaUpload();
    void handleNotFound();
};

extern WebPortal Portal;

#endif // WEB_PORTAL_H
