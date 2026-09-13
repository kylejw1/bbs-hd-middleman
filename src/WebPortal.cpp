#include "WebPortal.h"
#include <Update.h>
#include <ESPmDNS.h>

WebPortal Portal;

WebPortal::WebPortal()
    : _server(HTTP_PORT)
    , _staConfigured(false)
    , _wifiPhase(WifiPhase::StaConnecting)
    , _apActive(false)
    , _mdnsStarted(false)
    , _pendingStaConnect(false)
    , _staAttemptStartMs(0)
    , _scanStartMs(0)
    , _staDownSinceMs(0)
    , _pendingStaConnectMs(0)
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

    _apActive = false;
    _mdnsStarted = false;
    _pendingStaConnect = false;
    _staDownSinceMs = 0;
    _scanStartMs = 0;

    if (_staConfigured) {
        // Station first: the router's radio does the AP work, the ESP32 is just
        // a client, and there is no single-radio AP+STA contention.
        Serial.printf("[WebPortal] Station configured: '%s'\n", _staSsid.c_str());
        startStationAttempt(false);
    } else {
        Serial.println("[WebPortal] No station configured; starting SoftAP");
        requestAccessPoint();
    }
}

// Begin (or restart) a join attempt. When keepAccessPoint is true we leave the
// SoftAP up so the caller's current web session is not interrupted; it is torn
// down as soon as the station link comes up.
void WebPortal::startStationAttempt(bool keepAccessPoint) {
    if (!(keepAccessPoint && _apActive)) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.setSleep(false);  // no modem sleep: lowest dashboard latency
    WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    _wifiPhase = WifiPhase::StaConnecting;
    _staAttemptStartMs = millis();
    Serial.printf("[WebPortal] Connecting to station '%s'...\n", _staSsid.c_str());
}

void WebPortal::startMdns() {
    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        _mdnsStarted = true;
        Serial.printf("[WebPortal] mDNS ready: http://%s.local/\n", MDNS_HOSTNAME);
    } else {
        Serial.println("[WebPortal] mDNS start failed");
    }
}

// Ask for a fallback SoftAP. The channel scan runs asynchronously so the UART
// bridge keeps its sub-2ms display keep-alive cadence.
void WebPortal::requestAccessPoint() {
    if (_apActive || _wifiPhase == WifiPhase::ApScanning) return;

    WiFi.mode(WIFI_STA);            // scanning requires the station interface
    WiFi.setSleep(false);
    WiFi.scanNetworks(true, true);  // async: never block loop()
    _scanStartMs = millis();
    _wifiPhase = WifiPhase::ApScanning;
}

uint8_t WebPortal::pickBestApChannel() {
    const uint8_t candidates[3] = {1, 6, 11};
    uint16_t congestion[3] = {0, 0, 0};

    int n = WiFi.scanComplete();
    if (n <= 0) {
        WiFi.scanDelete();
        Serial.println("[WebPortal] AP channel scan empty/failed; using default");
        return DEFAULT_AP_CHANNEL;
    }

    // Weight each candidate by every network that overlaps its 5-channel span.
    for (int i = 0; i < n; ++i) {
        int ch = WiFi.channel(i);
        if (ch >= 1 && ch <= 5)  congestion[0]++;
        if (ch >= 2 && ch <= 8)  congestion[1]++;
        if (ch >= 7 && ch <= 13) congestion[2]++;
    }
    WiFi.scanDelete();

    uint8_t best = 1;  // prefer channel 6 on a tie (least congested default)
    for (uint8_t c = 0; c < 3; ++c) {
        if (congestion[c] < congestion[best]) best = c;
    }

    Serial.printf("[WebPortal] AP channel congestion 1:%u 6:%u 11:%u -> using %u\n",
                  (unsigned)congestion[0], (unsigned)congestion[1],
                  (unsigned)congestion[2], (unsigned)candidates[best]);
    return candidates[best];
}

void WebPortal::startAccessPoint(uint8_t channel) {
    // AP_STA with an idle station keeps the door open for a later join request
    // without paying any coexistence cost (the STA interface is not associated).
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    if (!WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS, channel, 0, DEFAULT_AP_MAX_CONN)) {
        Serial.println("[WebPortal] SoftAP start FAILED");
    }

    IPAddress apIP = WiFi.softAPIP();
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(DNS_PORT, "*", apIP);   // captive portal: hijack all DNS
    _apActive = true;
    _wifiPhase = WifiPhase::ApOnly;

    startMdns();
    Serial.printf("[WebPortal] SoftAP '%s' on channel %u at %s\n",
                  DEFAULT_AP_SSID, (unsigned)channel, apIP.toString().c_str());
}

void WebPortal::stopAccessPoint() {
    if (!_apActive) return;
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    _apActive = false;
    Serial.println("[WebPortal] SoftAP stopped (station link active)");
}

void WebPortal::process() {
    // Deferred Wi-Fi reconfiguration from /api/wifi: let the HTTP response flush
    // before the radio changes underneath the client.
    if (_pendingStaConnect && (millis() - _pendingStaConnectMs) >= 400) {
        _pendingStaConnect = false;
        startStationAttempt(_apActive);
    }

    switch (_wifiPhase) {
        case WifiPhase::StaConnecting:
            if (WiFi.status() == WL_CONNECTED) {
                if (_apActive) stopAccessPoint();
                _wifiPhase = WifiPhase::StaConnected;
                _staDownSinceMs = 0;
                startMdns();
                Serial.printf("[WebPortal] Station connected, IP: %s\n",
                              WiFi.localIP().toString().c_str());
            } else if (millis() - _staAttemptStartMs >= STA_CONNECT_TIMEOUT_MS) {
                WiFi.disconnect(true, false);  // stop retrying (and channel hopping)
                if (_apActive) {
                    _wifiPhase = WifiPhase::ApOnly;
                    Serial.println("[WebPortal] Station join failed; staying on SoftAP");
                } else {
                    Serial.println("[WebPortal] Station join timed out; starting fallback SoftAP");
                    requestAccessPoint();
                }
            }
            break;

        case WifiPhase::StaConnected:
            if (WiFi.status() != WL_CONNECTED) {
                if (_staDownSinceMs == 0) {
                    _staDownSinceMs = millis();
                } else if (millis() - _staDownSinceMs >= STA_LOST_GRACE_MS) {
                    _staDownSinceMs = 0;
                    Serial.println("[WebPortal] Station link lost; starting fallback SoftAP");
                    requestAccessPoint();
                }
            } else {
                _staDownSinceMs = 0;
            }
            break;

        case WifiPhase::ApScanning:
            if (WiFi.scanComplete() >= 0 || (millis() - _scanStartMs) >= AP_SCAN_TIMEOUT_MS) {
                startAccessPoint(pickBestApChannel());
            }
            break;

        case WifiPhase::ApOnly:
            break;
    }

    // Captive portal DNS only exists while we are the access point.
    if (_apActive) {
        _dnsServer.processNextRequest();
    }

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
    _server.on("/api/serial-trace", HTTP_GET, [this]() { handleSerialTrace(); });
    _server.on("/api/debug", HTTP_GET, [this]() { handleDebugConfig(); });
    _server.on("/api/debug", HTTP_POST, [this]() { handleDebugConfig(); });

    // OTA firmware upload
    _server.on("/update", HTTP_POST, [this]() { handleOtaComplete(); }, [this]() { handleOtaUpload(); });

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
    BbsFwConfig cfg;
    bool ok = Bridge.readConfig(cfg, 3500);

    if (!ok) {
        // Never fabricate a config for the UI: if the controller did not answer,
        // report the failure and leave the form fields empty.
        _server.send(503, "application/json",
                     "{\"success\":false,\"fromController\":false,"
                     "\"error\":\"Controller did not respond\"}");
        return;
    }

    JsonDocument doc;
    serializeConfigToJson(cfg, doc);
    doc["fromController"] = true;

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

    // The controller's detected version is authoritative. Refuse a payload built
    // for a different layout (e.g. a v6 JSON profile imported while connected to a
    // v5 controller) rather than zeroing the mismatched fields.
    uint8_t ver = Bridge.getConfigVersion();
    if (ver != BBS_FW_CONFIG_VERSION_6 && ver != BBS_FW_CONFIG_VERSION && ver != BBS_FW_CONFIG_VERSION_4) {
        ver = (uint8_t)(doc["configVersion"] | BBS_FW_CONFIG_VERSION);
    }
    if (ver != BBS_FW_CONFIG_VERSION_6 && ver != BBS_FW_CONFIG_VERSION && ver != BBS_FW_CONFIG_VERSION_4) {
        ver = BBS_FW_CONFIG_VERSION;
    }

    uint8_t payloadVer = doc["configVersion"] | 0;
    if (payloadVer != 0 && payloadVer != ver) {
        _server.send(409, "application/json",
                     "{\"success\":false,\"error\":\"Config version mismatch: payload is v"
                     + String(payloadVer) + " but the controller is v" + String(ver)
                     + ". Read from the controller and retry.\"}");
        return;
    }

    BbsFwConfig cfg;
    cfg.version = ver;
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

        // Defer the radio change until process() runs so this response flushes
        // first (the SoftAP is kept up until the new link actually comes up).
        _pendingStaConnect = true;
        _pendingStaConnectMs = millis();
        _server.send(200, "application/json", "{\"success\":true}");
    } else {
        _server.send(400, "application/json", "{\"error\":\"Invalid SSID\"}");
    }
}

void WebPortal::handleInfo() {
    JsonDocument doc;
    doc["model"] = "ESP32-S3 (44-Pin DevKitC-1)";
    doc["fwVersion"] = FW_VERSION;
    doc["fwBuild"] = __DATE__ " " __TIME__;
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["flashSize"] = ESP.getFlashChipSize();
    doc["uptimeSec"] = millis() / 1000;
    doc["mdnsHost"] = String(MDNS_HOSTNAME) + ".local";

    bool staUp = (WiFi.status() == WL_CONNECTED);
    if (_apActive) {
        doc["wifiMode"] = staUp ? "AP+STA" : "AP";
    } else if (staUp) {
        doc["wifiMode"] = "STA";
    } else {
        doc["wifiMode"] = "STA_CONNECTING";
    }
    doc["staIP"] = staUp ? WiFi.localIP().toString() : "Not Connected";
    doc["apIP"] = _apActive ? WiFi.softAPIP().toString() : "Off";

    String resp;
    serializeJson(doc, resp);
    _server.send(200, "application/json", resp);
}

void WebPortal::handleSerialTrace() {
    // Tracing is off by default; return an empty payload without touching the
    // ring buffer so an idle client cannot rack up JSON-building work.
    if (!Debug.isEnabled()) {
        _server.send(200, "application/json",
                     "{\"enabled\":false,\"seq\":0,\"dropped\":0,\"bytes\":[],\"texts\":[]}");
        return;
    }

    // Optional ?after=<seq> query param for incremental polling
    uint32_t afterSeq = 0;
    if (_server.hasArg("after")) {
        afterSeq = (uint32_t)_server.arg("after").toInt();
    }

    JsonDocument doc;
    Debug.buildTraceJson(doc, afterSeq);
    doc["enabled"] = true;

    String jsonStr;
    serializeJson(doc, jsonStr);
    _server.send(200, "application/json", jsonStr);
}

void WebPortal::handleDebugConfig() {
    if (_server.method() == HTTP_POST) {
        if (!_server.hasArg("plain")) {
            _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, _server.arg("plain"))) {
            _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        bool enabled = doc["enabled"] | false;
        if (enabled && !Debug.isEnabled()) {
            Debug.clear();  // start each debug session with a clean buffer
        }
        Debug.setEnabled(enabled);
    }

    JsonDocument resp;
    resp["enabled"] = Debug.isEnabled();
    String jsonResp;
    serializeJson(resp, jsonResp);
    _server.send(200, "application/json", jsonResp);
}

void WebPortal::handleOtaUpload() {
    // XHR `send(file)` posts the raw binary as the request body, so the WebServer
    // dispatches it through the "raw" handler and populates _currentRaw (NOT
    // _currentUpload — that stays null and dereferencing it crashes).
    HTTPRaw& raw = _server.raw();

    if (raw.status == RAW_START) {
        // raw.totalSize is 0 at this point; use the Content-Length header instead.
        size_t fwSize = (size_t)_server.clientContentLength();
        if (fwSize == 0) fwSize = UPDATE_SIZE_UNKNOWN;
        Debug.tracef("OTA begin: %d bytes", fwSize);
        if (!Update.begin(fwSize, U_FLASH)) {
            Debug.tracef("OTA begin failed: %s", Update.errorString());
        }
    } else if (raw.status == RAW_WRITE) {
        if (Update.write(raw.buf, raw.currentSize) != raw.currentSize) {
            Debug.tracef("OTA write failed: %s", Update.errorString());
        }
    } else if (raw.status == RAW_END) {
        if (Update.end(true)) {
            Debug.trace("OTA image received, validating...");
        } else {
            Debug.tracef("OTA end failed: %s", Update.errorString());
        }
    }
}

void WebPortal::handleOtaComplete() {
    if (Update.hasError()) {
        _server.sendHeader("Connection", "close");
        _server.send(500, "text/plain", Update.errorString());
        Debug.tracef("OTA failed: %s", Update.errorString());
    } else {
        _server.sendHeader("Connection", "close");
        _server.send(200, "text/plain", "OK");
        Debug.trace("OTA success, rebooting in 100ms");
        delay(100);
        ESP.restart();
    }
}

void WebPortal::handleNotFound() {
    // Captive portal redirect only makes sense while we are the access point.
    // In station mode we return a real 404 so API/JS typos stay debuggable.
    if (_apActive) {
        String apIP = WiFi.softAPIP().toString();
        if (_server.hostHeader() != apIP) {
            _server.sendHeader("Location", String("http://") + apIP + "/", true);
            _server.send(302, "text/plain", "");
            return;
        }
    }

    _server.send(404, "text/plain", "Not Found");
}
