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
#include "WebContent.h"

class WebPortal {
public:
    WebPortal();

    void begin();
    void process();

private:
    WebServer _server;
    DNSServer _dnsServer;
    Preferences _prefs;

    String _staSsid;
    String _staPass;
    bool _staConfigured;

    void setupRoutes();
    void setupWifi();

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
    void handleNotFound();
};

extern WebPortal Portal;

#endif // WEB_PORTAL_H
