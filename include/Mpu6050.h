#ifndef MPU6050_H
#define MPU6050_H

// =============================================================================
// MPU6050_H — optional MPU-6050 accelerometer / gyro on I2C
//
// The GY-521 breakout carries an MPU-6050, which is I2C ONLY. There is no SPI on
// that board; the SPI-capable part is the MPU-6000, which the GY-521 does not
// expose. Nothing else in this firmware uses I2C, so the bus is free.
//
// This is a register-level driver on purpose. Adafruit_MPU6050 pulls in BusIO
// plus the Unified Sensor abstraction, which is tens of KB of flash for what is
// a handful of registers — and the classic ESP32 is already near 94% of its OTA
// slot.
//
// Polled from loop(): process() is non-blocking apart from the I2C transaction
// itself and is rate-limited to MPU_READ_HZ. Nothing here allocates after
// begin().
//
// "No sensor fitted" is a fully supported state: begin() returns false, the
// display keeps showing the bike page, and one line goes to the serial log.
// =============================================================================

#include "Config.h"

#define MPU6050_I2C_ADDR    0x68

#if MPU6050_ENABLED

#include <Arduino.h>

class Mpu6050 {
public:
    struct Sample {
        float ax, ay, az;         // g
        float gx, gy, gz;         // deg/s
        float tempC;
        float pitchDeg, rollDeg;  // complementary-filtered tilt
    };

    // Wakes the sensor and configures ranges and DLPF. Returns false when
    // nothing answers on the bus, in which case process() becomes a no-op.
    bool begin();

    // Call every loop() iteration. Rate-limited to MPU_READ_HZ internally.
    void process();

    bool     isPresent()    const { return _present; }
    uint8_t  whoAmI()       const { return _whoAmI; }
    uint8_t  address()      const { return MPU6050_I2C_ADDR; }
    uint16_t sampleRateHz() const { return _rateHz; }
    uint32_t readErrors()   const { return _readErrors; }
    const Sample& sample()  const { return _s; }

private:
    bool writeReg(uint8_t reg, uint8_t val);
    bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);

    // One 14-byte burst, unscaled: [ax ay az temp gx gy gz].
    bool readRaw(int16_t raw[7]);

    // Averages gyro bias, the 1 g reference and the starting attitude while the
    // bike is stationary. Returns false if it was clearly moving.
    bool calibrate();

    bool fetch();

    bool     _present       = false;
    uint8_t  _whoAmI        = 0;
    uint16_t _rateHz        = 0;
    uint32_t _readErrors    = 0;
    uint8_t  _consecErrors  = 0;
    uint32_t _lastReadMs    = 0;
    uint32_t _rateMarkMs    = 0;
    uint32_t _rateCount     = 0;
    Sample   _s             = {};

    // Measured at boot by calibrate().
    float    _biasGx        = 0.0f;    // deg/s, static, all three axes
    float    _biasGy        = 0.0f;
    float    _biasGz        = 0.0f;
    float    _gRef          = 1.0f;    // measured 1 g, so gain error isn't tilt

    // Adaptive-fusion estimator state (bike_pitch_algorithm.md).
    float    _pitchRad      = 0.0f;    // theta, radians, nose up positive
    float    _rollRad       = 0.0f;    // phi, radians
    float    _biasYOnline   = 0.0f;    // rad/s, integral term on the pitch axis
};

extern Mpu6050 Mpu;

#else // MPU6050_ENABLED

// ---------------------------------------------------------------------------
// Not compiled in — stub so main.cpp and LcdDisplay compile unchanged
// ---------------------------------------------------------------------------
class Mpu6050 {
public:
    struct Sample {
        float ax, ay, az;
        float gx, gy, gz;
        float tempC;
        float pitchDeg, rollDeg;
    };

    inline bool     begin()        { return false; }
    inline void     process()      {}
    inline bool     isPresent()    const { return false; }
    inline uint8_t  whoAmI()       const { return 0; }
    inline uint8_t  address()      const { return MPU6050_I2C_ADDR; }
    inline uint16_t sampleRateHz() const { return 0; }
    inline uint32_t readErrors()   const { return 0; }
    inline const Sample& sample()  const { return _s; }

private:
    Sample _s = {};
};

extern Mpu6050 Mpu;

#endif // MPU6050_ENABLED

#endif // MPU6050_H
