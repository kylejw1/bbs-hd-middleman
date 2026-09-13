#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"
#include "BbsFwProtocol.h"

struct EventLogItem {
    uint32_t timestampMs;
    uint8_t id;
    int16_t data;
    bool hasData;
    const char* level; // "info", "warning", "error"
    String message;
};

class TelemetryTracker {
public:
    TelemetryTracker();

    // Reset / init
    void begin();

    // Metric updates (called from SerialBridge parser)
    void updateSpeedRpm(uint16_t rpm);
    void updateCurrent(float amps);
    void updateBatteryPercent(uint8_t percent);
    void updateBatteryVoltage(float volts);
    void updateAssistLevel(uint8_t level);
    void updateOperationMode(uint8_t mode);
    void updateLights(bool on);
    void updateStatusCode(uint8_t code);
    void updateTemperature(int8_t controllerC, int8_t motorC);
    void updateTargetTelemetry(uint8_t targetCurrentPercent, uint8_t targetSpeedPercent, uint16_t cadenceRpmX10);
    void updateFirmwareInfo(uint8_t major, uint8_t minor, uint8_t patch, uint8_t cfgVer, ControllerType type);

    // Activity tracking
    void recordDisplayRx(size_t bytes);
    void recordDisplayTx(size_t bytes);
    void recordControllerRx(size_t bytes);
    void recordControllerTx(size_t bytes);
    void recordIntercept();
    void recordForward();

    // Event Log
    void addEvent(uint8_t eventId, int16_t data, bool hasData);

    // Getters for display mock synthesizer
    uint8_t getCachedStatusCode() const;
    uint8_t getCachedBatteryPercent() const;
    uint16_t getCachedSpeedRpm() const;
    float getCachedCurrentAmps() const;
    float getCachedVoltage() const;
    uint8_t getCachedAssistLevel() const;
    bool getCachedLights() const;
    uint8_t getCachedOperationMode() const;

    // Check connectivity
    bool isDisplayActive() const;
    bool isControllerActive() const;

    // JSON builders for Web API
    void buildTelemetryJson(JsonDocument& doc);
    void buildEventsJson(JsonDocument& doc);

private:
    mutable SemaphoreHandle_t _mutex;

    // Live metrics
    uint16_t _speedRpm;
    float _speedKph;
    float _speedMph;
    uint8_t _batteryPercent;
    float _batteryVoltage;
    float _motorCurrentAmps;
    float _motorPowerWatts;
    uint8_t _assistLevel;
    uint8_t _operationMode;
    bool _lightsOn;
    uint8_t _statusCode;
    int8_t _controllerTempC;
    int8_t _motorTempC;
    float _wheelSizeInch;

    // Live bbs-fw motor targets (from the 0xEC debug telemetry frame)
    bool _hasTargetTelemetry;
    uint8_t _targetCurrentPercent;
    uint8_t _targetSpeedPercent;
    uint16_t _cadenceRpmX10;

    // Controller details
    uint8_t _fwMajor;
    uint8_t _fwMinor;
    uint8_t _fwPatch;
    uint8_t _configVersion;
    ControllerType _controllerType;

    // Timing
    uint32_t _lastDisplayRxMs;
    uint32_t _lastControllerRxMs;

    // Statistics
    uint32_t _displayRxBytes;
    uint32_t _displayTxBytes;
    uint32_t _controllerRxBytes;
    uint32_t _controllerTxBytes;
    uint32_t _forwardedPackets;
    uint32_t _interceptedPackets;

    // Event Log Ring Buffer
    EventLogItem _events[MAX_EVENT_LOG_ENTRIES];
    size_t _eventHead;
    size_t _eventCount;

    void recalculateSpeed();
};

extern TelemetryTracker Telemetry;

#endif // TELEMETRY_H
