#include "Telemetry.h"

TelemetryTracker Telemetry;

TelemetryTracker::TelemetryTracker()
    : _mutex(nullptr)
    , _speedRpm(0)
    , _speedKph(0.0f)
    , _speedMph(0.0f)
    // Zero, not a plausible-looking constant. These are only ever written by the
    // display's queries, so with no display attached they would otherwise read as
    // a real 52.0 V / 100 % from a battery that was never measured.
    , _batteryPercent(0)
    , _batteryVoltage(0.0f)
    , _motorCurrentAmps(0.0f)
    , _motorPowerWatts(0.0f)
    , _assistLevel(0)
    , _operationMode(0)
    , _lightsOn(false)
    , _statusCode(0)
    , _controllerTempC(25)
    , _motorTempC(25)
    , _wheelSizeInch(27.5f)
    , _lastGoodSpeedKph(0.0f)
    , _baroPressureHpa(0.0f)
    , _baroTempC(0.0f)
    , _baroAltitudeM(0.0f)
    , _slopePercent(0.0f)
    , _lastBaroMs(0)
    , _lastVehicleDataMs(0)
    , _hasTargetTelemetry(false)
    , _lastTargetTelemetryMs(0)
    , _targetCurrentPercent(0)
    , _targetSpeedPercent(0)
    , _cadenceRpmX10(0)
    , _motorRpmX10(0)
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
    , _eventSeq(0)
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
    // A 27.5 inch wheel is 0.1317 km/h per rpm, so 800 rpm is already 105 km/h.
    // Anything beyond this is a mis-framed response (an event frame swallowed as
    // a speed reply), not a bicycle, and rendering it would put four digits in a
    // readout that has only ever meant two.
    if (_speedRpm > 800) {
        _speedKph = _lastGoodSpeedKph;
        _speedMph = _speedKph * 0.621371f;
        return;
    }
    _lastVehicleDataMs = millis();
    _lastGoodSpeedKph = _speedRpm * _wheelSizeInch * 0.00478778f;
    _speedKph = _lastGoodSpeedKph;
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
        _lastVehicleDataMs = millis();
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateBatteryVoltage(float volts) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _batteryVoltage = volts;
        _lastVehicleDataMs = millis();
        _motorPowerWatts = _motorCurrentAmps * _batteryVoltage;
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateBaro(float pressureHpa, float temperatureC, float altitudeM) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _baroPressureHpa = pressureHpa;
        _baroTempC = temperatureC;
        _baroAltitudeM = altitudeM;
        _lastBaroMs = millis();
        xSemaphoreGive(_mutex);
    }
}

void TelemetryTracker::updateSlopePercent(float percent) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _slopePercent = percent;
        xSemaphoreGive(_mutex);
    }
}

float TelemetryTracker::getCachedPressureHpa() const { return _baroPressureHpa; }
float TelemetryTracker::getCachedBaroTempC() const { return _baroTempC; }
float TelemetryTracker::getCachedBaroAltitudeM() const { return _baroAltitudeM; }
float TelemetryTracker::getCachedSlopePercent() const { return _slopePercent; }
bool  TelemetryTracker::hasBaro() const { return _lastBaroMs != 0 && (millis() - _lastBaroMs < 3000); }

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

void TelemetryTracker::updateTargetTelemetry(uint8_t targetCurrentPercent, uint8_t targetSpeedPercent,
                                             uint16_t cadenceRpmX10, uint16_t motorRpmX10) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _targetCurrentPercent = targetCurrentPercent;
        _targetSpeedPercent = targetSpeedPercent;
        _cadenceRpmX10 = cadenceRpmX10;
        _motorRpmX10 = motorRpmX10;
        _hasTargetTelemetry = true;
        _lastTargetTelemetryMs = millis();
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
        _eventSeq++;
        xSemaphoreGive(_mutex);
    }
}

uint32_t TelemetryTracker::getEventSeq() const {
    uint32_t seq = 0;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        seq = _eventSeq;
        xSemaphoreGive(_mutex);
    }
    return seq;
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

float TelemetryTracker::getCachedSpeedKph() const {
    return _speedKph;
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

uint8_t TelemetryTracker::getCachedTargetCurrentPct() const {
    return _targetCurrentPercent;
}

uint8_t TelemetryTracker::getCachedTargetSpeedPct() const {
    return _targetSpeedPercent;
}

uint16_t TelemetryTracker::getCachedCadenceRpmX10() const {
    return _cadenceRpmX10;
}

uint16_t TelemetryTracker::getCachedMotorRpmX10() const {
    return _motorRpmX10;
}

bool TelemetryTracker::isDisplayActive() const {
    return (millis() - _lastDisplayRxMs) < 2500;
}

bool TelemetryTracker::isControllerActive() const {
    return (millis() - _lastControllerRxMs) < 2500;
}

bool TelemetryTracker::hasTargetTelemetry() const {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        bool active = _hasTargetTelemetry && (millis() - _lastTargetTelemetryMs < 4000);
        xSemaphoreGive(_mutex);
        return active;
    }
    return false;
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

        // Live bbs-fw targets, pushed from the firmware's 0xEC debug frame.
        bool targetFresh = _hasTargetTelemetry && (millis() - _lastTargetTelemetryMs < 4000);
        doc["hasTargetTelemetry"] = targetFresh;
        doc["targetCurrentPercent"] = targetFresh ? _targetCurrentPercent : 0;
        doc["targetSpeedPercent"] = targetFresh ? _targetSpeedPercent : 0;
        doc["cadenceRpm"] = serialized(String(targetFresh ? (_cadenceRpmX10 / 10.0f) : 0.0f, 1));
        doc["motorRpm"] = serialized(String(targetFresh ? (_motorRpmX10 / 10.0f) : 0.0f, 1));


        // True while speed/voltage/battery are actually being measured -- by a
        // display, or by this bridge polling for them. The app uses it to decide
        // whether a number is a reading or just an untouched zero.
        // On-bike barometer: pressure, temperature, and the grade the middleman
        // derives from it and the wheel speed. Absent (0 / false) when no DPS310
        // answered, so a client can tell "no sensor" from "sea level".
        const bool baroOk = (_lastBaroMs != 0 && (millis() - _lastBaroMs < 3000));
        doc["hasBaro"] = baroOk;
        doc["baroPressureHpa"] = serialized(String(baroOk ? _baroPressureHpa : 0.0f, 2));
        doc["baroTempC"] = serialized(String(baroOk ? _baroTempC : 0.0f, 1));
        doc["baroAltitudeM"] = serialized(String(baroOk ? _baroAltitudeM : 0.0f, 1));
        doc["slopePercent"] = serialized(String(baroOk ? _slopePercent : 0.0f, 2));

        doc["hasVehicleData"] = (millis() - _lastVehicleDataMs < 5000);
        doc["displayConnected"] = isDisplayActive();
        doc["controllerConnected"] = isControllerActive();

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
