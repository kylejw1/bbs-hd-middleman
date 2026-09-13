#ifndef SERIAL_BRIDGE_H
#define SERIAL_BRIDGE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"
#include "BbsFwProtocol.h"
#include "Telemetry.h"

enum class BridgeState {
    PASS_THROUGH,       // Transparent forwarding between Display and Controller
    CONFIG_INTERCEPT    // Intercepted: Middleman talks to Controller; synthesizes Display responses
};

struct QueuedDisplayCmd {
    uint8_t len;
    uint8_t data[8];
};

class SerialBridge {
public:
    SerialBridge();

    // Initialization
    void begin();

    // Main polling loop (called frequently from loop() or FreeRTOS task)
    void process();

    // High-level controller operations (called from Web API)
    bool readFirmwareInfo(uint8_t& major, uint8_t& minor, uint8_t& patch, uint8_t& cfgVer, ControllerType& ctrlType, uint32_t timeoutMs = 2000);
    bool readConfig(BbsFwConfig& config, uint32_t timeoutMs = 3000);
    bool writeConfig(const BbsFwConfig& config, uint32_t timeoutMs = 3000);
    bool resetConfig(uint32_t timeoutMs = 2500);
    bool calibrateVoltage(float measuredVolts, uint32_t timeoutMs = 2500);
    bool enableEventLog(bool enable, uint32_t timeoutMs = 1500);

    // Direct command injections (can be triggered from Web UI)
    void injectPasLevel(uint8_t level);
    void injectLights(bool on);
    void injectOperationMode(uint8_t mode);

    // State getters
    BridgeState getState() const;
    bool isInterceptActive() const;
    uint8_t getConfigVersion() const;

private:
    HardwareSerial _controllerSerial;
    HardwareSerial _displaySerial;

    BridgeState _state;
    SemaphoreHandle_t _bridgeMutex;

    // Display RX parsing buffer
    uint8_t _displayBuf[32];
    size_t  _displayBufLen;
    uint32_t _lastDisplayByteMs;

    // Controller RX parsing buffer
    uint8_t _controllerBuf[256];
    size_t  _controllerBufLen;
    uint32_t _lastControllerByteMs;

    // Last query opcode received from display
    uint8_t _lastDisplayOpcode;

    // Detected controller config struct version (4 or 5); drives read/write framing
    uint8_t _configVersion;

    // Queued display write commands while in config intercept mode
    QueuedDisplayCmd _queuedWrites[MAX_QUEUED_DISPLAY_CMDS];
    size_t _queuedCount;

    // Internal processing
    void processDisplayRxPassThrough();
    void processControllerRxPassThrough();
    void processDisplayRxIntercept();

    // Protocol helper handlers
    void handleDisplayPacket(const uint8_t* buf, size_t len);
    void handleControllerPacket(const uint8_t* buf, size_t len);
    void synthesizeDisplayResponse(uint8_t opcode);

    // Synchronous controller query helpers
    bool sendAndReceiveController(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t expectedLen, uint32_t timeoutMs);
    bool receiveController(uint8_t* buf, size_t len, uint32_t timeoutMs);
    bool consumeControllerEventFrame(uint8_t firstByte, uint32_t deadlineMs);
    void flushQueuedDisplayWrites();
};

extern SerialBridge Bridge;

#endif // SERIAL_BRIDGE_H
