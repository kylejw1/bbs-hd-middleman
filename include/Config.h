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

#if defined(LCD_DISPLAY_ST7789)
// ST7789 at 76×284 in landscape = 284 wide × 76 tall.
// Rotation 1 maps the short edge (76) to height and long edge (284) to width.
#define LCD_ROTATION        1
#define LCD_DISPLAY_NAME    "ST7789"

#elif defined(LCD_DISPLAY_GC9A01)
// GC9A01 240×240 round. No rotation needed (it's a circle).
#define LCD_ROTATION        0
#define LCD_DISPLAY_NAME    "GC9A01"
#endif

#endif // CONFIG_H
