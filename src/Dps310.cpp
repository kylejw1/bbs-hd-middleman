#include "Dps310.h"

#include <Wire.h>
#include <math.h>
#include "Telemetry.h"

Dps310 Baro;

// -----------------------------------------------------------------------------
// registers (DPS310 datasheet, section 4)
// -----------------------------------------------------------------------------
static constexpr uint8_t REG_PSR_B2      = 0x00;   // pressure, 24 bit big-endian
static constexpr uint8_t REG_TMP_B2      = 0x03;   // temperature, 24 bit
static constexpr uint8_t REG_PRS_CFG     = 0x06;
static constexpr uint8_t REG_TMP_CFG     = 0x07;
static constexpr uint8_t REG_MEAS_CFG    = 0x08;
static constexpr uint8_t REG_CFG_REG     = 0x09;
static constexpr uint8_t REG_RESET       = 0x0C;
static constexpr uint8_t REG_PROD_ID     = 0x0D;
static constexpr uint8_t REG_COEF        = 0x10;   // 18 bytes
static constexpr uint8_t REG_TMP_COEF_SRCE = 0x28;

static constexpr uint8_t DPS310_PRODUCT_ID = 0x10;

// Scaling denominators indexed by the oversampling code (1x..128x). This is the
// datasheet's table, and it is not monotonic: the codes above 8x are a second
// set, which is exactly the sort of thing worth copying rather than deriving.
static constexpr float kScale[8] = {
    524288.0f, 1572864.0f, 3670016.0f, 7864320.0f,
    253952.0f, 516096.0f, 1040384.0f, 2088960.0f,
};

// Oversampling: code 4 = 16 measurements per result. In "high precision" terms
// this is the datasheet's >8x regime, which requires the shift-enable bits in
// CFG_REG *and* the 16x denominator above.
static constexpr uint8_t PRS_OSR_16X = 4;
static constexpr uint8_t TMP_OSR_16X = 4;

// Output data rate: code 5 = 32 Hz. 16x oversampling takes ~27 ms per
// conversion, so 32 Hz is the fastest rate that the measurement time actually
// supports; asking for 64 Hz here would silently return stale conversions.
static constexpr uint8_t RATE_32HZ = 5;

static int32_t twosComplement24(uint32_t raw) {
    if (raw & 0x800000UL) raw |= 0xFF000000UL;
    return (int32_t)raw;
}

/**
 * Poll cadence. 16x oversampling at 32 Hz means a fresh conversion every 31 ms,
 * so polling faster than ~36 Hz only reads the same result twice.
 */
static constexpr uint32_t POLL_INTERVAL_MS = 1000 / 36;

bool Dps310::begin() {
#if !DPS310_ENABLED
    return false;
#else
    Wire.begin(DPS310_SDA_PIN, DPS310_SCL_PIN, DPS310_I2C_HZ);
    Wire.setTimeOut(20);   // a missing sensor must never stall the 1200-baud bridge

    // Probe both possible addresses: SDO low gives 0x76, SDO high or floating 0x77.
    const uint8_t candidates[2] = { 0x77, 0x76 };
    for (uint8_t addr : candidates) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;

        _address = addr;
        if (!readRegisters(REG_PROD_ID, &_productId, 1)) continue;
        if (_productId != DPS310_PRODUCT_ID) continue;

        _present = true;
        break;
    }
    if (!_present) {
        Serial.printf("[BARO] no DPS310 on SDA %d / SCL %d\n", DPS310_SDA_PIN, DPS310_SCL_PIN);
        return false;
    }

    // Soft reset, then wait for the coefficient block to be ready.
    writeRegister(REG_RESET, 0x89);
    delay(40);

    if (!readCoefficients()) {
        _present = false;
        return false;
    }

    // Temperature source: the part's own sensor (bit 7 clear in TMP_CFG means
    // internal). The external option is for a thermistor on a DPS368.
    writeRegister(REG_TMP_COEF_SRCE, 0x00);

    // 16x oversampling on both channels, 32 Hz output.
    writeRegister(REG_PRS_CFG, (uint8_t)((RATE_32HZ << 4) | PRS_OSR_16X));
    writeRegister(REG_TMP_CFG, (uint8_t)((RATE_32HZ << 4) | TMP_OSR_16X));

    // Shift enable for oversampling above 8x: without these the raw values
    // overflow the 24-bit result registers and the compensation is nonsense.
    writeRegister(REG_CFG_REG, (uint8_t)((1 << 3) | (1 << 2)));

    // Continuous pressure + temperature measurement.
    writeRegister(REG_MEAS_CFG, 0b111);

    _lastPollMs = millis();
    _lastSampleMs = 0;

    Serial.printf("[BARO] DPS310 at 0x%02X, 16x oversampling @ 32 Hz on SDA %d / SCL %d\n",
                  _address, DPS310_SDA_PIN, DPS310_SCL_PIN);
    return true;
#endif
}

void Dps310::process() {
    if (!_present) return;

    const uint32_t now = millis();
    if (now - _lastPollMs < POLL_INTERVAL_MS) return;
    _lastPollMs = now;

    int32_t tRaw = 0;
    const int32_t pRaw = readRaw(&tRaw);
    if (pRaw == INT32_MIN) return;   // not ready yet

    const float pressure = compensatePressure(pRaw, tRaw);
    // Plausibility: a bike is never at 300 hPa or 1100 hPa, and a failed I2C
    // read produces exactly those extremes.
    if (pressure < 300.0f || pressure > 1100.0f) return;

    _sample.pressureHpa = pressure;
    _sample.temperatureC = _lastTempScaled;   // compensated below, see calcTemp
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
    // their variance turns a few pascals into tens of percent of grade.
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

bool Dps310::readRegisters(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)_address, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

bool Dps310::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool Dps310::readCoefficients() {
    uint8_t b[18];
    if (!readRegisters(REG_COEF, b, sizeof(b))) return false;

    auto s12 = [](uint32_t v) -> int16_t {
        if (v & 0x800U) v |= 0xF000U;
        return (int16_t)v;
    };
    auto s20 = [](uint32_t v) -> int32_t {
        if (v & 0x80000UL) v |= 0xFFF00000UL;
        return (int32_t)v;
    };

    // c0 is only ever used halved, so halve it here and keep the name honest.
    _c0Half = (int16_t)(s12(((uint32_t)b[0] << 4) | (b[1] >> 4)) / 2);
    _c1  = s12(((uint32_t)(b[1] & 0x0F) << 8) | b[2]);
    _c00 = s20(((uint32_t)b[3] << 12) | ((uint32_t)b[4] << 4) | (b[5] >> 4));
    _c10 = s20(((uint32_t)(b[5] & 0x0F) << 16) | ((uint32_t)b[6] << 8) | b[7]);
    _c01 = (int16_t)(((uint32_t)b[8] << 8) | b[9]);
    _c11 = (int16_t)(((uint32_t)b[10] << 8) | b[11]);
    _c20 = (int16_t)(((uint32_t)b[12] << 8) | b[13]);
    _c21 = (int16_t)(((uint32_t)b[14] << 8) | b[15]);
    _c30 = (int16_t)(((uint32_t)b[16] << 8) | b[17]);

    // An all-zero or all-ones block means the read failed rather than that the
    // part has no calibration.
    bool anyNonZero = false;
    for (uint8_t v : b) if (v != 0x00 && v != 0xFF) { anyNonZero = true; break; }
    return anyNonZero;
}

/**
 * One conversion. Returns INT32_MIN when a fresh result is not ready yet, which
 * is normal at the top of the 32 Hz cycle.
 */
int32_t Dps310::readRaw(int32_t* temperatureRaw) {
    uint8_t status = 0;
    if (!readRegisters(REG_MEAS_CFG, &status, 1)) return INT32_MIN;
    if (!(status & 0x20) || !(status & 0x10)) return INT32_MIN;   // TMP_RDY | PRS_RDY

    uint8_t p[3], t[3];
    if (!readRegisters(REG_PSR_B2, p, 3)) return INT32_MIN;
    if (!readRegisters(REG_TMP_B2, t, 3)) return INT32_MIN;

    const uint32_t pRaw = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    const uint32_t tRaw = ((uint32_t)t[0] << 16) | ((uint32_t)t[1] << 8) | t[2];

    *temperatureRaw = twosComplement24(tRaw);
    return twosComplement24(pRaw);
}

/**
 * Datasheet compensation, term for term. The temperature is scaled once and kept
 * because the pressure formula needs it and the part supplies it on the same
 * conversion.
 */
float Dps310::compensatePressure(int32_t pRaw, int32_t tRaw) {
    const float tScaled = (float)tRaw / kScale[TMP_OSR_16X];
    _lastTempScaled = (float)_c0Half + (float)_c1 * tScaled;

    const float pScaled = (float)pRaw / kScale[PRS_OSR_16X];
    return (float)_c00
         + pScaled * ((float)_c10 + pScaled * ((float)_c20 + pScaled * (float)_c30))
         + tScaled * ((float)_c01 + pScaled * ((float)_c11 + pScaled * (float)_c21));
}
