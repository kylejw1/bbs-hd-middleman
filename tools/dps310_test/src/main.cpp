// DPS310 — temperature, pressure and altitude, calibrated for Calgary.
//
// The chip measures pressure and temperature; altitude is derived from pressure
// against a sea-level reference. That reference is what "set it to Calgary"
// means: the default 1013.25 hPa is a standard atmosphere, not Calgary's, so
// using it puts the altitude reading out by well over a hundred metres. Instead
// the sketch solves for the reference that makes the local pressure equal
// Calgary's known elevation, once, at startup.

#include <Adafruit_DPS310.h>

Adafruit_DPS310 dps;

// Can also use SPI!
#define DPS310_CS 10

// --- Calgary -----------------------------------------------------------------
// City of Calgary's published elevation. Downtown is around 1042 m and the
// airport around 1084 m, so this is a rough city datum -- put in your own
// location's elevation from a map if you want the absolute number to be right at
// your bench. Everything *relative* (the dH readout) is unaffected by the error.
//
// The sea-level reference is single-shot: a change in weather moves the absolute
// altitude by tens of metres, exactly as it does on any barometric altimeter.
// What it does not move is the difference between two readings minutes apart,
// which is what the slope maths actually uses.
static constexpr float CALGARY_ELEVATION_M = 1045.0f;

// Standard-atmosphere constant: the exponent in the barometric formula.
static constexpr float BARO_EXP = 0.1903f;

// How often the readout is printed. Sampling continues at the sensor's rate
// regardless; this only throttles the serial output.
static constexpr uint32_t PRINT_INTERVAL_MS = 500;

static float seaLevelHpa = 1013.25f;

static float altitudeFor(float pressureHpa) {
  return 44330.0f * (1.0f - powf(pressureHpa / seaLevelHpa, BARO_EXP));
}

/**
 * Work out the sea-level pressure that puts the current reading at Calgary's
 * elevation, by inverting the barometric formula:
 *
 *   P0 = P / (1 - h/44330)^(1/0.1903)
 *
 * Averaged over a couple of seconds because a single conversion is noisier than
 * the reference needs to be.
 */
static float calibrateSeaLevel() {
  Serial.println(F("Calibrating against Calgary..."));

  // Let the first conversions settle: the library reads whatever is in the
  // registers, so the first samples after configuring are stale.
  delay(1500);

  float sum = 0;
  int n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    if (dps.pressureAvailable()) {
      sensors_event_t te, pe;
      dps.getEvents(&te, &pe);
      if (pe.pressure > 300.0f && pe.pressure < 1100.0f) {
        sum += pe.pressure;
        n++;
      }
    }
    delay(2);
  }

  if (n == 0) {
    Serial.println(F("  no valid readings, keeping standard 1013.25 hPa"));
    return 1013.25f;
  }

  const float meanHpa = sum / n;
  const float denom = powf(1.0f - CALGARY_ELEVATION_M / 44330.0f, 1.0f / BARO_EXP);
  const float seaLevel = meanHpa / denom;

  Serial.printf("  mean pressure   %.3f hPa over %d samples\n", meanHpa, n);
  Serial.printf("  elevation datum %.0f m\n", CALGARY_ELEVATION_M);
  Serial.printf("  sea-level ref   %.3f hPa\n", seaLevel);
  return seaLevel;
}

// --- rolling 5 s altitude window --------------------------------------------
// 512 entries covers 2.5 s at up to 200 Hz, and 39 s at the ~13 Hz the part
// actually manages at 64x oversampling.
static constexpr uint32_t OFFSET_WINDOW_MS = 2500;
static constexpr int      RING             = 512;
// Reference must be at least this old, or the number would be measured over a
// fraction of a second and read as a movement that did not happen.
static constexpr uint32_t MIN_SPAN_MS      = 2000;

static float    ringAlt[RING];
static uint32_t ringMs[RING];
static int      ringHead  = 0;
static int      ringCount = 0;

static void pushSample(uint32_t nowMs, float altM) {
  ringAlt[ringHead] = altM;
  ringMs[ringHead]  = nowMs;
  ringHead = (ringHead + 1) % RING;
  if (ringCount < RING) ringCount++;
}

/**
 * Index of the oldest sample still worth using, or -1 while the window is still
 * filling. Pruning keeps the newest sample that is already older than the
 * window, so the reference sits as close to 5 s ago as the sample rate allows.
 */
static int oldestSample() {
  if (ringCount == 0) return -1;
  const uint32_t now = millis();
  int oldest = (ringHead - ringCount + RING) % RING;
  while (ringCount > 1) {
    const int next = (oldest + 1) % RING;
    if (now - ringMs[next] <= OFFSET_WINDOW_MS) break;
    oldest = next;
    ringCount--;
  }
  if (now - ringMs[oldest] < MIN_SPAN_MS) return -1;
  return oldest;
}

void setup() {
  Serial.begin(115200);
  while (!Serial)
    delay(10);

  Serial.println("DPS310");
  if (!dps.begin_I2C()) { // Can pass in I2C address here
    // if (! dps.begin_SPI(DPS310_CS)) {  // If you want to use SPI
    Serial.println("Failed to find DPS");
    while (1)
      yield();
  }
  Serial.println("DPS OK!");

  dps.configurePressure(DPS310_64HZ, DPS310_64SAMPLES);
  dps.configureTemperature(DPS310_64HZ, DPS310_64SAMPLES);

  seaLevelHpa = calibrateSeaLevel();
  Serial.println();
}

void loop() {
  sensors_event_t temp_event, pressure_event;

  while (!dps.temperatureAvailable() || !dps.pressureAvailable()) {
    return; // wait until there's something to read
  }

  dps.getEvents(&temp_event, &pressure_event);

  // Altitude from the calibrated reference, computed for every sample so the
  // 5 s window is fed at the sensor's own rate...
  const float altitudeM = altitudeFor(pressure_event.pressure);
  const uint32_t now = millis();
  pushSample(now, altitudeM);

  // ...but printed only twice a second. The window needs the full-rate samples;
  // the eye does not.
  static uint32_t lastPrintMs = 0;
  if (now - lastPrintMs < PRINT_INTERVAL_MS) return;
  lastPrintMs = now;

  Serial.print(F("Temperature = "));
  Serial.print(temp_event.temperature);
  Serial.println(" *C");

  Serial.print(F("Pressure = "));
  Serial.print(pressure_event.pressure);
  Serial.println(" hPa");

  Serial.printf("Altitude = %.1f m\n", altitudeM);

  // Rolling 2.5 s offset: how far the altitude has moved relative to where it was
  // 2.5 seconds ago. This is a sliding window, not a baseline that resets -- so
  // it answers "am I climbing right now?" continuously, and a slow drift cancels
  // instead of accumulating.
  const int oldest = oldestSample();
  if (oldest < 0) {
    Serial.println(F("  dH(2.5s) =   -- cm   (filling window)"));
  } else {
    Serial.printf("  dH(2.5s) = %+.2f cm over %.2f s\n",
                  (altitudeM - ringAlt[oldest]) * 100.0f,
                  (now - ringMs[oldest]) / 1000.0f);
  }

  Serial.println();
}
