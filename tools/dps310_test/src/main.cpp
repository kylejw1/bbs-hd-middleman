// DPS310 bench test — ESP32 DevKit V1.
//
// Wiring as specified: DPS310 SCK -> GPIO 21, DPS310 SDI -> GPIO 22.
// (That is the opposite of the ESP32's usual default, so it is set explicitly
// rather than left to Wire's defaults.)
//
// Highest pressure precision: 128x oversampling. Temperature only exists here
// because the pressure compensation needs it, so it runs at 1x and as slowly as
// the part allows.
//
// The loop polls the measurement-ready flag rather than reading on a timer: at
// 128x a conversion takes something over 100 ms, and a fixed delay either reads
// the same result twice or reads before it exists.

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ---------------------------------------------------------------------------
// wiring
// ---------------------------------------------------------------------------
static constexpr int     PIN_SCL = 21;      // DPS310 SCK
static constexpr int     PIN_SDA = 22;      // DPS310 SDI
static constexpr uint32_t I2C_HZ = 400000;

// ---------------------------------------------------------------------------
// registers (DPS310 datasheet section 4)
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_PSR_B2        = 0x00;   // 3 bytes, MSB first
static constexpr uint8_t REG_TMP_B2        = 0x03;   // 3 bytes, MSB first
static constexpr uint8_t REG_PRS_CFG       = 0x06;
static constexpr uint8_t REG_TMP_CFG       = 0x07;
static constexpr uint8_t REG_MEAS_CFG      = 0x08;
static constexpr uint8_t REG_CFG_REG       = 0x09;
static constexpr uint8_t REG_RESET         = 0x0C;
static constexpr uint8_t REG_PROD_ID       = 0x0D;
static constexpr uint8_t REG_COEF          = 0x10;   // 18 bytes
static constexpr uint8_t REG_TMP_COEF_SRCE = 0x28;

static constexpr uint8_t PROD_ID_DPS310 = 0x10;

// --- measurement configuration ---------------------------------------------
// Oversampling codes: 0=1x 1=2x 2=4x 3=8x 4=16x 5=32x 6=64x 7=128x
// Rate codes (bits 6:4): 0=1Hz 1=2 2=4 3=8 4=16 5=32 6=64 7=128
static constexpr uint8_t PRS_OSR = 7;    // 128x — the highest the part offers
static constexpr uint8_t PRS_RATE = 2;   // 4 Hz: 128x takes ~150 ms per result
static constexpr uint8_t TMP_OSR = 0;    // 1x — only needed for compensation
static constexpr uint8_t TMP_RATE = 0;   // 1 Hz

// Scaling denominators indexed by oversampling code. This table is not
// monotonic — the codes above 8x are a second set — which is why it is copied
// rather than computed.
static constexpr float kScale[8] = {
    524288.0f, 1572864.0f, 3670016.0f, 7864320.0f,
    253952.0f, 516096.0f, 1040384.0f, 2088960.0f,
};

// calibration
static int16_t c0Half, c1;
static int32_t c00, c10;
static int16_t c01, c11, c20, c21, c30;
static float   lastTempScaled = 0.0f;

static uint8_t address = 0x77;

// ---------------------------------------------------------------------------
// I2C helpers
// ---------------------------------------------------------------------------
static bool readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)address, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

static bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static int32_t signExtend(uint32_t v, uint8_t bits) {
    const uint32_t m = 1UL << (bits - 1);
    if (v & m) v |= ~((1UL << bits) - 1);
    return (int32_t)v;
}

// ---------------------------------------------------------------------------
// calibration block
// ---------------------------------------------------------------------------
static bool readCoefficients() {
    uint8_t b[18];
    if (!readRegs(REG_COEF, b, sizeof(b))) return false;

    // c0 is only ever used halved, so halve it once here.
    c0Half = (int16_t)(signExtend(((uint32_t)b[0] << 4) | (b[1] >> 4), 12) / 2);
    c1  = (int16_t)signExtend(((uint32_t)(b[1] & 0x0F) << 8) | b[2], 12);
    c00 = signExtend(((uint32_t)b[3] << 12) | ((uint32_t)b[4] << 4) | (b[5] >> 4), 20);
    c10 = signExtend(((uint32_t)(b[5] & 0x0F) << 16) | ((uint32_t)b[6] << 8) | b[7], 20);
    c01 = (int16_t)(((uint32_t)b[8] << 8) | b[9]);
    c11 = (int16_t)(((uint32_t)b[10] << 8) | b[11]);
    c20 = (int16_t)(((uint32_t)b[12] << 8) | b[13]);
    c21 = (int16_t)(((uint32_t)b[14] << 8) | b[15]);
    c30 = (int16_t)(((uint32_t)b[16] << 8) | b[17]);

    bool any = false;
    for (uint8_t v : b) if (v != 0x00 && v != 0xFF) { any = true; break; }
    return any;
}

static bool probe() {
    const uint8_t candidates[2] = { 0x77, 0x76 };
    for (uint8_t a : candidates) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() != 0) continue;
        address = a;
        uint8_t id = 0;
        if (!readRegs(REG_PROD_ID, &id, 1)) continue;
        Serial.printf("  probe 0x%02X -> PROD_ID 0x%02X\n", a, id);
        if (id == PROD_ID_DPS310) return true;
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(1200);   // let the USB-serial side attach before the banner
    Serial.println();
    Serial.println("=== DPS310 bench test ===");
    Serial.printf("I2C: SDA %d (SDI), SCL %d (SCK), %u Hz\n", PIN_SDA, PIN_SCL, I2C_HZ);

    Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
    Wire.setTimeOut(20);

    if (!probe()) {
        Serial.println("!! no DPS310 found on 0x77 or 0x76 — check SDA/SCL and power");
        while (true) delay(1000);
    }

    // Soft reset; the coefficient block is only valid afterwards.
    writeReg(REG_RESET, 0x89);
    delay(50);

    if (!readCoefficients()) {
        Serial.println("!! calibration block read failed");
        while (true) delay(1000);
    }
    Serial.printf("coef: c0=%d c1=%d c00=%ld c10=%ld c01=%d c11=%d c20=%d c21=%d c30=%d\n",
                  c0Half, c1, (long)c00, (long)c10, c01, c11, c20, c21, c30);

    // Internal temperature sensor (bit 7 clear = internal).
    writeReg(REG_TMP_COEF_SRCE, 0x00);

    // 128x pressure, 1x temperature.
    writeReg(REG_PRS_CFG, (uint8_t)((PRS_RATE << 4) | PRS_OSR));
    writeReg(REG_TMP_CFG, (uint8_t)((TMP_RATE << 4) | TMP_OSR));

    // Shift-enable is mandatory above 8x: without it the 24-bit result registers
    // overflow and every reading is nonsense.
    writeReg(REG_CFG_REG, (uint8_t)(1 << 2));   // P_SHIFT only; T_SHIFT not needed at 1x

    // Continuous pressure + temperature.
    writeReg(REG_MEAS_CFG, 0b111);

    Serial.printf("mode: pressure %dx oversampling @ %d Hz, temperature %dx @ %d Hz\n",
                  1 << PRS_OSR, 1 << PRS_RATE, 1 << TMP_OSR, 1 << TMP_RATE);
    Serial.println("expected: ~150 ms per pressure result (~6 Hz ceiling), polling PRS_RDY");
    Serial.println();
    Serial.println("   t_ms      p_raw     t_raw   pressure_hPa   temp_C     dt_ms   Hz");
}

void loop() {
    uint8_t status = 0;
    if (!readRegs(REG_MEAS_CFG, &status, 1)) {
        Serial.println("!! status read failed");
        delay(500);
        return;
    }

    // Wait for a genuinely new result: PRS_RDY (bit 4) and TMP_RDY (bit 5).
    if (!(status & 0x10) || !(status & 0x20)) {
        delay(2);
        return;
    }

    uint8_t p[3], t[3];
    if (!readRegs(REG_PSR_B2, p, 3) || !readRegs(REG_TMP_B2, t, 3)) {
        Serial.println("!! data read failed");
        delay(100);
        return;
    }

    const int32_t pRaw = signExtend(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2], 24);
    const int32_t tRaw = signExtend(((uint32_t)t[0] << 16) | ((uint32_t)t[1] << 8) | t[2], 24);

    // Compensation, datasheet form.
    const float tScaled = (float)tRaw / kScale[TMP_OSR];
    lastTempScaled = (float)c0Half + (float)c1 * tScaled;

    const float pScaled = (float)pRaw / kScale[PRS_OSR];
    const float pressure = (float)c00
                         + pScaled * ((float)c10 + pScaled * ((float)c20 + pScaled * (float)c30))
                         + tScaled * ((float)c01 + pScaled * ((float)c11 + pScaled * (float)c21));

    static uint32_t lastMs = 0;
    const uint32_t now = millis();
    const float dt = (lastMs == 0) ? 0.0f : (float)(now - lastMs);
    lastMs = now;

    Serial.printf("%8lu  %9ld  %8ld  %13.3f  %8.2f  %8.1f  %5.1f\n",
                  (unsigned long)now, (long)pRaw, (long)tRaw, pressure, lastTempScaled,
                  dt, dt > 0.0f ? 1000.0f / dt : 0.0f);
}
