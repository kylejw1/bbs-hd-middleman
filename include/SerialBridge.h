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

    // Auto-assist override. The display re-asserts its own PAS level on every
    // poll cycle, so a one-shot injection loses within about a second. While an
    // override is active the middleman rewrites the display's PAS write to this
    // level before forwarding it, which makes the middleman the authority.
    void setPasOverride(uint8_t level);
    void clearPasOverride();
    bool hasPasOverride() const { return _pasOverrideActive; }
    uint8_t getPasOverrideLevel() const { return _pasOverrideLevel; }
    void injectLights(bool on);
    void injectOperationMode(uint8_t mode);


    // State getters
    BridgeState getState() const;
    bool isInterceptActive() const;
    uint8_t getConfigVersion() const;

    // Cached config accessors (for LcdDisplay PAS curve rendering).
    // Return sensible defaults when no config has been read yet.
    uint8_t getConfigMaxCurrentAmps() const;
    uint8_t getConfigPasKeepCurrentPct() const;
    uint8_t getConfigPasKeepCurrentRpm() const;
    // mode=0 → standard_levels, mode=1 → sport_levels; idx 0..9
    uint8_t getLevelCurrentPct(uint8_t mode, uint8_t idx) const;
    uint8_t getLevelCadencePct(uint8_t mode, uint8_t idx) const;
    uint8_t getLevelFlags(uint8_t mode, uint8_t idx) const;

private:
    HardwareSerial _controllerSerial;
    HardwareSerial _displaySerial;

    BridgeState _state;
    SemaphoreHandle_t _bridgeMutex;

    // Display RX parsing buffer
    uint8_t _displayBuf[32];
    size_t  _displayBufLen;
    uint32_t _lastDisplayByteMs;

    bool     _pasOverrideActive = false;
    uint8_t  _pasOverrideLevel = 0;
    bool     _pasOverridePending = false;
    uint32_t _pasOverrideLastMs = 0;
    uint8_t  _writeBuf[5] = {0};
    size_t   _writeLen = 0;
    size_t   _writeExpectedLen = 0;
    bool     _holdWrite = false;

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

    // Last successfully read/written config — cached for the LCD display.
    // Protected by _bridgeMutex for writes; read-only accessors are called
    // from loop() so they take the mutex briefly.
    BbsFwConfig _cachedConfig;
    bool        _hasCachedConfig;

    // Internal processing
    void processDisplayRxPassThrough();
    void processControllerRxPassThrough();
    void processDisplayRxIntercept();


    // Protocol helper handlers
    void handleDisplayPacket(const uint8_t* buf, size_t len);

    // Write frames are held until complete so their payload can be rewritten.
    void forwardDisplayByte(uint8_t b);
    void flushHeldWrite();
    void processPasOverride();
    void handleControllerPacket(const uint8_t* buf, size_t len);
    void synthesizeDisplayResponse(uint8_t opcode);

    // Synchronous controller query helpers
    bool sendAndReceiveController(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t expectedLen, uint32_t timeoutMs);
    bool receiveController(uint8_t* buf, size_t len, uint32_t timeoutMs);
    bool consumeControllerEventFrame(uint8_t firstByte, uint32_t deadlineMs);
    bool waitControllerIdle(uint32_t quietMs, uint32_t timeoutMs);
    void flushQueuedDisplayWrites();
};

extern SerialBridge Bridge;

#endif // SERIAL_BRIDGE_H
