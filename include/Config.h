#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// HARDWARE PIN ASSIGNMENTS
// =============================================================================
// Defaults are for the 44-pin ESP32-S3 DevKitC-1. Every pin below is
// #ifndef-guarded so a different board can override them from its own PlatformIO
// environment with -D flags, without editing this file -- see [env:gc9a01-esp32]
// in platformio.ini for the classic-ESP32 mapping.
//
// Note: ESP32 GPIOs operate at 3.3V logic levels.
// Bafang UART communication is 5V TTL logic.
// A bi-directional logic level shifter (or resistor voltage divider on RX lines)
// is recommended between the ESP32 and the Bafang 5V lines.

// Controller UART (UART1) - connects to Bafang BBS-HD controller
#define CONTROLLER_UART_NUM     1
#ifndef CONTROLLER_RX_PIN
#define CONTROLLER_RX_PIN       17   // Connect to Controller TX (via level shifter)
#endif
#ifndef CONTROLLER_TX_PIN
#define CONTROLLER_TX_PIN       18   // Connect to Controller RX (via level shifter)
#endif

// Display UART (UART2) - connects to Bafang HMI Display (500C, 850C, DPC-18, etc.)
#define DISPLAY_UART_NUM        2
#ifndef DISPLAY_RX_PIN
#define DISPLAY_RX_PIN          15   // Connect to Display TX (via level shifter)
#endif
#ifndef DISPLAY_TX_PIN
#define DISPLAY_TX_PIN          16   // Connect to Display RX (via level shifter)
#endif

// Optional Status / Activity LED.
// Override with -DSTATUS_LED_PIN=<n>, or define NO_STATUS_LED to compile the
// heartbeat out entirely on boards with no free LED pin.
#if !defined(STATUS_LED_PIN) && !defined(NO_STATUS_LED)
#define STATUS_LED_PIN          48   // On-board RGB / status LED on ESP32-S3 DevKit
#endif

// Standard Bafang UART baud rate: 1200 baud, 8 data bits, no parity, 1 stop bit (8N1)
#define BAFANG_BAUD_RATE        1200

// Debug USB Serial baud rate
#define DEBUG_SERIAL_BAUD       115200

// Serial trace ring buffer / web Debug Console.
// Disabled by default: capturing every UART byte costs CPU/RAM and the web UI
// polling /api/serial-trace floods a weak Wi-Fi link. Enable it from the
// Debug Console tab only while actively diagnosing the bus.
#define DEBUG_TRACE_ENABLED_DEFAULT  0

// =============================================================================
// WI-FI CONFIGURATION
// =============================================================================
// Default SoftAP credentials
#define DEFAULT_AP_SSID         "BBS-FW-Middleman"
#define DEFAULT_AP_PASS         "bafang1234"   // Set to "" for open network
#define DEFAULT_AP_CHANNEL      6              // fallback only; the AP scans for a quiet channel
#define DEFAULT_AP_MAX_CONN     4

// mDNS hostname: the dashboard is reachable at http://<MDNS_HOSTNAME>.local/
// in both station and access-point modes, so you never have to remember an IP.
#define MDNS_HOSTNAME           "bbshd"

// Station-first Wi-Fi management:
//  * Try the configured router first (STA-only, best throughput).
//  * Fall back to our own SoftAP if the join does not complete in time.
//  * Drop the SoftAP once the station link is up (avoids single-radio AP+STA contention).
//  * If the station link later drops, wait out a grace period before falling back.
#define STA_CONNECT_TIMEOUT_MS  12000
#define STA_LOST_GRACE_MS       45000
#define AP_SCAN_TIMEOUT_MS      4000

// Captive Portal DNS port
#define DNS_PORT                53

// Web Server HTTP port
#define HTTP_PORT               80

// =============================================================================
// MIDDLEMAN ARBITRATION TIMING
// =============================================================================
// Time in ms of bus silence before sending intercepted requests to controller
#define BUS_QUIET_TIME_MS       40

// Timeout waiting for controller response during config transactions
#define CONTROLLER_TIMEOUT_MS   2500

// The controller emits event/telemetry frames asynchronously, written back to
// back. Before a config transaction we wait for the line to be idle for this
// long so we are certainly at a frame boundary; one byte at 1200 baud takes
// 8.33 ms, so a 25 ms gap cannot be in the middle of a frame.
#define CONTROLLER_FRAME_QUIET_MS         25
#define CONTROLLER_FRAME_IDLE_TIMEOUT_MS  1000

// Maximum queued display write commands while in config mode
#define MAX_QUEUED_DISPLAY_CMDS 8

// Maximum event log entries stored in RAM
#define MAX_EVENT_LOG_ENTRIES   60

// Firmware version string (displayed in web UI and used for OTA validation)
#define FW_VERSION              "1.0.1"

// =============================================================================
// BLUETOOTH LE TRANSPORT
// =============================================================================
// The dashboard is normally served by the ESP32 itself, over its SoftAP or your
// LAN, and that copy needs no Bluetooth at all. But a page loaded over HTTPS
// cannot reach http://192.168.4.1 (mixed content), and Web Bluetooth requires a
// secure context the ESP32 cannot provide -- so the GitHub Pages copy of the
// dashboard talks to the bike over BLE GATT instead.
//
// That BLE link is a convenience, never a replacement: the on-device copy is
// what keeps working on iOS Safari, where Web Bluetooth does not exist.
//
// Set to 0 to build without Bluedroid (saves roughly 600 KB flash / 30 KB RAM).
//
// ON BY DEFAULT, in every environment. It used to be gated behind its own
// [env:ble] because the Bluedroid coexistence abort killed setup() outright and
// the display threw a communication error. That is fixed by the init ordering in
// setup() (Bluetooth before Wi-Fi) together with applyWifiPowerSave() in
// WebPortal, so the transport now ships everywhere and there is no longer a
// second environment that can be flashed over the working firmware by mistake.
//
// The #ifndef is kept so a one-off BLE-free build remains possible:
//
//     PLATFORMIO_BUILD_FLAGS="-DBLE_TRANSPORT_ENABLED=0" pio run -e <env> -t upload
//
// PlatformIO does not reliably notice a changed build flag, so clear
// .pio/build/<env> first or the stale BLE image is what gets flashed.
#ifndef BLE_TRANSPORT_ENABLED
#define BLE_TRANSPORT_ENABLED   1
#endif

#define BLE_DEVICE_NAME         "BBSHD-Middleman"

// Bump whenever the GATT frame layout below changes. The dashboard refuses to
// talk to a device whose protocol version it does not understand.
#define BLE_PROTOCOL_VERSION    1

// Largest request or response body accepted over BLE. The v6 config JSON is the
// biggest payload at roughly 3 KB, so this leaves generous headroom.
#define BLE_MAX_FRAME_BODY      6144

// Preferred ATT MTU. The ESP32-S3 supports up to 517; the value actually in
// effect is whatever the phone negotiates, and notify chunking follows it.
#define BLE_PREFERRED_MTU       517

// Cadence of the unsolicited telemetry push, in milliseconds. Only sent while a
// client is connected, and it replaces the dashboard's 1.5 s HTTP poll entirely.
// The event log is pushed on change instead (see TelemetryTracker::getEventSeq).
#define BLE_TELEMETRY_PUSH_MS   1000

// Bluetooth PIN policy. The PIN itself lives in NVS (see WebPortal::getBlePin);
// an empty PIN leaves the BLE API open. BLE range is only a few metres, so this
// backoff exists to make a short PIN tedious to guess rather than to stop a
// remote attacker. The counter deliberately survives a disconnect, so dropping
// the link does not buy a fresh budget of attempts.
#define BLE_AUTH_MAX_ATTEMPTS   5
#define BLE_AUTH_LOCKOUT_MS     30000

// =============================================================================
// LCD DISPLAY (optional)
// =============================================================================
// Compile in one of the two TFT targets by setting a build flag in
// platformio.ini (see the [env:st7789] / [env:gc9a01] environments):
//
//   -DLCD_DISPLAY_ST7789   — 2.25" strip, 76×284, landscape → 284×76
//   -DLCD_DISPLAY_GC9A01   — 1.28" round, 240×240
//
// Neither defined → LcdDisplay stubs out to empty calls, zero overhead.
//
// -----------------------------------------------------------------------------
// SPI PINS ARE NOT SET HERE. They live in platformio.ini as TFT_eSPI flags:
//
//     -DTFT_MOSI=11  -DTFT_SCLK=12  -DTFT_CS=10  -DTFT_DC=9  -DTFT_RST=8
//     -DSPI_FREQUENCY=40000000   (SPI clock; GC9A01 uses 27000000)
//
// TFT_eSPI reads those at library compile time, so a plain #define in this file
// could never reach it. There used to be LCD_*_PIN defines here that looked like
// the pin configuration and were referenced nowhere -- editing them silently did
// nothing. Change the -DTFT_* flags in the environment you are building, or add
// a new environment with your own pin set.
// -----------------------------------------------------------------------------
// Backlight: neither panel is driven from here. Wire BL to 3V3 for always-on, or
// add a -DTFT_BL=<gpio> flag and call ledcWrite() yourself for PWM dimming.
// -----------------------------------------------------------------------------

// How often the display is refreshed (ms). 200 ms = 5 fps is plenty for a bike.
#ifndef LCD_REFRESH_MS
#define LCD_REFRESH_MS  200
#endif

// Maximum cadence RPM shown on the X axis of the PAS curve chart.
// 120 is the BBSHD nominal max; set lower if your cadence sensor reads lower.
#ifndef LCD_CURVE_MAX_CADENCE_RPM
#define LCD_CURVE_MAX_CADENCE_RPM  120
#endif

// Which page the round display shows, and whether it alternates.
//
//   0 = bike telemetry page (PAS curve, cadence, slope, targets)
//   1 = IMU readout page (accel / gyro / temperature / bubble level)
//
// There is no room for both at once on a 240 px circle, so LCD_START_PAGE picks
// the one the display sits on. It sits on the bike page: the curve and the two
// targets are what the rider can act on, and the slope now has its own numeric
// tile there, so the IMU page is the one that can afford to be hidden.
//
// LCD_PAGE_MS is how long to hold each page before switching. **0 disables the
// alternating entirely** and pins the display to LCD_START_PAGE. The cycling
// code is all still there and still correct -- it just never fires. Set it
// non-zero to bring the alternation back.
#ifndef LCD_START_PAGE
#define LCD_START_PAGE  0
#endif
#ifndef LCD_PAGE_MS
#define LCD_PAGE_MS     0
#endif

// The IMU page draws a live bubble level, so it refreshes faster than the bike
// page's LCD_REFRESH_MS.
#ifndef LCD_IMU_REFRESH_MS
#define LCD_IMU_REFRESH_MS  100
#endif

#if defined(LCD_DISPLAY_ST7789)
// ST7789 at 76×284 in landscape = 284 wide × 76 tall.
// Rotation 1 maps the short edge (76) to height and long edge (284) to width.
#define LCD_ROTATION        1
#define LCD_DISPLAY_NAME    "ST7789"

#elif defined(LCD_DISPLAY_GC9A01)
// GC9A01 240x240 round. "It's a circle, so it needs no rotation" is wrong about
// the thing that actually matters: the circle is rotation-invariant, but the
// readout inside it is not. This sets which way up the rider sees it.
//
// 1 = 90 degrees clockwise from the panel's native orientation. Because the
// panel is square, W and H are unchanged and none of the layout constants need
// adjusting -- only the content turns.
#define LCD_ROTATION        1
#define LCD_DISPLAY_NAME    "GC9A01"
#endif

// =============================================================================
// MPU-6050 ACCELEROMETER / GYRO (optional, I2C)
// =============================================================================
// The GY-521 breakout carries an MPU-6050, which is I2C ONLY — there is no SPI
// on that board. (The SPI-capable part is the MPU-6000, which a GY-521 does not
// expose.) Nothing else in this firmware uses I2C, so the bus is free.
//
// ON BY DEFAULT: the driver costs a few KB of flash and nothing at all when no
// sensor answers — the display just keeps showing the bike page. Set to 0 to
// compile it out entirely.
//
// -----------------------------------------------------------------------------
// PINS: 13/14 are free on BOTH supported boards, and are deliberately NOT the
// Arduino defaults. Wire defaults to SDA 21 / SCL 22 on a classic ESP32 and
// SDA 8 / SCL 9 on an S3 — on this project those are the LCD's CS/SCLK and
// RST/DC respectively. Wire.begin() is therefore always called with explicit
// pins; never call the argument-less form.
// -----------------------------------------------------------------------------
// POWER THE GY-521 FROM 3V3, NOT 5V. Most breakouts carry I2C pull-ups, and on
// some clones they are tied to VCC rather than the regulator output — feed it 5V
// and you put 5V on SDA/SCL, which damages the ESP32. At 3V3 it is safe either
// way, and no level shifter is needed (unlike the 5V Bafang UARTs).
//
// ADO selects the address (GND or floating → 0x68, VCC → 0x69). XDA/XCL are for
// an external magnetometer and are left unconnected.
// -----------------------------------------------------------------------------
#ifndef MPU6050_ENABLED
#define MPU6050_ENABLED     1
#endif

#if MPU6050_ENABLED
#ifndef MPU_SDA_PIN
#define MPU_SDA_PIN         13
#endif
#ifndef MPU_SCL_PIN
#define MPU_SCL_PIN         14
#endif
// Data-ready interrupt. The driver polls for now; the pin is reserved so the
// GY-521's INT pad has a documented destination.
#ifndef MPU_INT_PIN
#define MPU_INT_PIN         19
#endif
// Drop to 100000 if the wiring is long or the bus proves unreliable.
#ifndef MPU_I2C_HZ
#define MPU_I2C_HZ          400000
#endif

// Full-scale ranges. ±4g / ±500 dps is the compromise for a bike: ±2g resolves
// better but road and motor vibration can clip it.
#ifndef MPU_ACCEL_FS
#define MPU_ACCEL_FS        4       // 2, 4, 8 or 16 (g)
#endif
#ifndef MPU_GYRO_FS
#define MPU_GYRO_FS         500     // 250, 500, 1000 or 2000 (deg/s)
#endif

// Sensor poll rate. The complementary filter integrates the gyro at this rate,
// so it is also the effective filter update rate.
#ifndef MPU_READ_HZ
#define MPU_READ_HZ         100
#endif

// How often the middleman broadcasts inclination (pitch & roll) to the
// motor controller over UART (ms). 500 ms (2 Hz) matches bbs-fw's 0xEC
// telemetry cadence and consumes ~12% of the 1200 baud link. Set to 0 to disable.
#ifndef MPU_PITCH_BROADCAST_MS
#define MPU_PITCH_BROADCAST_MS 500
#endif

// DEBUG: Temporarily substitute the pitch angle (degrees) in place of the
// wheel speed reading sent to the HMI display. The display expects motor RPM
// which it converts to km/h using wheel circumference; we back-calculate the
// RPM that would produce a display reading equal to the pitch angle value in
// km/h units (e.g. 15.2 degrees -> display shows 15.2 km/h).
// Set to 0 to restore normal speed display.
#ifndef MPU_PITCH_AS_SPEED_DISPLAY
// Debug aid: substitutes the tilt angle for the speed the display shows, by
// back-calculating the RPM that would render it. Left on by mistake once and it
// read as thousands of km/h whenever the estimate ran away, because the value is
// clamped by a uint16 RPM rather than by anything physical. Off for real use.
#define MPU_PITCH_AS_SPEED_DISPLAY 0
#endif

// -----------------------------------------------------------------------------
// Pitch estimator tuning -- implements bike_pitch_algorithm.md
//
// Adaptive-gain complementary filter: the gyro propagates attitude every
// sample, and the accelerometer corrects it with a confidence weight that
// collapses toward zero whenever the accelerometer cannot be trusted
// (accelerating, braking, rough road). At zero weight the estimate is pure gyro
// integration, which is immune to linear acceleration by construction.
// -----------------------------------------------------------------------------

// Proportional gain on the accelerometer correction. 0.03 at 100 Hz is a ~0.3 s
// time constant during steady riding.
#ifndef MPU_KP
#define MPU_KP              0.03f
#endif

// Integral gain for the online gyro bias estimate. Deliberately tiny: it tracks
// thermal drift over minutes and must not chase the pitch itself.
#ifndef MPU_KI
#define MPU_KI              0.002f
#endif

// Normalised deviation of |f| from 1 g at which the magnitude weight has decayed
// to exp(-0.5) ~ 0.61. 0.08 is about 0.8 m/s^2.
#ifndef MPU_SIGMA_NORM
#define MPU_SIGMA_NORM      0.08f
#endif

// Longitudinal residual gate, m/s^2. Below MPU_TAU_LOW the accelerometer is
// fully trusted; above MPU_TAU_HIGH it is ignored entirely.
//
// This is the term that actually catches forward acceleration. The magnitude
// weight above is nearly blind to it: horizontal acceleration shifts |f| by
// only A^2/2g (2% at 2 m/s^2) while shifting f_x by A. The residual compares
// f_x against the gravity component implied by the current pitch estimate, so
// it sees A directly.
#ifndef MPU_TAU_LOW
#define MPU_TAU_LOW         0.30f
#endif
#ifndef MPU_TAU_HIGH
#define MPU_TAU_HIGH        1.20f
#endif

// Gyro magnitude (rad/s) below which the bike counts as quiescent and the
// online bias integrator is allowed to run. 0.05 rad/s is about 2.8 deg/s.
#ifndef MPU_GYRO_QUIET
#define MPU_GYRO_QUIET      0.05f
#endif

// Hard limit on the online gyro bias estimate, deg/s. A real bias is a fraction
// of a degree per second, so this is purely a backstop: the bias integrator is
// the one state that can run away, and a sign slip there drives the pitch to
// +/-90 degrees and the grade readout into the thousands of percent before
// anything else looks wrong. Clamping makes that fail visible instead of absurd.
#ifndef MPU_BIAS_LIMIT_DPS
#define MPU_BIAS_LIMIT_DPS  5.0f
#endif

// Gravity, m/s^2.
#ifndef MPU_GRAVITY
#define MPU_GRAVITY         9.80665f
#endif

// Sign of the pitch rate about the body Y axis.
//
// With the document's frame -- X forward, Y left, Z up, right-handed -- a
// nose-up rotation is NEGATIVE about +Y: rotating about +Y carries +X toward
// -Z, which is nose down. The document's theta_dot = +omega_y therefore
// disagrees with its own accelerometer convention (f_x = +g sin(theta) for nose
// up, which is correct and which this driver follows). The gyro term is negated
// here so the two agree; with the document's sign they fight each other, and
// the estimate runs the wrong way exactly when the accelerometer is gated off.
//
// Flip to +1.0 if your sensor is mounted with Y to the right instead of left.
#ifndef MPU_PITCH_GYRO_SIGN
#define MPU_PITCH_GYRO_SIGN (-1.0f)
#endif

// Complementary filter weight for roll. The document specifies only the pitch
// estimator, so roll keeps a plain fast filter: lateral acceleration on a
// bicycle is small, so its accelerometer reference is trustworthy. 0.98 is
// ~0.5 s at 100 Hz.
#ifndef MPU_ROLL_ALPHA
#define MPU_ROLL_ALPHA      0.98f
#endif

// Samples averaged at boot to measure gyro bias, the 1 g reference and the
// starting attitude. The bike must be STATIONARY; a warning is logged if the
// gyro spread suggests it was not. Uncancelled bias is precisely what makes the
// pitch drift during acceleration, because that is when the filter is flying on
// the gyro alone. Runs in setup() and takes well under a second.
#ifndef MPU_CAL_SAMPLES
#define MPU_CAL_SAMPLES     200
#endif

// Peak-to-peak gyro spread (deg/s) above which the boot calibration is assumed
// to have happened while the bike was moving.
#ifndef MPU_CAL_MAX_SPREAD_DPS
#define MPU_CAL_MAX_SPREAD_DPS  5.0f
#endif
#endif // MPU6050_ENABLED

#endif // CONFIG_H
