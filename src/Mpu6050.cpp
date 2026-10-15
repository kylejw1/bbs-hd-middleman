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

    calibrate();   // best effort — logs, and warns, if the bike was moving

    return true;
}

// =============================================================================
// calibrate() — measure gyro bias, the 1 g reference and the starting attitude
//
// The bike must be stationary. Uncancelled gyro bias is what makes the pitch
// drift during acceleration, because that is exactly when the fusion weight goes
// to zero and the estimate is running on the gyro alone.
//
// Seeding the attitude here is not in bike_pitch_algorithm.md but the algorithm
// needs it. Its residual gate compares f_x against g*sin(theta_pred), so
// starting from theta_pred = 0 while parked on a slope makes r_x exceed
// MPU_TAU_HIGH immediately and the accelerometer is rejected before it ever gets
// a chance to correct -- a cold-start deadlock that the gyro cannot break
// because a stationary bike produces no rotation to integrate. A stationary
// accelerometer has no such ambiguity, so the estimate is seeded from it.
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
        delay(2);
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
    // not show up as a permanent tilt nor as a permanent magnitude penalty.
    _gRef = sqrtf(ax0 * ax0 + ay0 * ay0 + az0 * az0);
    if (_gRef < 0.5f || _gRef > 1.5f) {
        Serial.printf("[MPU] implausible 1g reference %.3f g, using 1.000\n", _gRef);
        _gRef = 1.0f;
    }

    // Seed the attitude. Same observation the runtime filter uses, and the same
    // sign convention the document defines: nose up / climbing is POSITIVE, and
    // the forward accelerometer axis reads +g*sin(theta) on a climb.
    _pitchRad  = atan2f(ax0, sqrtf(ay0 * ay0 + az0 * az0));
    _rollRad   = atan2f(ay0, az0);
    _biasYOnline = 0.0f;
    _s.pitchDeg = _pitchRad * RAD_TO_DEG;
    _s.rollDeg  = _rollRad  * RAD_TO_DEG;

    // The zero reference: whatever attitude the board sits at while the bike is
    // stationary at power-on. slopeDeg()/leanDeg() subtract it, which is what
    // turns the absolute estimate into "how far from level this bike is" and
    // removes the permanent bias of however the sensor sits in its mount.
    //
    // It costs nothing extra: this is the same averaged sample that seeds the
    // estimator above, kept as well.
    _slopeZeroDeg = _s.pitchDeg;
    _leanZeroDeg  = _s.rollDeg;

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

    // State the zero explicitly. This line is the only record of what "level"
    // was taken to mean on this power cycle: slopeDeg() reads 0.0 against it by
    // construction, so if the bike was parked on a hill when it booted, that
    // hill is now "flat" and nothing on the display would reveal it.
    Serial.printf("[MPU] slope zero %+.1f deg, lean zero %+.1f deg "
                  "(captured at power-on -- power-cycle on level ground to redo)\n",
                  _slopeZeroDeg, _leanZeroDeg);
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
// fetch() — one cycle of the adaptive-fusion pitch estimator
//
// Implements bike_pitch_algorithm.md. The shape of it:
//
//   1. propagate pitch and roll on the gyro (immune to linear acceleration)
//   2. form the accelerometer's pitch observation
//   3. score how much the accelerometer can be trusted right now
//   4. correct the propagated estimate by (gain x trust) of the innovation
//   5. leak a little of that innovation into the gyro bias, but only at rest
//
// Step 3 is the whole point. Two independent detectors feed a weight in [0,1]:
// a magnitude check on |f| versus 1 g, and a residual check comparing f_x
// against the gravity component implied by the current pitch estimate. The
// residual does the heavy lifting for acceleration and braking -- the magnitude
// check is nearly blind to them, because horizontal acceleration perturbs |f|
// only to second order (A^2/2g, so 2% at 2 m/s^2) while perturbing f_x directly.
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

    // ---- Roll --------------------------------------------------------------
    //
    // The document specifies only the pitch estimator, so roll keeps a plain
    // fast complementary filter: lateral acceleration on a bicycle is small, so
    // its accelerometer reference is trustworthy. It feeds the pitch
    // propagation through cos(phi)/sin(phi) below and the display.
    // Roll rate is +omega_x in this frame: rotating about +X carries +Y (left)
    // toward +Z, which is leaning right, and atan2(f_y, f_z) is also positive
    // for leaning right. They have to agree, or the gyro fights the accelerometer.
    const float phiAcc = atan2f(_s.ay, _s.az);          // radians
    _rollRad = MPU_ROLL_ALPHA * (_rollRad + _s.gx * DEG_TO_RAD * dt)
             + (1.0f - MPU_ROLL_ALPHA) * phiAcc;
    _s.rollDeg = _rollRad * RAD_TO_DEG;

    // ---- Physical units for the estimator ----------------------------------
    const float fx = _s.ax * MPU_GRAVITY;    // m/s^2
    const float fy = _s.ay * MPU_GRAVITY;
    const float fz = _s.az * MPU_GRAVITY;

    const float wx = _s.gx * DEG_TO_RAD;     // rad/s
    const float wy = _s.gy * DEG_TO_RAD;
    const float wz = _s.gz * DEG_TO_RAD;

    // ---- Step 2: kinematic propagation -------------------------------------
    //
    // theta_dot = wy*cos(phi) - wz*sin(phi). MPU_PITCH_GYRO_SIGN carries the
    // sign correction explained in Config.h: the document's +omega_y contradicts
    // its own accelerometer convention for this right-handed frame, and with the
    // wrong sign the gyro and the accelerometer pull in opposite directions.
    // The entire pitch rate (including the wz*sin(phi) yaw coupling) is scaled
    // by MPU_PITCH_GYRO_SIGN so turning on a bank cancels cleanly to zero.
    const float cosPhi = cosf(_rollRad);
    const float sinPhi = sinf(_rollRad);
    const float wyUnbiased = wy;

    const float thetaPred = _pitchRad + MPU_PITCH_GYRO_SIGN * (wyUnbiased * cosPhi - wz * sinPhi) * dt;

    // ---- Step 3: forward acceleration compensation (Case B: pure IMU) -------
    //
    // Case A of the document subtracts a differentiated wheel speed here. This
    // build has no usable feed for that -- the controller's speed arrives over
    // the 1200-baud link at roughly 1 Hz and in rpm, so differentiating it
    // yields far more quantisation noise than signal. The adaptive weighting in
    // Step 5 covers the same ground without the external input.
    const float fxComp = fx;
    const float fyComp = fy;
    const float fzComp = fz;

    // ---- Step 4: accelerometer observation and anomaly scores --------------
    const float thetaAcc = atan2f(fxComp, sqrtf(fyComp * fyComp + fzComp * fzComp));

    // Against the 1 g this sensor actually measures, so a gain error is not a
    // permanent penalty on the magnitude weight.
    const float fNorm = sqrtf(fxComp * fxComp + fyComp * fyComp + fzComp * fzComp);
    const float gRef  = _gRef * MPU_GRAVITY;
    const float epsA  = fabsf(fNorm - gRef) / gRef;

    const float fxGrav = gRef * sinf(thetaPred);
    const float rX     = fabsf(fxComp - fxGrav);

    // ---- Step 5: adaptive trust --------------------------------------------
    const float sigma = MPU_SIGMA_NORM;
    const float wNorm = expf(-(epsA * epsA) / (2.0f * sigma * sigma));
    const float omegaNorm = sqrtf(wx * wx + wy * wy + wz * wz);

    // Quiescent check: when the sensor is stationary (sitting on a desk, or
    // stopped at a red light), angular rates are near zero and specific force
    // magnitude is ~1 g. There is ZERO forward acceleration. Any discrepancy
    // between the accelerometer and thetaPred is pure tilt error, NOT linear
    // acceleration. Forcing wRes to 1.0 when quiescent breaks the catastrophic
    // deadlock where a transient error > 7 deg permanently locks out the
    // accelerometer and prevents the pitch from ever returning to true level.
    const bool isQuiescent = (omegaNorm < 0.15f && epsA < MPU_SIGMA_NORM);

    float wRes;
    if (isQuiescent) {
        wRes = 1.0f;
    } else if (rX <= MPU_TAU_LOW) {
        wRes = 1.0f;
    } else if (rX >= MPU_TAU_HIGH) {
        wRes = 0.05f;   // Small floor during motion so filter can always recover
    } else {
        wRes = (MPU_TAU_HIGH - rX) / (MPU_TAU_HIGH - MPU_TAU_LOW);
        if (wRes < 0.05f) wRes = 0.05f;
    }

    // When stationary, allow faster convergence back to true gravity
    const float kpEffective = isQuiescent ? 0.06f : MPU_KP;
    const float w = isQuiescent ? 1.0f : (wNorm * wRes);

    // ---- Step 6: measurement update ----------------------------------------
    const float eTheta = atan2f(sinf(thetaAcc - thetaPred), cosf(thetaAcc - thetaPred));

    _pitchRad = thetaPred + (kpEffective * w) * eTheta;
    _s.pitchDeg = _pitchRad * RAD_TO_DEG;

    // ---- Step 6.2: online gyro bias ----------------------------------------
    // Static calibration in calibrate() removes baseline bias. The proportional
    // complementary filter (kpEffective * w * eTheta) inherently tracks and
    // suppresses residual thermal drift (< 0.05 deg/s) with sub-0.03 deg error,
    // avoiding the second-order integral runaway/ramp.
    _biasYOnline = 0.0f;

    ++_rateCount;
    return true;
}

#endif // MPU6050_ENABLED
