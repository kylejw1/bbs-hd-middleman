#include "Telemetry.h"

TelemetryTracker Telemetry;

TelemetryTracker::TelemetryTracker()
    : _mutex(nullptr)
    , _speedRpm(0)
    , _speedKph(0.0f)
    , _speedMph(0.0f)
    , _batteryPercent(100)
    , _batteryVoltage(52.0f)
    , _motorCurrentAmps(0.0f)
    , _motorPowerWatts(0.0f)
    , _assistLevel(1)
    , _operationMode(0)
    , _lightsOn(false)
    , _statusCode(0)
    , _controllerTempC(25)
    , _motorTempC(25)
    , _wheelSizeInch(27.5f)
    , _fwMajor(0)
    , _fwMinor(0)
    , _fwPatch(0)
    , _configVersion(5)
    , _controllerType(ControllerType::BBSHD)
    , _lastDisplayRxMs(0)
    , _lastControllerRxMs(0)
    , _displayRxBytes(0)
    , _displayTxBytes(0)
    , _controllerRxBytes(0)
    , _controllerTxBytes(0)
    , _forwardedPackets(0)
    , _interceptedPackets(0)
    , _eventHead(0)
    , _eventCount(0)
{
}

void TelemetryTracker::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
}

void TelemetryTracker::recalculateSpeed() {
    // RPM to km/h: speed = (rpm * 60 * wheel_diameter_inches * 0.0254 * PI) / 1000
    // simplified: speed = rpm * wheel_diameter_inches * 0.00478778f
    _speedKph = _speedRpm * _wheelSizeInch * 0.00478778f;
    _speedMph = _speedKph * 0.621371f;
}

void TelemetryTracker::updateSpeedRpm(uint16_t rpm) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _speedRpm = rpm;
        recalculateSpeed();
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateCurrent(float amps) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _motorCurrentAmps = amps;
        _motorPowerWatts = _motorCurrentAmps * _batteryVoltage;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateBatteryPercent(uint8_t percent) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _batteryPercent = percent;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateBatteryVoltage(float volts) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _batteryVoltage = volts;
        _motorPowerWatts = _motorCurrentAmps * _batteryVoltage;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateAssistLevel(uint8_t level) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _assistLevel = level;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateOperationMode(uint8_t mode) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _operationMode = mode;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateLights(bool on) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _lightsOn = on;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateStatusCode(uint8_t code) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _statusCode = code;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateTemperature(int8_t controllerC, int8_t motorC) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _controllerTempC = controllerC;
        _motorTempC = motorC;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateFirmwareInfo(uint8_t major, uint8_t minor, uint8_t patch, uint8_t cfgVer, ControllerType type) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _fwMajor = major;
        _fwMinor = minor;
        _fwPatch = patch;
        _configVersion = cfgVer;
        _controllerType = type;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::recordDisplayRx(size_t bytes) {
    _lastDisplayRxMs = millis();
    _displayRxBytes += bytes;
}

void TelemetryTracker::recordDisplayTx(size_t bytes) {
    _displayTxBytes += bytes;
}

void TelemetryTracker::recordControllerRx(size_t bytes) {
    _lastControllerRxMs = millis();
    _controllerRxBytes += bytes;
}

void TelemetryTracker::recordControllerTx(size_t bytes) {
    _controllerTxBytes += bytes;
}

void TelemetryTracker::recordIntercept() {
    _interceptedPackets++;
}

void TelemetryTracker::recordForward() {
    _forwardedPackets++;
}

void TelemetryTracker::addEvent(uint8_t eventId, int16_t data, bool hasData) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        EventLogItem& item = _events[_eventHead];
        item.timestampMs = millis();
        item.id = eventId;
        item.data = data;
        item.hasData = hasData;

        if (eventId >= 64 && eventId < 128) {
            item.level = "error";
        } else if (eventId == 3 || eventId == 79 || eventId == 136 || eventId == 142) {
            item.level = "warning";
        } else {
            item.level = "info";
        }

        item.message = getEventDescription(eventId, data, hasData);

        _eventHead = (_eventHead + 1) % MAX_EVENT_LOG_ENTRIES;
        if (_eventCount < MAX_EVENT_LOG_ENTRIES) {
            _eventCount++;
        }
        xSemaphoreGive(_mutex);
    }
}

uint8_t TelemetryTracker::getCachedStatusCode() const {
    return _statusCode;
}

uint8_t TelemetryTracker::getCachedBatteryPercent() const {
    return _batteryPercent;
}

uint16_t TelemetryTracker::getCachedSpeedRpm() const {
    return _speedRpm;
}

float TelemetryTracker::getCachedCurrentAmps() const {
    return _motorCurrentAmps;
}

float TelemetryTracker::getCachedVoltage() const {
    return _batteryVoltage;
}

uint8_t TelemetryTracker::getCachedAssistLevel() const {
    return _assistLevel;
}

bool TelemetryTracker::getCachedLights() const {
    return _lightsOn;
}

uint8_t TelemetryTracker::getCachedOperationMode() const {
    return _operationMode;
}

bool TelemetryTracker::isDisplayActive() const {
    return (millis() - _lastDisplayRxMs) < 2500;
}

bool TelemetryTracker::isControllerActive() const {
    return (millis() - _lastControllerRxMs) < 2500;
}

void TelemetryTracker::buildTelemetryJson(JsonDocument& doc) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        doc["speedRpm"] = _speedRpm;
        doc["speedKph"] = serialized(String(_speedKph, 1));
        doc["speedMph"] = serialized(String(_speedMph, 1));
        doc["batteryPercent"] = _batteryPercent;
        doc["batteryVoltage"] = serialized(String(_batteryVoltage, 1));
        doc["currentAmps"] = serialized(String(_motorCurrentAmps, 1));
        doc["powerWatts"] = (int)_motorPowerWatts;
        doc["assistLevel"] = _assistLevel;
        doc["operationMode"] = _operationMode == 1 ? "Sport" : "Standard";
        doc["lights"] = _lightsOn;
        doc["statusCode"] = _statusCode;
        doc["controllerTempC"] = _controllerTempC;
        doc["motorTempC"] = _motorTempC;

        doc["displayConnected"] = isDisplayActive();
        doc["controllerConnected"] = isControllerActive();

        doc["fwVersion"] = String(_fwMajor) + "." + String(_fwMinor) + "." + String(_fwPatch);
        doc["configVersion"] = _configVersion;
        doc["controllerType"] = getControllerTypeName(_controllerType);

        doc["stats"]["displayRxBytes"] = _displayRxBytes;
        doc["stats"]["displayTxBytes"] = _displayTxBytes;
        doc["stats"]["controllerRxBytes"] = _controllerRxBytes;
        doc["stats"]["controllerTxBytes"] = _controllerTxBytes;
        doc["stats"]["forwarded"] = _forwardedPackets;
        doc["stats"]["intercepted"] = _interceptedPackets;
        doc["uptimeSeconds"] = millis() / 1000;

        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::buildEventsJson(JsonDocument& doc) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        JsonArray arr = doc.to<JsonArray>();

        // Return from oldest to newest
        size_t start = (_eventHead + MAX_EVENT_LOG_ENTRIES - _eventCount) % MAX_EVENT_LOG_ENTRIES;
        for (size_t i = 0; i < _eventCount; ++i) {
            size_t idx = (start + i) % MAX_EVENT_LOG_ENTRIES;
            const EventLogItem& item = _events[idx];

            JsonObject obj = arr.add<JsonObject>();
            obj["time"] = item.timestampMs;
            obj["id"] = item.id;
            obj["level"] = item.level;
            obj["msg"] = item.message;
            if (item.hasData) {
                obj["data"] = item.data;
            }
        }
        xSemaphoreGive(_mutex);
    }
}
