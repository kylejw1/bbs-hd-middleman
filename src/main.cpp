#include <Arduino.h>
#include "Config.h"
#include "BbsFwProtocol.h"
#include "Telemetry.h"
#include "SerialBridge.h"
#include "WebPortal.h"

uint32_t lastLedBlinkMs = 0;
bool ledState = false;

void setup() {
    // Initialize USB Debug Serial
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(1000);

    Serial.println("\n=======================================================");
    Serial.println("  BBS-HD / BBS-FW ESP32-S3 Serial Middleman & Bridge   ");
    Serial.println("=======================================================");

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
    if (Bridge.readFirmwareInfo(maj, min, pat, cfgVer, cType, 1000)) {
        Serial.printf("[System] Connected to %s (Firmware %d.%d.%d, Config v%d)\n",
                      getControllerTypeName(cType), maj, min, pat, cfgVer);
        // Enable controller event logging
        Bridge.enableEventLog(true, 1000);
    } else {
        Serial.println("[System] Controller not responding yet (will auto-connect when powered on)");
    }

    Serial.println("[System] Initialization complete! System is running.\n");
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
