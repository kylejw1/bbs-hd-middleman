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
// Multi-value debug telemetry (bbs-fw 0xEC): header, target current %,
// target speed %, cadence rpm x10 (hi/lo), checksum.
constexpr uint8_t EVENT_LOG_TELEMETRY_ENTRY     = 0xec;
constexpr size_t  EVENT_LOG_TELEMETRY_SIZE      = 6;

// Assist Flags
constexpr uint8_t ASSIST_FLAG_PAS               = 0x01;
constexpr uint8_t ASSIST_FLAG_THROTTLE          = 0x02;
constexpr uint8_t ASSIST_FLAG_CRUISE            = 0x04;
constexpr uint8_t ASSIST_FLAG_PAS_VARIABLE      = 0x08;
constexpr uint8_t ASSIST_FLAG_PAS_TORQUE        = 0x10;
constexpr uint8_t ASSIST_FLAG_OVERRIDE_CADENCE  = 0x20;
constexpr uint8_t ASSIST_FLAG_OVERRIDE_SPEED    = 0x40;
constexpr uint8_t ASSIST_FLAG_DISPLAY_TARGET_CURRENT = 0x80;  // v6: show target current on display

// Config sizes / wire versions
constexpr uint8_t BBS_FW_CONFIG_VERSION         = 5;
constexpr size_t  BBS_FW_CONFIG_V5_SIZE         = 154;
constexpr uint8_t BBS_FW_CONFIG_VERSION_6       = 6;
constexpr size_t  BBS_FW_CONFIG_V6_SIZE         = 192;
constexpr uint8_t BBS_FW_CONFIG_VERSION_4       = 4;
constexpr size_t  BBS_FW_CONFIG_V4_SIZE         = 152;

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

// Config version 4 (bbs-fw v1.5.0 and earlier): identical to V5 but WITHOUT the
// `use_pretension` and `pretension_speed_cutoff_kph` fields. Those two bytes were
// removed in v4 and re-added (at this position) in v5. Every other field is
// byte-identical; V4 is therefore exactly 2 bytes shorter (152 vs 154).
struct BbsFwConfigV4 {
    uint8_t use_freedom_units;
    uint8_t max_current_amps;
    uint8_t current_ramp_amps_s;
    uint8_t max_battery_x100v_u16l;
    uint8_t max_battery_x100v_u16h;
    uint8_t low_cut_off_v;
    uint8_t max_speed_kph;
    uint8_t use_speed_sensor;
    uint8_t use_shift_sensor;
    uint8_t use_push_walk;
    uint8_t use_temperature_sensor;
    uint8_t lights_mode;
    // NOTE: use_pretension and pretension_speed_cutoff_kph are absent in V4.
    uint8_t wheel_size_inch_x10_u16l;
    uint8_t wheel_size_inch_x10_u16h;
    uint8_t speed_sensor_signals;
    uint8_t pas_start_delay_pulses;
    uint8_t pas_stop_delay_x100s;
    uint8_t pas_keep_current_percent;
    uint8_t pas_keep_current_cadence_rpm;
    uint8_t throttle_start_voltage_mv_u16l;
    uint8_t throttle_start_voltage_mv_u16h;
    uint8_t throttle_end_voltage_mv_u16l;
    uint8_t throttle_end_voltage_mv_u16h;
    uint8_t throttle_start_percent;
    uint8_t throttle_global_spd_lim_opt;
    uint8_t throttle_global_spd_lim_percent;
    uint8_t shift_interrupt_duration_ms_u16l;
    uint8_t shift_interrupt_duration_ms_u16h;
    uint8_t shift_interrupt_current_threshold_percent;
    uint8_t walk_mode_data_display;
    uint8_t assist_mode_select;
    uint8_t assist_startup_level;

    AssistLevel standard_levels[10];
    AssistLevel sport_levels[10];
};

// Config version 6 (bbs-fw fork, "per-assist-level PAS min current and cadence
// taper, display target current"). It is NOT a superset of v5:
//   * the global `pas_keep_current_percent` / `pas_keep_current_cadence_rpm`
//     header fields are gone (folded into the per-level min current + taper),
//   * each assist level grew from 6 to 8 bytes.
// Total size: 32-byte header + 2 * 10 * 8 = 192 bytes.
struct AssistLevelV6 {
    uint8_t flags;
    uint8_t max_current_percent;             // target current before taper
    uint8_t min_current_percent;             // current floor after taper
    uint8_t taper_start_cadence_rpm;         // taper begins at this cadence
    uint8_t taper_end_cadence_rpm;           // taper reaches min current here
    uint8_t max_throttle_current_percent;
    uint8_t max_speed_percent;
    uint8_t torque_amplification_factor_x10;
};

struct BbsFwConfigV6 {
    // Units
    uint8_t use_freedom_units;

    // Global electrical & motor
    uint8_t max_current_amps;
    uint8_t current_ramp_amps_s;
    uint8_t max_battery_x100v_u16l;
    uint8_t max_battery_x100v_u16h;
    uint8_t low_cut_off_v;
    uint8_t max_speed_kph;

    // External sensors & accessories
    uint8_t use_speed_sensor;
    uint8_t use_shift_sensor;
    uint8_t use_push_walk;
    uint8_t use_temperature_sensor;
    uint8_t lights_mode;
    uint8_t use_pretension;
    uint8_t pretension_speed_cutoff_kph;

    // Speed sensor
    uint8_t wheel_size_inch_x10_u16l;
    uint8_t wheel_size_inch_x10_u16h;
    uint8_t speed_sensor_signals;

    // Pedal Assist (PAS) — note: no global keep-current fields in v6
    uint8_t pas_start_delay_pulses;
    uint8_t pas_stop_delay_x100s;

    // Throttle
    uint8_t throttle_start_voltage_mv_u16l;
    uint8_t throttle_start_voltage_mv_u16h;
    uint8_t throttle_end_voltage_mv_u16l;
    uint8_t throttle_end_voltage_mv_u16h;
    uint8_t throttle_start_percent;
    uint8_t throttle_global_spd_lim_opt;
    uint8_t throttle_global_spd_lim_percent;

    // Shift sensor interrupt
    uint8_t shift_interrupt_duration_ms_u16l;
    uint8_t shift_interrupt_duration_ms_u16h;
    uint8_t shift_interrupt_current_threshold_percent;

    // Display walk mode field
    uint8_t walk_mode_data_display;

    // Assist mode options
    uint8_t assist_mode_select;
    uint8_t assist_startup_level;

    // 10 Standard assist levels (0 - 9)
    AssistLevelV6 standard_levels[10];

    // 10 Sport assist levels (0 - 9)
    AssistLevelV6 sport_levels[10];
};

#pragma pack(pop)

// Version-tagged container. The controller reports its wire config version in
// the firmware-version response and in the read-config header; only one of the
// two layouts below is valid for a given session (v4 is read into the v5 view).
struct BbsFwConfig {
    uint8_t version;    // 4, 5 or 6 — the controller's detected config version
    BbsFwConfigV5 v5;   // active when version is 4 or 5
    BbsFwConfigV6 v6;   // active when version is 6

    bool isV6() const { return version >= BBS_FW_CONFIG_VERSION_6; }
};

// Convert between the version-4 (152-byte) and version-5 (154-byte) config layouts.
// The only difference is the two pretension fields at V5 byte offsets 12-13.
void convertConfigV4toV5(const BbsFwConfigV4& src, BbsFwConfigV5& dst);
void convertConfigV5toV4(const BbsFwConfigV5& src, BbsFwConfigV4& dst);

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
// Serialize/deserialize using the layout selected by cfg.version (4/5 -> v5 view,
// 6 -> v6 view). The caller must set cfg.version before deserializing.
bool serializeConfigToJson(const BbsFwConfig& cfg, JsonDocument& doc);
bool deserializeConfigFromJson(const JsonDocument& doc, BbsFwConfig& cfg);
String getEventDescription(uint8_t eventId, int16_t data, bool hasData);
const char* getControllerTypeName(ControllerType type);

#endif // BBS_FW_PROTOCOL_H
