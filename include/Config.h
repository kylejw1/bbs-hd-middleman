#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// HARDWARE PIN ASSIGNMENTS (44-Pin ESP32-S3 DevKitC-1)
// =============================================================================
// Note: ESP32-S3 GPIOs operate at 3.3V logic levels.
// Bafang UART communication is 5V TTL logic.
// A bi-directional logic level shifter (or resistor voltage divider on RX lines)
// is recommended between the ESP32-S3 and the Bafang 5V lines.

// Controller UART (UART1) - connects to Bafang BBS-HD controller
#define CONTROLLER_UART_NUM     1
#define CONTROLLER_RX_PIN       17   // Connect to Controller TX (via level shifter)
#define CONTROLLER_TX_PIN       18   // Connect to Controller RX (via level shifter)

// Display UART (UART2) - connects to Bafang HMI Display (500C, 850C, DPC-18, etc.)
#define DISPLAY_UART_NUM        2
#define DISPLAY_RX_PIN          15   // Connect to Display TX (via level shifter)
#define DISPLAY_TX_PIN          16   // Connect to Display RX (via level shifter)

// Optional Status / Activity LED
#define STATUS_LED_PIN          48   // On-board RGB / status LED on ESP32-S3 DevKit

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

// Maximum queued display write commands while in config mode
#define MAX_QUEUED_DISPLAY_CMDS 8

// Maximum event log entries stored in RAM
#define MAX_EVENT_LOG_ENTRIES   60

// Firmware version string (displayed in web UI and used for OTA validation)
#define FW_VERSION              "1.0.1"

#endif // CONFIG_H
