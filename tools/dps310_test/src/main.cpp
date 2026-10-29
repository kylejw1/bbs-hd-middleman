// DPS310 bench test — ESP32 DevKit V1, on Adafruit's DPS310 library.
//
// Replaces the hand-written register driver so there is one implementation to
// trust rather than two to keep in step. The library handles the coefficient
// read, the compensation scaling and the shift-enable bit for high oversampling;
// the only decisions left here are the mode and the wiring.
//
// Wiring found on the bench: the part answers on SDA 21 / SCL 22, which is the
// reverse of the module's SDI/SCK labelling. Both orders are tried and the one
// with a bus on it is reported, so a mis-wired sensor diagnoses itself.

#include <Arduino.h>
#include <Adafruit_DPS310.h>
#include <Wire.h>

static constexpr int PIN_A = 22;    // first attempt: SDA 22 / SCL 21
static constexpr int PIN_B = 21;
static constexpr uint32_t I2C_HZ = 400000;

Adafruit_DPS310 dps;
TwoWire i2c(0);

// Anything answering in the sensor range, so a wiring fault is distinguishable
// from a missing sensor.
static int scanBus() {
    int found = -1;
    Serial.print("  scan:");
    for (uint8_t a = 0x08; a < 0x78; a++) {
        i2c.beginTransmission(a);
        if (i2c.endTransmission() == 0) {
            Serial.printf(" 0x%02X", a);
            if (found < 0 && (a == 0x76 || a == 0x77)) found = a;
        }
    }
    if (found < 0) Serial.print(" (no DPS310)");
    Serial.println();
    return found;
}

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println();
    Serial.println("=== DPS310 bench test (Adafruit library) ===");

    int addr = -1;
    for (int attempt = 0; attempt < 2 && addr < 0; attempt++) {
        const int sda = (attempt == 0) ? PIN_A : PIN_B;
        const int scl = (attempt == 0) ? PIN_B : PIN_A;
        if (attempt == 1) Serial.println("  retrying with SDA/SCL swapped");
        Serial.printf("I2C on SDA %d, SCL %d\n", sda, scl);

        i2c.end();
        i2c.begin(sda, scl, I2C_HZ);
        i2c.setTimeOut(20);
        delay(20);
        addr = scanBus();
    }
    if (addr < 0) {
        Serial.println("!! no DPS310 on either pin order — check 3V3, GND and the jumpers");
        while (true) delay(1000);
    }

    if (!dps.begin_I2C(addr, &i2c)) {
        Serial.println("!! begin_I2C failed");
        while (true) delay(1000);
    }

    // Highest pressure precision the part offers. The library sets the shift bit
    // for oversampling above 8x, which is mandatory there.
    dps.configurePressure(DPS310_4HZ, DPS310_128SAMPLES);
    dps.configureTemperature(DPS310_1HZ, DPS310_1SAMPLE);
    dps.setMode(DPS310_CONT_PRESTEMP);

    Serial.println("mode: pressure 128x @ 4 Hz, temperature 1x @ 1 Hz");
    Serial.println();
    Serial.println("   t_ms   pressure_hPa   temp_C     dt_ms   Hz");
}

void loop() {
    if (!dps.pressureAvailable()) {
        delay(2);
        return;
    }

    sensors_event_t pe, te;
    dps.getEvents(&te, &pe);

    static uint32_t lastMs = 0;
    const uint32_t now = millis();
    const float dt = (lastMs == 0) ? 0.0f : (float)(now - lastMs);
    lastMs = now;

    Serial.printf("%8lu  %12.3f  %7.2f  %8.1f  %5.1f\n",
                  (unsigned long)now, pe.pressure, te.temperature, dt,
                  dt > 0.0f ? 1000.0f / dt : 0.0f);
}
