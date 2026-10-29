#include "Dps310.h"

#include <Adafruit_DPS310.h>
#include <Wire.h>
#include <math.h>

#include "Telemetry.h"

Dps310 Baro;

// The library object is file-static: nothing outside this translation unit needs
// to know which library is behind the wrapper, and swapping it again would not
// touch a header.
static Adafruit_DPS310 s_dps;

/**
 * Measurement mode.
 *
 * 16x oversampling at 32 Hz. Above 16x the conversion takes long enough that the
 * output rate has to drop below the sample rate the grade window wants (~30 Hz at
 * 4 s / 192 samples); 32 Hz keeps the window fed, and the library's noise floor
 * at 16x is still well under a centimetre of height. The bench measured 27 mm of
 * noise at 128x, so 16x lands near 75 mm -- ample against a 1 % level band.
 */
static constexpr dps310_rate_t       kPressureRate = DPS310_32HZ;
static constexpr dps310_oversample_t kPressureOs   = DPS310_16SAMPLES;

// Temperature only feeds the compensation, so it is measured rarely and the last
// value is reused in between.
static constexpr dps310_rate_t       kTempRate = DPS310_1HZ;
static constexpr dps310_oversample_t kTempOs   = DPS310_1SAMPLE;

// A sample is only trusted once the temperature is physically plausible. The
// library reads whatever is in the register without checking readiness, so the
// first conversions after setMode() return stale registers: 833 hPa and -66 C on
// the bench.
static constexpr float kMinPlausibleTempC = -40.0f;
static constexpr float kMaxPlausibleTempC = 85.0f;

bool Dps310::begin() {
#if !DPS310_ENABLED
    return false;
#else
    Wire.begin(DPS310_SDA_PIN, DPS310_SCL_PIN, DPS310_I2C_HZ);
    Wire.setTimeOut(20);   // a missing sensor must never stall the 1200-baud bridge

    // Probe both possible addresses: SDO low gives 0x76, SDO high or floating 0x77.
    const uint8_t candidates[2] = { 0x77, 0x76 };
    for (uint8_t addr : candidates) {
        if (s_dps.begin_I2C(addr, &Wire)) {
            _address = addr;
            _present = true;
            break;
        }
    }
    if (!_present) {
        Serial.printf("[BARO] no DPS310 on SDA %d / SCL %d\n",
                      DPS310_SDA_PIN, DPS310_SCL_PIN);
        return false;
    }

    s_dps.configurePressure(kPressureRate, kPressureOs);
    s_dps.configureTemperature(kTempRate, kTempOs);
    s_dps.setMode(DPS310_CONT_PRESTEMP);

    _lastPollMs = millis();
    _lastSampleMs = 0;

    Serial.printf("[BARO] DPS310 at 0x%02X, 16x oversampling @ 32 Hz, SDA %d / SCL %d\n",
                  _address, DPS310_SDA_PIN, DPS310_SCL_PIN);
    return true;
#endif
}

void Dps310::process() {
    if (!_present) return;

    // Ask the part whether a new pressure conversion exists. The library's own
    // read does not check, so calling it on a timer returns duplicates and, at
    // start-up, stale registers.
    if (!s_dps.pressureAvailable()) return;

    sensors_event_t pe, te;
    if (!s_dps.getEvents(&te, &pe)) return;

    const float pressure = pe.pressure;      // hPa
    const float temperature = te.temperature; // degrees C

    if (!isfinite(pressure) || pressure < 300.0f || pressure > 1100.0f) return;
    if (!isfinite(temperature) ||
        temperature < kMinPlausibleTempC || temperature > kMaxPlausibleTempC) {
        return;
    }

    const uint32_t now = millis();

    _sample.pressureHpa = pressure;
    _sample.temperatureC = temperature;
    _sample.altitudeM = 44330.0f * (1.0f - powf(pressure / 1013.25f, 1.0f / 5.255f));

    if (_lastSampleMs != 0) {
        const float dt = (now - _lastSampleMs) / 1000.0f;
        if (dt > 0.0f) {
            // Slow EMA so the reported rate is a measurement, not a jitter trace.
            const float inst = 1.0f / dt;
            _rateHz = (_rateHz <= 0.0f) ? inst : (_rateHz * 0.9f + inst * 0.1f);
        }
    }
    _lastSampleMs = now;
    _sampleCount++;

    updateSlope(_sample.altitudeM, now);

    // Publish for the API, the BLE push and the phone app. One source of truth:
    // these are the same getters the dashboard reads, so the two cannot disagree.
    Telemetry.updateBaro(_sample.pressureHpa, _sample.temperatureC, _sample.altitudeM);
    Telemetry.updateSlopePercent(_slopePercent);
}

// -----------------------------------------------------------------------------
// Grade: dh/ds over a travelling window, computed on the bike
// -----------------------------------------------------------------------------
static constexpr uint32_t SLOPE_WINDOW_MS = 4000;
static constexpr double   SLOPE_MIN_SPAN_M = 5.0;
static constexpr double   SLOPE_MAX_RESIDUAL_M = 0.55;
static constexpr float    SLOPE_MAX_PCT = 40.0f;
static constexpr float    SLOPE_SLEW_PCT_PER_S = 3.0f;
static constexpr float    MIN_SPEED_MPS = 1.2f;   // under ~4 km/h no useful ds

void Dps310::updateSlope(float altitudeM, uint32_t nowMs) {
    const float speedMps = Telemetry.getCachedSpeedKph() / 3.6f;
    if (speedMps < MIN_SPEED_MPS) {
        // Standing still measures nothing; drop the window so a stale slope
        // cannot outlive the hill that produced it.
        _sCount = 0;
        _sHead = 0;
        _lastSlopeMs = 0;
        return;
    }

    const double dt = (_lastSlopeMs == 0) ? 0.0 : (nowMs - _lastSlopeMs) / 1000.0;
    _lastSlopeMs = nowMs;
    _distanceM += speedMps * (dt > 0.5 ? 0.5 : dt);

    const int at = (_sHead + _sCount) % SLOPE_CAP;
    if (_sCount < SLOPE_CAP) {
        _sDist[at] = _distanceM;
        _sAlt[at] = altitudeM;
        _sTime[at] = nowMs;
        _sCount++;
    } else {
        _sDist[_sHead] = _distanceM;
        _sAlt[_sHead] = altitudeM;
        _sTime[_sHead] = nowMs;
        _sHead = (_sHead + 1) % SLOPE_CAP;
    }

    float grade = 0.0f;
    if (!fitSlope(nowMs, &grade)) return;

    const float since = (_lastGradeMs == 0) ? 0.0f : (nowMs - _lastGradeMs) / 1000.0f;
    _lastGradeMs = nowMs;
    const float maxStep = SLOPE_SLEW_PCT_PER_S * since;
    _slopePercent += (since <= 0.0f) ? (grade - _slopePercent)
                                     : constrain(grade - _slopePercent, -maxStep, maxStep);
}

bool Dps310::fitSlope(uint32_t nowMs, float* outPercent) {
    while (_sCount > 0 && nowMs - _sTime[_sHead] > SLOPE_WINDOW_MS) {
        _sHead = (_sHead + 1) % SLOPE_CAP;
        _sCount--;
    }
    if (_sCount < 12) return false;

    const int first = (_sHead + _sCount - 1) % SLOPE_CAP;
    if (_sDist[first] - _sDist[_sHead] < SLOPE_MIN_SPAN_M) return false;

    double sumS = 0, sumH = 0;
    for (int i = 0; i < _sCount; i++) {
        const int at = (_sHead + i) % SLOPE_CAP;
        sumS += _sDist[at];
        sumH += _sAlt[at];
    }
    const double meanS = sumS / _sCount;
    const double meanH = sumH / _sCount;

    double cov = 0, varS = 0;
    for (int i = 0; i < _sCount; i++) {
        const int at = (_sHead + i) % SLOPE_CAP;
        const double ds = _sDist[at] - meanS;
        cov += ds * (_sAlt[at] - meanH);
        varS += ds * ds;
    }
    // The gate that matters: if the sample positions barely spread, dividing by
    // their variance turns a few centimetres into tens of percent of grade.
    if (varS < _sCount * 4.0) return false;

    const double slope = cov / varS;
    double resid = 0;
    for (int i = 0; i < _sCount; i++) {
        const int at = (_sHead + i) % SLOPE_CAP;
        const double e = _sAlt[at] - (meanH + slope * (_sDist[at] - meanS));
        resid += e * e;
    }
    if (sqrt(resid / _sCount) > SLOPE_MAX_RESIDUAL_M) return false;

    *outPercent = constrain((float)(slope * 100.0), -SLOPE_MAX_PCT, SLOPE_MAX_PCT);
    return true;
}
