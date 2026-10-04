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
// A stuck bus must not stall loop() for long enough to overrun the UART.
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

    _present   = true;
    _lastReadMs = millis();
    _rateMarkMs = _lastReadMs;
    return true;
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
// fetch() — one 14-byte burst read
//
// Must be a single read: accel, temperature and gyro all come from the same
// sample, so reading them separately would mix two sample instants.
// =============================================================================
bool Mpu6050::fetch() {
    uint8_t raw[14];
    if (!readRegs(REG_ACCEL_XOUT_H, raw, sizeof(raw))) return false;

    const int16_t rax = (int16_t)((raw[0]  << 8) | raw[1]);
    const int16_t ray = (int16_t)((raw[2]  << 8) | raw[3]);
    const int16_t raz = (int16_t)((raw[4]  << 8) | raw[5]);
    const int16_t rtp = (int16_t)((raw[6]  << 8) | raw[7]);
    const int16_t rgx = (int16_t)((raw[8]  << 8) | raw[9]);
    const int16_t rgy = (int16_t)((raw[10] << 8) | raw[11]);
    const int16_t rgz = (int16_t)((raw[12] << 8) | raw[13]);

    _s.ax = rax / ACCEL_LSB_PER_G;
    _s.ay = ray / ACCEL_LSB_PER_G;
    _s.az = raz / ACCEL_LSB_PER_G;

    _s.gx = rgx / GYRO_LSB_PER_DPS;
    _s.gy = rgy / GYRO_LSB_PER_DPS;
    _s.gz = rgz / GYRO_LSB_PER_DPS;

    _s.tempC = (rtp / 340.0f) + 36.53f;

    // Complementary filter: the gyro carries short-term response, the
    // accelerometer's gravity vector stops the drift. Accelerometer-only tilt is
    // unusable on a vibrating bike, and gyro-only integration wanders within
    // seconds.
    const float accelPitch = atan2f(-_s.ax, sqrtf(_s.ay * _s.ay + _s.az * _s.az)) * RAD_TO_DEG;
    const float accelRoll  = atan2f( _s.ay, _s.az) * RAD_TO_DEG;
    const float dt = 1.0f / (float)MPU_READ_HZ;

    _s.pitchDeg = MPU_COMP_ALPHA * (_s.pitchDeg + _s.gy * dt)
                + (1.0f - MPU_COMP_ALPHA) * accelPitch;
    _s.rollDeg  = MPU_COMP_ALPHA * (_s.rollDeg  - _s.gx * dt)
                + (1.0f - MPU_COMP_ALPHA) * accelRoll;

    ++_rateCount;
    return true;
}

#endif // MPU6050_ENABLED
