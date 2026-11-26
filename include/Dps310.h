#pragma once

#include <Arduino.h>
#include "Config.h"

// DPS310 barometric pressure sensor on I2C.
//
// This wraps Adafruit's DPS310 library rather than driving the registers here.
// That is a deliberate reversal: a hand-written driver was measured against the
// library on the same board in the same mode and lost -- it read the temperature
// 26 degrees low (-3.6 C in a 23 C room), which shifted the compensated pressure
// by 5 hPa, because the coefficient handling for the temperature term is subtler
// than the datasheet's formula suggests. The library gets it right, is actively
// maintained, and is one implementation instead of two.
//
// What this class adds on top:
//   * the measurement mode this project wants (see begin()),
//   * a ready check before reading, which the library does not do -- its first
//     conversions read stale registers and reported 833 hPa and -66 C,
//   * the grade estimate, which is our own arithmetic on top of the readings.
//
// Units: pressure in hPa, temperature in degrees C, altitude in metres relative
// to the standard 1013.25 hPa datum.

class Dps310 {
public:
    struct Sample {
        float pressureHpa = 0.0f;
        float temperatureC = 0.0f;
        float altitudeM = 0.0f;     // relative to 1013.25 hPa, not sea level
    };

    bool begin();
    void process();

    bool    isPresent() const { return _present; }
    uint8_t address() const { return _address; }

    /** Latest reading. Valid once isPresent() and a sample has arrived. */
    const Sample& sample() const { return _sample; }

    /** Measured sample rate, for the diagnostics that ask "is it really 32 Hz?". */
    float sampleRateHz() const { return _rateHz; }

    /** Grade in percent, from pressure against distance travelled. */
    float slopePercent() const { return _slopePercent; }

private:
    // --- grade estimator -------------------------------------------------
    // A least-squares fit of height against distance over a travelling window,
    // because differentiating two noisy quantities gives a far noisier ratio
    // than fitting one against the other.
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

    bool     _present = false;
    uint8_t  _address = 0x77;

    Sample   _sample;
    float    _rateHz = 0.0f;

    uint32_t _lastPollMs = 0;
    uint32_t _lastProbeMs = 0;
    uint32_t _lastSampleMs = 0;
    uint32_t _sampleCount = 0;
};

extern Dps310 Baro;
