#include "WebPortal.h"
#include <Update.h>
#include <ESPmDNS.h>
#include <string.h>

WebPortal Portal;

namespace {

// Wi-Fi modem sleep is MANDATORY while the Bluetooth controller is enabled.
//
// Requesting WIFI_PS_NONE with Bluetooth up makes the Wi-Fi driver abort inside
// pm_set_sleep_type(), with the log message:
//
//   E wifi: Error! Should enable WiFi modem sleep when both WiFi and Bluetooth
//           are enabled!!!!!!
//
// so `WiFi.setSleep(false)` is only safe in a BLE-free build. Both this abort
// and the coex_enable() one it replaced present as "the middleman is dead and
// the display is erroring", because both happen before setup() reaches loop().
// The cost of WIFI_PS_MIN_MODEM is a little added dashboard latency.
void applyWifiPowerSave() {
#if BLE_TRANSPORT_ENABLED
    (void)WiFi.setSleep(true);   // WIFI_PS_MIN_MODEM: required for coexistence
#else
    (void)WiFi.setSleep(false);  // WIFI_PS_NONE: lowest latency without BLE
#endif
}

} // namespace

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
    _blePin = _prefs.getString("ble_pin", "");
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
    applyWifiPowerSave();
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
    applyWifiPowerSave();
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
    applyWifiPowerSave();
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
    applyWifiPowerSave();
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

    // REST APIs. Registered straight from API_ROUTES -- the same table
    // dispatchApi() switches on -- so the HTTP and BLE transports can never end
    // up exposing different route sets.
    for (size_t i = 0; i < API_ROUTE_COUNT; ++i) {
        const ApiRoute& route = API_ROUTES[i];
        _server.on(route.path, route.post ? HTTP_POST : HTTP_GET, [this, &route]() {
            ApiRequest req;
            req.path = route.path;
            req.post = route.post;
            serveApi(req);
        });
    }

    // OTA firmware upload
    _server.on("/update", HTTP_POST, [this]() { handleOtaComplete(); }, [this]() { handleOtaUpload(); });

    // Captive Portal Redirects for iOS, Android, Windows
    _server.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/fwlink", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/canonical.html", HTTP_GET, [this]() { handleRoot(); });

    _server.onNotFound([this]() { handleNotFound(); });
}

void WebPortal::serveApi(const ApiRequest& request) {
    ApiRequest req = request;

    // WebServer owns the request body, so keep a copy alive for the duration of
    // the dispatch and hand the handler a pointer to it.
    String body;
    if (req.post && _server.hasArg("plain")) {
        body = _server.arg("plain");
        req.body = &body;
    }
    if (_server.hasArg("after")) {
        req.afterSeq = (uint32_t)_server.arg("after").toInt();
    }

    ApiResponse res;
    if (!dispatchApi(req, res)) {
        _server.send(404, "application/json", "{\"error\":\"Not found\"}");
        return;
    }

    _server.send(res.status, res.html ? "text/html" : "application/json", res.body);
}

void WebPortal::sendApi(int status, const char* contentType, const String& body) {
    if (_apiOut) {
        _apiOut->status = status;
        _apiOut->body = body;
        _apiOut->html = (contentType != nullptr && strcmp(contentType, "text/html") == 0);
        return;
    }

    _server.send(status, contentType, body);
}

bool WebPortal::dispatchApi(const ApiRequest& req, ApiResponse& out) {
    const ApiRoute* route = findApiRoute(req.path, req.post);
    if (route == nullptr) {
        return false;
    }

    // Handlers reply through sendApi(), which redirects into `captured` rather
    // than the HTTP socket. Save and restore the sink so a nested dispatch cannot
    // corrupt the outer one and a capture can never outlive this call.
    //
    // Call this from loop() only -- never from a Bluetooth callback. The handlers
    // drive the 1200-baud bridge (a config write holds CONFIG_INTERCEPT for over
    // a second) and the sink fields are not thread-safe.
    ApiResponse captured;
    ApiResponse* const previousOut = _apiOut;
    const String* const previousBody = _apiBody;
    const bool previousPost = _apiIsPost;
    const uint32_t previousAfter = _apiAfterSeq;

    _apiOut = &captured;
    _apiBody = req.body;
    _apiIsPost = req.post;
    _apiAfterSeq = req.afterSeq;

    switch (route->id) {
        case ApiRouteId::Telemetry:        handleTelemetry();        break;
        case ApiRouteId::Events:           handleEvents();           break;
        case ApiRouteId::GetConfig:        handleGetConfig();        break;
        case ApiRouteId::PostConfig:       handlePostConfig();       break;
        case ApiRouteId::ResetConfig:      handleResetConfig();      break;
        case ApiRouteId::CalibrateVoltage: handleCalibrateVoltage(); break;
        case ApiRouteId::CmdPas:           handleCmdPas();           break;
        case ApiRouteId::CmdMode:          handleCmdMode();          break;
        case ApiRouteId::CmdLights:        handleCmdLights();        break;
        case ApiRouteId::WifiConfig:       handleWifiConfig();       break;
        case ApiRouteId::Info:             handleInfo();             break;
        case ApiRouteId::SerialTrace:      handleSerialTrace();      break;
        case ApiRouteId::DebugConfig:      handleDebugConfig();      break;
        case ApiRouteId::BlePin:           handleBlePin();           break;
    }

    _apiOut = previousOut;
    _apiBody = previousBody;
    _apiIsPost = previousPost;
    _apiAfterSeq = previousAfter;

    out = captured;
    return true;
}

String WebPortal::getBlePin() const {
    return _blePin;
}

void WebPortal::setBlePin(const String& pin) {
    _blePin = pin;
    _prefs.putString("ble_pin", pin);
}

void WebPortal::handleBlePin() {
    // GET only reports whether a PIN is set; it never reveals the PIN itself.
    if (!_apiIsPost) {
        JsonDocument doc;
        doc["pinSet"] = (_blePin.length() > 0);
#if BLE_TRANSPORT_ENABLED
        doc["bleEnabled"] = true;
#else
        doc["bleEnabled"] = false;
#endif
        String out;
        serializeJson(doc, out);
        sendApi(200, "application/json", out);
        return;
    }

    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, *_apiBody)) {
        sendApi(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String pin = doc["pin"] | "";
    pin.trim();

    // Empty clears the PIN, which re-opens the BLE API. Anything else must be
    // long enough to be worth something: a 1-2 character PIN would be trivial to
    // guess at BLE range.
    if (pin.length() > 0 && (pin.length() < 4 || pin.length() > 16)) {
        sendApi(400, "application/json",
                "{\"error\":\"PIN must be 4-16 characters, or empty to remove\"}");
        return;
    }

    setBlePin(pin);

    JsonDocument resp;
    resp["success"] = true;
    resp["pinSet"] = (pin.length() > 0);
    String out;
    serializeJson(resp, out);
    sendApi(200, "application/json", out);
}

void WebPortal::handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WebPortal::handleTelemetry() {
    JsonDocument doc;
    Telemetry.buildTelemetryJson(doc);

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendApi(200, "application/json", jsonStr);
}

void WebPortal::handleEvents() {
    JsonDocument doc;
    Telemetry.buildEventsJson(doc);

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendApi(200, "application/json", jsonStr);
}

void WebPortal::handleGetConfig() {
    BbsFwConfig cfg;
    bool ok = Bridge.readConfig(cfg, 3500);

    if (!ok) {
        // Never fabricate a config for the UI: if the controller did not answer,
        // report the failure and leave the form fields empty.
        sendApi(503, "application/json",
                     "{\"success\":false,\"fromController\":false,"
                     "\"error\":\"Controller did not respond\"}");
        return;
    }

    JsonDocument doc;
    serializeConfigToJson(cfg, doc);
    doc["fromController"] = true;

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendApi(200, "application/json", jsonStr);
}

void WebPortal::handlePostConfig() {
    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    const String& body = *_apiBody;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendApi(400, "application/json", "{\"error\":\"Invalid JSON\"}");
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
        sendApi(409, "application/json",
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
    sendApi(ok ? 200 : 500, "application/json", jsonResp);
}

void WebPortal::handleResetConfig() {
    bool ok = Bridge.resetConfig(3000);

    JsonDocument resp;
    resp["success"] = ok;
    String jsonResp;
    serializeJson(resp, jsonResp);
    sendApi(ok ? 200 : 500, "application/json", jsonResp);
}

void WebPortal::handleCalibrateVoltage() {
    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, *_apiBody);

    float volts = doc["voltage"] | 0.0f;
    if (volts < 20.0f || volts > 70.0f) {
        sendApi(400, "application/json", "{\"error\":\"Voltage out of range\"}");
        return;
    }

    bool ok = Bridge.calibrateVoltage(volts, 2500);

    JsonDocument resp;
    resp["success"] = ok;
    String jsonResp;
    serializeJson(resp, jsonResp);
    sendApi(ok ? 200 : 500, "application/json", jsonResp);
}

void WebPortal::handleCmdPas() {
    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, *_apiBody);
    uint8_t lvl = doc["level"] | 1;

    Bridge.injectPasLevel(lvl);
    sendApi(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleCmdMode() {
    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, *_apiBody);
    uint8_t mode = doc["mode"] | 0;

    Bridge.injectOperationMode(mode);
    sendApi(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleCmdLights() {
    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, *_apiBody);
    bool on = doc["on"] | false;

    Bridge.injectLights(on);
    sendApi(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleWifiConfig() {
    if (!_apiBody) {
        sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, *_apiBody);

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
        sendApi(200, "application/json", "{\"success\":true}");
    } else {
        sendApi(400, "application/json", "{\"error\":\"Invalid SSID\"}");
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
    sendApi(200, "application/json", resp);
}

void WebPortal::handleSerialTrace() {
    // Tracing is off by default; return an empty payload without touching the
    // ring buffer so an idle client cannot rack up JSON-building work.
    if (!Debug.isEnabled()) {
        sendApi(200, "application/json",
                     "{\"enabled\":false,\"seq\":0,\"dropped\":0,\"bytes\":[],\"texts\":[]}");
        return;
    }

    // Optional ?after=<seq> cursor for incremental polling. The HTTP glue fills
    // this in from the query string; the BLE transport parses its own.
    uint32_t afterSeq = _apiAfterSeq;

    JsonDocument doc;
    Debug.buildTraceJson(doc, afterSeq);
    doc["enabled"] = true;

    String jsonStr;
    serializeJson(doc, jsonStr);
    sendApi(200, "application/json", jsonStr);
}

void WebPortal::handleDebugConfig() {
    if (_apiIsPost) {
        if (!_apiBody) {
            sendApi(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, *_apiBody)) {
            sendApi(400, "application/json", "{\"error\":\"Invalid JSON\"}");
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
    sendApi(200, "application/json", jsonResp);
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
        sendApi(500, "text/plain", Update.errorString());
        Debug.tracef("OTA failed: %s", Update.errorString());
    } else {
        _server.sendHeader("Connection", "close");
        sendApi(200, "text/plain", "OK");
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
            sendApi(302, "text/plain", "");
            return;
        }
    }

    sendApi(404, "text/plain", "Not Found");
}
