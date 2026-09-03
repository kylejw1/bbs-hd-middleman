#include "WebPortal.h"

WebPortal Portal;

WebPortal::WebPortal()
    : _server(HTTP_PORT)
    , _staConfigured(false)
{
}

void WebPortal::begin() {
    setupWifi();
    setupRoutes();
    _server.begin();
    Serial.println("[WebPortal] HTTP server started on port 80");
}

void WebPortal::setupWifi() {
    _prefs.begin("bbshd-cfg", false);
    _staSsid = _prefs.getString("sta_ssid", "");
    _staPass = _prefs.getString("sta_pass", "");
    _staConfigured = (_staSsid.length() > 0);

    WiFi.mode(WIFI_AP_STA);

    // Start SoftAP
    WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS, DEFAULT_AP_CHANNEL, 0, DEFAULT_AP_MAX_CONN);
    IPAddress apIP = WiFi.softAPIP();

    // Start DNS Captive Portal server
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(DNS_PORT, "*", apIP);

    Serial.printf("[WebPortal] SoftAP '%s' active at IP: %s\n", DEFAULT_AP_SSID, apIP.toString().c_str());

    // Connect to Station if configured
    if (_staConfigured) {
        Serial.printf("[WebPortal] Connecting to Station Wi-Fi '%s'...\n", _staSsid.c_str());
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    }
}

void WebPortal::process() {
    _dnsServer.processNextRequest();
    _server.handleClient();
}

void WebPortal::setupRoutes() {
    // Web UI
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });

    // REST APIs
    _server.on("/api/telemetry", HTTP_GET, [this]() { handleTelemetry(); });
    _server.on("/api/events", HTTP_GET, [this]() { handleEvents(); });
    _server.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
    _server.on("/api/config", HTTP_POST, [this]() { handlePostConfig(); });
    _server.on("/api/reset", HTTP_POST, [this]() { handleResetConfig(); });
    _server.on("/api/calibrate", HTTP_POST, [this]() { handleCalibrateVoltage(); });
    _server.on("/api/cmd/pas", HTTP_POST, [this]() { handleCmdPas(); });
    _server.on("/api/cmd/mode", HTTP_POST, [this]() { handleCmdMode(); });
    _server.on("/api/cmd/lights", HTTP_POST, [this]() { handleCmdLights(); });
    _server.on("/api/wifi", HTTP_POST, [this]() { handleWifiConfig(); });
    _server.on("/api/info", HTTP_GET, [this]() { handleInfo(); });

    // Captive Portal Redirects for iOS, Android, Windows
    _server.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/fwlink", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/canonical.html", HTTP_GET, [this]() { handleRoot(); });

    _server.onNotFound([this]() { handleNotFound(); });
}

void WebPortal::handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WebPortal::handleTelemetry() {
    JsonDocument doc;
    Telemetry.buildTelemetryJson(doc);

    String jsonStr;
    serializeJson(doc, jsonStr);
    _server.send(200, "application/json", jsonStr);
}

void WebPortal::handleEvents() {
    JsonDocument doc;
    Telemetry.buildEventsJson(doc);

    String jsonStr;
    serializeJson(doc, jsonStr);
    _server.send(200, "application/json", jsonStr);
}

void WebPortal::handleGetConfig() {
    BbsFwConfigV5 cfg;
    bool ok = Bridge.readConfig(cfg, 3500);

    if (!ok) {
        // Return default safe fallback config with warning flag if controller is offline
        initDefaultBbsHdConfig(cfg);
    }

    JsonDocument doc;
    serializeConfigToJson(cfg, doc);
    doc["fromController"] = ok;

    String jsonStr;
    serializeJson(doc, jsonStr);
    _server.send(200, "application/json", jsonStr);
}

void WebPortal::handlePostConfig() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    String body = _server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    BbsFwConfigV5 cfg;
    deserializeConfigFromJson(doc, cfg);

    bool ok = Bridge.writeConfig(cfg, 3500);

    JsonDocument resp;
    resp["success"] = ok;
    if (!ok) {
        resp["error"] = "Controller rejected config or timed out";
    }

    String jsonResp;
    serializeJson(resp, jsonResp);
    _server.send(ok ? 200 : 500, "application/json", jsonResp);
}

void WebPortal::handleResetConfig() {
    bool ok = Bridge.resetConfig(3000);

    JsonDocument resp;
    resp["success"] = ok;
    String jsonResp;
    serializeJson(resp, jsonResp);
    _server.send(ok ? 200 : 500, "application/json", jsonResp);
}

void WebPortal::handleCalibrateVoltage() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    float volts = doc["voltage"] | 0.0f;
    if (volts < 20.0f || volts > 70.0f) {
        _server.send(400, "application/json", "{\"error\":\"Voltage out of range\"}");
        return;
    }

    bool ok = Bridge.calibrateVoltage(volts, 2500);

    JsonDocument resp;
    resp["success"] = ok;
    String jsonResp;
    serializeJson(resp, jsonResp);
    _server.send(ok ? 200 : 500, "application/json", jsonResp);
}

void WebPortal::handleCmdPas() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));
    uint8_t lvl = doc["level"] | 1;

    Bridge.injectPasLevel(lvl);
    _server.send(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleCmdMode() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));
    uint8_t mode = doc["mode"] | 0;

    Bridge.injectOperationMode(mode);
    _server.send(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleCmdLights() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));
    bool on = doc["on"] | false;

    Bridge.injectLights(on);
    _server.send(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleWifiConfig() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));

    String ssid = doc["ssid"] | "";
    String pass = doc["pass"] | "";

    if (ssid.length() > 0) {
        _prefs.putString("sta_ssid", ssid);
        _prefs.putString("sta_pass", pass);
        _staSsid = ssid;
        _staPass = pass;
        _staConfigured = true;

        WiFi.begin(ssid.c_str(), pass.c_str());
        _server.send(200, "application/json", "{\"success\":true}");
    } else {
        _server.send(400, "application/json", "{\"error\":\"Invalid SSID\"}");
    }
}

void WebPortal::handleInfo() {
    JsonDocument doc;
    doc["model"] = "ESP32-S3 (44-Pin DevKitC-1)";
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["flashSize"] = ESP.getFlashChipSize();
    doc["uptimeSec"] = millis() / 1000;
    doc["wifiMode"] = (WiFi.status() == WL_CONNECTED) ? "AP+STA" : "AP_ONLY";
    doc["staIP"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Not Connected";
    doc["apIP"] = WiFi.softAPIP().toString();

    String resp;
    serializeJson(doc, resp);
    _server.send(200, "application/json", resp);
}

void WebPortal::handleNotFound() {
    // If request was from a captive portal check, redirect to root
    String host = _server.hostHeader();
    if (host != WiFi.softAPIP().toString()) {
        _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        _server.send(302, "text/plain", "");
        return;
    }

    _server.send(404, "text/plain", "Not Found");
}
