#ifndef BBS_FW_PROTOCOL_H
#define BBS_FW_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>

// =============================================================================
// PROTOCOL CONSTANTS & OPCODES
// =============================================================================

// High-level request types
constexpr uint8_t REQUEST_TYPE_READ             = 0x01;
constexpr uint8_t REQUEST_TYPE_WRITE            = 0x02;
constexpr uint8_t REQUEST_TYPE_BAFANG_READ      = 0x11;
constexpr uint8_t REQUEST_TYPE_BAFANG_WRITE     = 0x16;

// Config tool opcodes
constexpr uint8_t OPCODE_READ_FW_VERSION        = 0x01;
constexpr uint8_t OPCODE_READ_EVTLOG_ENABLE     = 0x02;
constexpr uint8_t OPCODE_READ_CONFIG            = 0x03;
constexpr uint8_t OPCODE_READ_STATUS            = 0x04;

constexpr uint8_t OPCODE_WRITE_EVTLOG_ENABLE    = 0xf0;
constexpr uint8_t OPCODE_WRITE_CONFIG           = 0xf1;
constexpr uint8_t OPCODE_WRITE_RESET_CONFIG     = 0xf2;
constexpr uint8_t OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION = 0xf3;

// Bafang standard display opcodes
constexpr uint8_t OPCODE_DISPLAY_READ_STATUS    = 0x08;
constexpr uint8_t OPCODE_DISPLAY_READ_CURRENT   = 0x0a;
constexpr uint8_t OPCODE_DISPLAY_READ_BATTERY   = 0x11;
constexpr uint8_t OPCODE_DISPLAY_READ_SPEED     = 0x20;
constexpr uint8_t OPCODE_DISPLAY_READ_UNKNOWN1  = 0x21;
constexpr uint8_t OPCODE_DISPLAY_READ_RANGE     = 0x22;
constexpr uint8_t OPCODE_DISPLAY_READ_CALORIES  = 0x24;
constexpr uint8_t OPCODE_DISPLAY_READ_UNKNOWN3  = 0x25;
constexpr uint8_t OPCODE_DISPLAY_READ_MOVING    = 0x31;

constexpr uint8_t OPCODE_DISPLAY_WRITE_PAS      = 0x0b;
constexpr uint8_t OPCODE_DISPLAY_WRITE_MODE     = 0x0c;
constexpr uint8_t OPCODE_DISPLAY_WRITE_LIGHTS   = 0x1a;
constexpr uint8_t OPCODE_DISPLAY_WRITE_SPEED_LIM= 0x1f;

// Event Log frame types
constexpr uint8_t EVENT_LOG_ENTRY               = 0xee;
constexpr uint8_t EVENT_LOG_DATA_ENTRY          = 0xed;

// Assist Flags
constexpr uint8_t ASSIST_FLAG_PAS               = 0x01;
constexpr uint8_t ASSIST_FLAG_THROTTLE          = 0x02;
constexpr uint8_t ASSIST_FLAG_CRUISE            = 0x04;
constexpr uint8_t ASSIST_FLAG_PAS_VARIABLE      = 0x08;
constexpr uint8_t ASSIST_FLAG_PAS_TORQUE        = 0x10;
constexpr uint8_t ASSIST_FLAG_OVERRIDE_CADENCE  = 0x20;
constexpr uint8_t ASSIST_FLAG_OVERRIDE_SPEED    = 0x40;

// Config sizes
constexpr uint8_t BBS_FW_CONFIG_VERSION         = 5;
constexpr size_t  BBS_FW_CONFIG_V5_SIZE         = 154;

// Controller Types
enum class ControllerType : uint8_t {
    Unknown = 0,
    BBSHD   = 1,
    BBS02   = 2,
    TSDZ2   = 3
};

// =============================================================================
// DATA STRUCTURES (Exact binary layout matching bbs-fw firmware)
// =============================================================================

#pragma pack(push, 1)

struct AssistLevel {
    uint8_t flags;
    uint8_t target_current_percent;
    uint8_t max_throttle_current_percent;
    uint8_t max_cadence_percent;
    uint8_t max_speed_percent;
    uint8_t torque_amplification_factor_x10;
};

struct BbsFwConfigV5 {
    // Units
    uint8_t use_freedom_units;               // 0 = metric (km/h), 1 = imperial (mph)

    // Global electrical & motor
    uint8_t max_current_amps;                // 5 - 33A
    uint8_t current_ramp_amps_s;             // 1 - 255 A/s
    uint8_t max_battery_x100v_u16l;          // Max battery volts x100 (low byte)
    uint8_t max_battery_x100v_u16h;          // Max battery volts x100 (high byte)
    uint8_t low_cut_off_v;                   // Low voltage cutoff in Volts (e.g. 41V)
    uint8_t max_speed_kph;                   // Max speed in km/h

    // External sensors & accessories
    uint8_t use_speed_sensor;                // 0 = disabled, 1 = enabled
    uint8_t use_shift_sensor;                // 0 = disabled, 1 = enabled
    uint8_t use_push_walk;                   // 0 = disabled, 1 = enabled
    uint8_t use_temperature_sensor;          // 0 = none, 1 = controller, 2 = motor, 3 = all
    uint8_t lights_mode;                     // 0 = default, 1 = disabled, 2 = always on, 3 = brake light
    uint8_t use_pretension;                  // 0 = disabled, 1 = enabled
    uint8_t pretension_speed_cutoff_kph;     // Cutoff speed in km/h

    // Speed sensor
    uint8_t wheel_size_inch_x10_u16l;        // Wheel size in inches x10 (low byte, e.g. 275 = 27.5")
    uint8_t wheel_size_inch_x10_u16h;        // Wheel size in inches x10 (high byte)
    uint8_t speed_sensor_signals;            // Pulses per wheel revolution (typically 1)

    // Pedal Assist (PAS)
    uint8_t pas_start_delay_pulses;          // Delay in magnet pulses before motor starts (0 - 24)
    uint8_t pas_stop_delay_x100s;            // Stop delay in units of 10ms (e.g. 20 = 200ms)
    uint8_t pas_keep_current_percent;        // Keep current when cadence drops (10 - 100%)
    uint8_t pas_keep_current_cadence_rpm;    // Cadence threshold for keep current (0 - 255 rpm)

    // Throttle
    uint8_t throttle_start_voltage_mv_u16l;  // Throttle start threshold in mV (low byte)
    uint8_t throttle_start_voltage_mv_u16h;  // Throttle start threshold in mV (high byte)
    uint8_t throttle_end_voltage_mv_u16l;    // Throttle wide-open threshold in mV (low byte)
    uint8_t throttle_end_voltage_mv_u16h;    // Throttle wide-open threshold in mV (high byte)
    uint8_t throttle_start_percent;          // Initial power kick % on throttle touch (0 - 100%)
    uint8_t throttle_global_spd_lim_opt;     // 0 = disabled, 1 = enabled, 2 = standard levels
    uint8_t throttle_global_spd_lim_percent; // Speed limit % for throttle

    // Shift sensor interrupt
    uint8_t shift_interrupt_duration_ms_u16l;// Cut duration in ms (low byte)
    uint8_t shift_interrupt_duration_ms_u16h;// Cut duration in ms (high byte)
    uint8_t shift_interrupt_current_threshold_percent; // Current threshold during shift cut %

    // Display walk mode field
    uint8_t walk_mode_data_display;          // 0 = speed, 1 = temp, 2 = power, 3 = battery %

    // Assist mode options
    uint8_t assist_mode_select;              // 0 = off, 1 = standard, 2 = lights, etc.
    uint8_t assist_startup_level;            // Startup PAS level (0 - 9)

    // 10 Standard assist levels (0 - 9)
    AssistLevel standard_levels[10];

    // 10 Sport assist levels (0 - 9)
    AssistLevel sport_levels[10];
};

#pragma pack(pop)

// =============================================================================
// PROTOCOL UTILITIES & HELPERS
// =============================================================================

inline uint8_t computeChecksum(const uint8_t* buf, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += buf[i];
    }
    return sum;
}

inline bool verifyChecksum(const uint8_t* buf, size_t length) {
    if (length < 2) return false;
    return computeChecksum(buf, length - 1) == buf[length - 1];
}

void initDefaultBbsHdConfig(BbsFwConfigV5& cfg);
bool serializeConfigToJson(const BbsFwConfigV5& cfg, JsonDocument& doc);
bool deserializeConfigFromJson(const JsonDocument& doc, BbsFwConfigV5& cfg);
String getEventDescription(uint8_t eventId, int16_t data, bool hasData);
const char* getControllerTypeName(ControllerType type);

#endif // BBS_FW_PROTOCOL_H
