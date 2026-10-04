#include "Mpu6050.h"

// The global singleton, in both the real and the stubbed build.
Mpu6050 Mpu;

#if MPU6050_ENABLED

#include <Wire.h>

// =============================================================================
// Register map — only what this driver touches
// =============================================================================
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_XOUT_H    0x3B
#define REG_PWR_MGMT_1      0x6B
#define REG_WHO_AM_I        0x75

#define WHO_MPU6050         0x68
#define WHO_MPU6500         0x70
#define WHO_MPU9250         0x71
#define WHO_MPU9255         0x73

// With the DLPF enabled the gyro runs at 1 kHz, so the divider is
// (1000 / rate) - 1.
#define SAMPLE_DIV          (1000 / MPU_READ_HZ - 1)

// I2C transaction timeout. Deliberately far below the 50 ms default: this runs
// on the same task as the 1200-baud bridge, where a byte arrives every 8.3 ms.
// A stuck bus must not stall loop() long enough to overrun the UART.
#define I2C_TIMEOUT_MS      20

// After this many consecutive failed reads, assume the sensor has dropped off
// the bus and stop polling it — otherwise every loop() would block for the I2C
// timeout and starve the bridge.
#define MAX_CONSEC_ERRORS   10

// -----------------------------------------------------------------------------
// Full-scale ranges and their LSB sensitivities
// -----------------------------------------------------------------------------
#if   MPU_ACCEL_FS == 2
  #define ACCEL_AFS_SEL     0x00
  #define ACCEL_LSB_PER_G   16384.0f
#elif MPU_ACCEL_FS == 4
  #define ACCEL_AFS_SEL     0x08
  #define ACCEL_LSB_PER_G   8192.0f
#elif MPU_ACCEL_FS == 8
  #define ACCEL_AFS_SEL     0x10
  #define ACCEL_LSB_PER_G   4096.0f
#else
  #define ACCEL_AFS_SEL     0x18
  #define ACCEL_LSB_PER_G   2048.0f
#endif

#if   MPU_GYRO_FS == 250
  #define GYRO_FS_SEL       0x00
  #define GYRO_LSB_PER_DPS  131.0f
#elif MPU_GYRO_FS == 500
  #define GYRO_FS_SEL       0x08
  #define GYRO_LSB_PER_DPS  65.5f
#elif MPU_GYRO_FS == 1000
  #define GYRO_FS_SEL       0x10
  #define GYRO_LSB_PER_DPS  32.8f
#else
  #define GYRO_FS_SEL       0x18
  #define GYRO_LSB_PER_DPS  16.4f
#endif

// =============================================================================
// Low-level I2C
// =============================================================================
bool Mpu6050::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool Mpu6050::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;   // repeated start
    if (Wire.requestFrom((uint8_t)MPU6050_I2C_ADDR, (uint8_t)len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = (uint8_t)Wire.read();
    return true;
}

// One burst read, still in raw counts. Must be a single transaction: accel,
// temperature and gyro all have to come from the same sample instant.
bool Mpu6050::readRaw(int16_t raw[7]) {
    uint8_t buf[14];
    if (!readRegs(REG_ACCEL_XOUT_H, buf, sizeof(buf))) return false;

    raw[0] = (int16_t)((buf[0]  << 8) | buf[1]);   // ax
    raw[1] = (int16_t)((buf[2]  << 8) | buf[3]);   // ay
    raw[2] = (int16_t)((buf[4]  << 8) | buf[5]);   // az
    raw[3] = (int16_t)((buf[6]  << 8) | buf[7]);   // temperature
    raw[4] = (int16_t)((buf[8]  << 8) | buf[9]);   // gx
    raw[5] = (int16_t)((buf[10] << 8) | buf[11]);  // gy
    raw[6] = (int16_t)((buf[12] << 8) | buf[13]);  // gz
    return true;
}

// =============================================================================
// begin()
// =============================================================================
bool Mpu6050::begin() {
    // Pins are always passed explicitly. Wire's defaults are SDA 21 / SCL 22 on
    // a classic ESP32 and SDA 8 / SCL 9 on an S3 — on this project those are the
    // LCD's CS/SCLK and RST/DC, so the argument-less form would fight the panel.
    Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN, MPU_I2C_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    if (!readRegs(REG_WHO_AM_I, &_whoAmI, 1)) {
        _present = false;
        return false;
    }
    // 0x00 / 0xFF mean nothing answered, or the line is stuck low/high.
    if (_whoAmI == 0x00 || _whoAmI == 0xFF) {
        _present = false;
        return false;
    }

    writeReg(REG_PWR_MGMT_1, 0x80);     // device reset
    delay(100);
    writeReg(REG_PWR_MGMT_1, 0x01);     // wake, PLL with X gyro reference
    delay(50);

    writeReg(REG_SMPLRT_DIV,  SAMPLE_DIV);
    writeReg(REG_CONFIG,      0x03);    // DLPF: accel 44 Hz, gyro 42 Hz
    writeReg(REG_GYRO_CONFIG, GYRO_FS_SEL);
    writeReg(REG_ACCEL_CONFIG, ACCEL_AFS_SEL);

    _present    = true;
    _lastReadMs = millis();
    _rateMarkMs = _lastReadMs;

    // One-pole coefficient for the az low-pass. alpha = dt / (tau + dt).
    const float dtMs = 1000.0f / (float)MPU_READ_HZ;
    _azLpCoef = dtMs / ((float)MPU_AZ_LP_MS + dtMs);

    calibrate();   // best effort — logs, and warns, if the bike was moving

    return true;
}

// =============================================================================
// calibrate() — measure gyro bias, the 1 g reference and the starting attitude
//
// The bike must be stationary. Uncancelled gyro bias is what makes the pitch
// drift during acceleration, because that is exactly when the filter is running
// on the gyro alone with the accelerometer correction slowed right down.
//
// Seeding the attitude here also resolves the branch that the Z-only pitch
// reference loses: acos() is even in pitch, so it cannot tell a climb from a
// descent, but a stationary accelerometer can.
// =============================================================================
bool Mpu6050::calibrate() {
    int16_t raw[7];
    double  sAx = 0, sAy = 0, sAz = 0, sGx = 0, sGy = 0, sGz = 0;
    int16_t minGx = 32767, maxGx = -32768;
    int16_t minGy = 32767, maxGy = -32768;
    int16_t minGz = 32767, maxGz = -32768;
    uint16_t n = 0;

    for (uint16_t i = 0; i < MPU_CAL_SAMPLES; ++i) {
        if (!readRaw(raw)) break;
        sAx += raw[0]; sAy += raw[1]; sAz += raw[2];
        sGx += raw[4]; sGy += raw[5]; sGz += raw[6];
        if (raw[4] < minGx) minGx = raw[4];
        if (raw[4] > maxGx) maxGx = raw[4];
        if (raw[5] < minGy) minGy = raw[5];
        if (raw[5] > maxGy) maxGy = raw[5];
        if (raw[6] < minGz) minGz = raw[6];
        if (raw[6] > maxGz) maxGz = raw[6];
        ++n;
    }

    if (n < (MPU_CAL_SAMPLES / 2)) {
        Serial.printf("[MPU] calibration skipped: only %u of %u samples read\n",
                      (unsigned)n, (unsigned)MPU_CAL_SAMPLES);
        return false;
    }

    const float inv = 1.0f / (float)n;
    _biasGx = (float)(sGx * inv) / GYRO_LSB_PER_DPS;
    _biasGy = (float)(sGy * inv) / GYRO_LSB_PER_DPS;
    _biasGz = (float)(sGz * inv) / GYRO_LSB_PER_DPS;

    const float ax0 = (float)(sAx * inv) / ACCEL_LSB_PER_G;
    const float ay0 = (float)(sAy * inv) / ACCEL_LSB_PER_G;
    const float az0 = (float)(sAz * inv) / ACCEL_LSB_PER_G;

    // Normalise against the 1 g actually measured, so a sensor gain error does
    // not show up as a permanent tilt.
    _gRef = sqrtf(ax0 * ax0 + ay0 * ay0 + az0 * az0);
    if (_gRef < 0.5f || _gRef > 1.5f) {
        Serial.printf("[MPU] implausible 1g reference %.3f g, using 1.000\n", _gRef);
        _gRef = 1.0f;
    }

    _s.pitchDeg = atan2f(-ax0, az0) * RAD_TO_DEG;
    _s.rollDeg  = atan2f( ay0, az0) * RAD_TO_DEG;

    _azLp = az0 / _gRef;
    if (_azLp >  1.0f) _azLp =  1.0f;
    if (_azLp < -1.0f) _azLp = -1.0f;

    // Was it actually still? Peak-to-peak gyro spread is the cheapest honest
    // check, and a moving calibration bakes real rotation into the bias.
    const int32_t spreadGx = (int32_t)maxGx - (int32_t)minGx;
    const int32_t spreadGy = (int32_t)maxGy - (int32_t)minGy;
    const int32_t spreadGz = (int32_t)maxGz - (int32_t)minGz;
    int32_t spread = spreadGx;
    if (spreadGy > spread) spread = spreadGy;
    if (spreadGz > spread) spread = spreadGz;
    const float spreadDps = (float)spread / GYRO_LSB_PER_DPS;
    const bool  still     = spreadDps < MPU_CAL_MAX_SPREAD_DPS;

    Serial.printf("[MPU] cal %u samples: bias %+.2f/%+.2f/%+.2f dps, 1g %.3f, "
                  "pitch0 %+.1f roll0 %+.1f, spread %.1f dps%s\n",
                  (unsigned)n, _biasGx, _biasGy, _biasGz, _gRef,
                  _s.pitchDeg, _s.rollDeg, spreadDps,
                  still ? "" : "   <- MOVING? pitch will drift");
    return still;
}

// =============================================================================
// process() — polled from loop(), rate-limited
// =============================================================================
void Mpu6050::process() {
    if (!_present) return;

    const uint32_t now = millis();
    const uint32_t interval = 1000u / MPU_READ_HZ;
    if (now - _lastReadMs < interval) return;
    _lastReadMs = now;

    if (fetch()) {
        _consecErrors = 0;
    } else {
        ++_readErrors;
        if (++_consecErrors >= MAX_CONSEC_ERRORS) {
            _present = false;
            Serial.printf("[MPU] no response after %u tries, %lu errors - disabled\n",
                          (unsigned)_consecErrors, (unsigned long)_readErrors);
        }
    }

    // Measure the achieved rate; it is the useful number when tuning the bus
    // speed, the DLPF, or when the bridge is competing for loop() time.
    if (now - _rateMarkMs >= 1000) {
        _rateHz     = (uint16_t)_rateCount;
        _rateCount  = 0;
        _rateMarkMs = now;
    }
}

// =============================================================================
// fetch() — scale one burst and run the two axis filters
// =============================================================================
bool Mpu6050::fetch() {
    int16_t raw[7];
    if (!readRaw(raw)) return false;

    _s.ax = raw[0] / ACCEL_LSB_PER_G;
    _s.ay = raw[1] / ACCEL_LSB_PER_G;
    _s.az = raw[2] / ACCEL_LSB_PER_G;
    _s.tempC = (raw[3] / 340.0f) + 36.53f;

    // Bias-corrected: the gyro is the only thing that stays honest while the
    // bike is accelerating, so it has to be worth trusting.
    _s.gx = raw[4] / GYRO_LSB_PER_DPS - _biasGx;
    _s.gy = raw[5] / GYRO_LSB_PER_DPS - _biasGy;
    _s.gz = raw[6] / GYRO_LSB_PER_DPS - _biasGz;

    const float dt = 1.0f / (float)MPU_READ_HZ;

    // ---- Roll (first: the pitch reference below is de-leaned by it) --------
    //
    // Lateral acceleration on a bicycle is small, so the accelerometer's roll
    // reference can be trusted and corrected quickly -- which is what keeps gyro
    // drift out. Left deliberately on the ordinary formula.
    const float accelRoll = atan2f(_s.ay, _s.az) * RAD_TO_DEG;
    _s.rollDeg = MPU_ROLL_ALPHA * (_s.rollDeg - _s.gx * dt)
               + (1.0f - MPU_ROLL_ALPHA) * accelRoll;

    // ---- Pitch -------------------------------------------------------------
    //
    // Not atan2(-ax, sqrt(ay^2 + az^2)). The bike accelerates along its own X
    // axis, and the sensor's Z axis is the bike's up, so those are perpendicular
    // by construction: the bike's acceleration contributes nothing to az. On the
    // flat at 2 m/s^2 the three-axis formula reads 11.5 degrees off while |a| is
    // only 2% away from 1 g -- and a 0.02 g bump moves |a| by that same 2%, which
    // is why gating on |a| cannot separate acceleration from a rough road -- but
    // az still says cos(pitch) exactly.
    //
    // So the accelerometer's contribution comes from Z alone. acos() is even in
    // pitch and loses the sign, which the gyro-propagated estimate supplies;
    // calibrate() seeded it while the bike was known to be stationary.
    //
    // Vertical acceleration from bumps and suspension does land on Z, hence the
    // low-pass. A hill's pitch does not change on a 1.5 s timescale.
    const float azNorm = _s.az / _gRef;
    _azLp += (azNorm - _azLp) * _azLpCoef;

    // az = cos(pitch) * cos(roll), so divide the lean back out. Without this a
    // 15 degree bank reads as 15.8 degrees of pitch rather than the 5 it is, and
    // a long turn would drag the estimate with it.
    const float cosRoll = cosf(_s.rollDeg * DEG_TO_RAD);
    float azPitch = (cosRoll > 0.2f) ? (_azLp / cosRoll) : _azLp;
    if (azPitch >  1.0f) azPitch =  1.0f;
    if (azPitch < -1.0f) azPitch = -1.0f;

    const float gyroPitch   = _s.pitchDeg + _s.gy * dt;
    const float pitchMagDeg = acosf(azPitch) * RAD_TO_DEG;
    const float pitchRef    = (gyroPitch >= 0.0f) ? pitchMagDeg : -pitchMagDeg;
    _s.pitchDeg = MPU_PITCH_ALPHA * gyroPitch
                + (1.0f - MPU_PITCH_ALPHA) * pitchRef;

    ++_rateCount;
    return true;
}

#endif // MPU6050_ENABLED
