#include <Arduino.h>
#include "Config.h"
#include "BbsFwProtocol.h"
#include "Telemetry.h"
#include "SerialBridge.h"
#include "WebPortal.h"
#include "DebugLog.h"
#include <esp_log.h>
#include <esp_ota_ops.h>

uint32_t lastLedBlinkMs = 0;
bool ledState = false;

// Capture ESP-IDF log output (log_e, log_w, log_i, log_d) so the web debug
// console can show errors like the [E][WebServer.cpp:638] "request handler not
// found" message that appears in the USB serial monitor but not the web UI.
static vprintf_like_t s_origEspLogVprintf = nullptr;

static int debugEspLogVprintf(const char* fmt, va_list args) {
    char buf[TRACE_TEXT_MAX];
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);

    // vsnprintf returns the *would-be* length, which may exceed the buffer.
    if (len < 0) {
        return 0;
    }
    if (len > (int)sizeof(buf) - 1) {
        len = (int)sizeof(buf) - 1;
    }

    // Strip trailing newline / CR
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    if (len > 0) {
        Debug.trace(buf);
    }

    // Forward to the original handler so the USB serial monitor still works
    if (s_origEspLogVprintf) {
        return s_origEspLogVprintf(fmt, args);
    }
    return 0;
}

void setup() {
    // Initialize USB Debug Serial
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(1000);

    Serial.println("\n=======================================================");
    Serial.println("  BBS-HD / BBS-FW ESP32-S3 Serial Middleman & Bridge   ");
    Serial.println("=======================================================");

    // Initialize debug trace ring buffer (for web debug console)
    Debug.begin();

    // Hook ESP-IDF log output so the web debug console captures
    // errors like "[E][WebServer.cpp:638] _handleRequest(): request handler not found"
    s_origEspLogVprintf = esp_log_set_vprintf(debugEspLogVprintf);

    Debug.trace("System boot starting");
    Debug.tracef("Free heap: %d bytes", ESP.getFreeHeap());

    #ifdef STATUS_LED_PIN
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    #endif

    // Initialize telemetry subsystem
    Telemetry.begin();

    // Initialize dual UART serial bridge (Controller & Display)
    Bridge.begin();

    // Initialize Wi-Fi Access Point & Web Portal
    Portal.begin();

    // Attempt initial query of controller firmware version
    uint8_t maj = 0, min = 0, pat = 0, cfgVer = 0;
    ControllerType cType = ControllerType::Unknown;
    Serial.println("[System] Querying controller firmware version...");
    Debug.trace("Querying controller firmware version...");
    if (Bridge.readFirmwareInfo(maj, min, pat, cfgVer, cType, 1000)) {
        Serial.printf("[System] Connected to %s (Firmware %d.%d.%d, Config v%d)\n",
                      getControllerTypeName(cType), maj, min, pat, cfgVer);
        Debug.tracef("Connected to %s v%d.%d.%d (cfg v%d)", getControllerTypeName(cType), maj, min, pat, cfgVer);
        // Enable controller event logging
        Bridge.enableEventLog(true, 1000);
    } else {
        Serial.println("[System] Controller not responding yet (will auto-connect when powered on)");
        Debug.trace("Controller not responding at boot (will auto-connect when powered)");
    }

    Serial.println("[System] Initialization complete! System is running.\n");
    Debug.trace("Boot complete");

    // Mark this OTA slot as valid so the bootloader won't roll back to the
    // previous firmware after a clean boot. If a bad OTA crashes before this
    // point, the bootloader rolls back to the known-good slot automatically.
    esp_ota_mark_app_valid_cancel_rollback();
}

void loop() {
    // 1. Process dual serial bridge (transparent pass-through & keep-alive synthesizer)
    Bridge.process();

    // 2. Process web server and DNS captive portal
    Portal.process();

    // 3. Heartbeat LED animation
    uint32_t now = millis();
    uint32_t blinkInterval = 1000; // Normal idle: 1s

    if (Bridge.isInterceptActive()) {
        blinkInterval = 100; // Fast blink during active config reading/flashing
    } else if (Telemetry.isControllerActive() && Telemetry.isDisplayActive()) {
        blinkInterval = 500; // Medium blink when fully bridged & active
    }

    if (now - lastLedBlinkMs >= blinkInterval) {
        lastLedBlinkMs = now;
        ledState = !ledState;
        #ifdef STATUS_LED_PIN
        digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
        #endif
    }
}
