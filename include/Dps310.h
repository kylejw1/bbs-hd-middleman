#pragma once

#include <Arduino.h>
#include "Config.h"

// DPS310 barometric pressure sensor on I2C, register level.
//
// Why this exists: the slope is dh/ds, and the phone's barometer is the wrong
// reference for a bike -- it lives in a pocket, it is in a different place to the
// wheels, and it stops existing when the app is not running. A sensor on the
// middleman measures the same air the bike is riding through, and lets the grade
// be computed on the bike rather than in whatever handset happens to be connected.
//
// Register-level on purpose, for the same reason the MPU-6050 driver was: a
// library would pull in a dependency and a build flavour for a handful of
// registers, and this project keeps its firmware dependencies to one JSON parser.
//
// Units: pressure in hPa (mbar), temperature in degrees C, altitude in metres
// relative to the standard 1013.25 hPa datum. The compensation follows the
// datasheet's formula (and Infineon's own Arduino library) exactly; the
// coefficients below are the ones the part calibrates itself with at manufacture.

class Dps310 {
public:
    struct Sample {
        float pressureHpa = 0.0f;
        float temperatureC = 0.0f;
        float altitudeM = 0.0f;     // relative to 1013.25 hPa, not sea level
    };

    bool begin();
    void process();

    bool  isPresent() const { return _present; }
    uint8_t address() const { return _address; }
    uint8_t productId() const { return _productId; }
    float lastError() const { return _lastError; }

    /** Latest compensated reading. Valid once isPresent() and a sample arrived. */
    const Sample& sample() const { return _sample; }

    /** Measured sample rate, for the diagnostics that ask "is it really 36 Hz?". */
    float sampleRateHz() const { return _rateHz; }

    /** Grade in percent, from pressure against distance travelled. */
    float slopePercent() const { return _slopePercent; }

private:
    bool    readCoefficients();
    bool    readRegisters(uint8_t reg, uint8_t* buf, size_t len);
    bool    writeRegister(uint8_t reg, uint8_t value);
    int32_t readRaw(int32_t* temperatureRaw);
    float   compensatePressure(int32_t pRaw, int32_t tRaw);

    bool     _present = false;
    uint8_t  _address = 0x77;
    uint8_t  _productId = 0;
    float    _lastError = 0.0f;

    Sample   _sample;
    float    _rateHz = 0.0f;

    // Calibration coefficients, assembled exactly as the datasheet lays them out.
    int16_t  _c0Half = 0;
    int16_t  _c1 = 0;
    int32_t  _c00 = 0;
    int32_t  _c10 = 0;
    int16_t  _c01 = 0, _c11 = 0, _c20 = 0, _c21 = 0, _c30 = 0;
    float    _lastTempScaled = 0.0f;

    uint32_t _lastPollMs = 0;
    uint32_t _lastSampleMs = 0;
    uint32_t _sampleCount = 0;

    // --- grade estimator -------------------------------------------------
    // Same structure as the phone's: a least-squares fit of height against
    // distance over a travelling window, because differentiating two noisy
    // quantities gives a far noisier ratio than fitting one against the other.
    void updateSlope(float altitudeM, uint32_t nowMs);
    bool fitSlope(uint32_t nowMs, float* outPercent);

    static constexpr int      SLOPE_CAP = 192;
    double   _sDist[SLOPE_CAP] = {0};
    double   _sAlt[SLOPE_CAP]  = {0};
    uint32_t _sTime[SLOPE_CAP] = {0};
    int      _sHead = 0;
    int      _sCount = 0;
    double   _distanceM = 0.0;
    uint32_t _lastSlopeMs = 0;
    float    _slopePercent = 0.0f;
    uint32_t _lastGradeMs = 0;
};

extern Dps310 Baro;
