#include "BbsFwProtocol.h"

// Compile-time guards: struct packing/size drift would corrupt the controller EEPROM
// (AGENTS.md rule #1). Fail the build loudly if these ever move.
static_assert(sizeof(BbsFwConfigV5) == BBS_FW_CONFIG_V5_SIZE, "BbsFwConfigV5 size changed");
static_assert(sizeof(BbsFwConfigV4) == BBS_FW_CONFIG_V4_SIZE, "BbsFwConfigV4 size changed");
static_assert(sizeof(AssistLevel) == 6, "AssistLevel size changed");

void convertConfigV4toV5(const BbsFwConfigV4& src, BbsFwConfigV5& dst) {
    uint8_t* d = reinterpret_cast<uint8_t*>(&dst);
    const uint8_t* s = reinterpret_cast<const uint8_t*>(&src);
    // Fields before the pretension gap: use_freedom_units .. lights_mode (12 bytes)
    memcpy(d, s, 12);
    // Pretension was hardcoded OFF in v4
    dst.use_pretension = 0;
    dst.pretension_speed_cutoff_kph = 0;
    // Fields after the gap: wheel_size .. sport_levels (140 bytes)
    memcpy(d + 14, s + 12, 140);
}

void convertConfigV5toV4(const BbsFwConfigV5& src, BbsFwConfigV4& dst) {
    uint8_t* d = reinterpret_cast<uint8_t*>(&dst);
    const uint8_t* s = reinterpret_cast<const uint8_t*>(&src);
    memcpy(d, s, 12);                 // use_freedom_units .. lights_mode
    memcpy(d + 12, s + 14, 140);      // wheel_size .. sport_levels (skip pretension)
}

void initDefaultBbsHdConfig(BbsFwConfigV5& cfg) {
    memset(&cfg, 0, sizeof(BbsFwConfigV5));

    cfg.use_freedom_units = 0;              // Metric by default (km/h)
    cfg.max_current_amps = 30;              // 30 Amps safe BBSHD default
    cfg.current_ramp_amps_s = 50;           // 50 A/s smooth ramp
    
    // 58.8V max battery (14S 52V pack: 5880)
    uint16_t maxV = 5880;
    cfg.max_battery_x100v_u16l = (uint8_t)(maxV & 0xFF);
    cfg.max_battery_x100v_u16h = (uint8_t)(maxV >> 8);
    
    cfg.low_cut_off_v = 41;                 // 41V LVC
    cfg.max_speed_kph = 50;                 // 50 km/h

    cfg.use_speed_sensor = 1;
    cfg.use_shift_sensor = 1;
    cfg.use_push_walk = 1;
    cfg.use_temperature_sensor = 3;         // All (Controller & Motor)
    cfg.lights_mode = 0;                    // Default
    cfg.use_pretension = 0;
    cfg.pretension_speed_cutoff_kph = 16;

    // 27.5 inch wheel -> 275
    uint16_t wheel = 275;
    cfg.wheel_size_inch_x10_u16l = (uint8_t)(wheel & 0xFF);
    cfg.wheel_size_inch_x10_u16h = (uint8_t)(wheel >> 8);
    cfg.speed_sensor_signals = 1;

    cfg.pas_start_delay_pulses = 3;
    cfg.pas_stop_delay_x100s = 20;          // 200 ms
    cfg.pas_keep_current_percent = 80;
    cfg.pas_keep_current_cadence_rpm = 255;

    // Throttle: 1100 mV start, 3600 mV end
    uint16_t thStart = 1100;
    cfg.throttle_start_voltage_mv_u16l = (uint8_t)(thStart & 0xFF);
    cfg.throttle_start_voltage_mv_u16h = (uint8_t)(thStart >> 8);

    uint16_t thEnd = 3600;
    cfg.throttle_end_voltage_mv_u16l = (uint8_t)(thEnd & 0xFF);
    cfg.throttle_end_voltage_mv_u16h = (uint8_t)(thEnd >> 8);

    cfg.throttle_start_percent = 5;
    cfg.throttle_global_spd_lim_opt = 0;
    cfg.throttle_global_spd_lim_percent = 100;

    // Shift sensor: 450 ms interrupt
    uint16_t shiftDur = 450;
    cfg.shift_interrupt_duration_ms_u16l = (uint8_t)(shiftDur & 0xFF);
    cfg.shift_interrupt_duration_ms_u16h = (uint8_t)(shiftDur >> 8);
    cfg.shift_interrupt_current_threshold_percent = 15;

    cfg.walk_mode_data_display = 0;         // Speed
    cfg.assist_mode_select = 1;             // Standard
    cfg.assist_startup_level = 1;

    // Standard Assist Levels (Level 0 - 9)
    // Level 0: Throttle only (or off)
    cfg.standard_levels[0] = { ASSIST_FLAG_THROTTLE, 0, 100, 100, 100, 10 };
    // Levels 1 - 9: Progressive power & cadence
    uint8_t pwrSteps[9] = { 15, 25, 35, 45, 55, 68, 80, 90, 100 };
    uint8_t spdSteps[9] = { 40, 50, 60, 70, 80, 88, 94, 98, 100 };
    for (int i = 1; i <= 9; ++i) {
        cfg.standard_levels[i].flags = ASSIST_FLAG_PAS | ASSIST_FLAG_THROTTLE;
        cfg.standard_levels[i].target_current_percent = pwrSteps[i - 1];
        cfg.standard_levels[i].max_throttle_current_percent = 100;
        cfg.standard_levels[i].max_cadence_percent = 100;
        cfg.standard_levels[i].max_speed_percent = spdSteps[i - 1];
        cfg.standard_levels[i].torque_amplification_factor_x10 = 10;
    }

    // Sport Assist Levels (More aggressive throttle and power)
    cfg.sport_levels[0] = { ASSIST_FLAG_THROTTLE, 0, 100, 100, 100, 10 };
    uint8_t sportPwrSteps[9] = { 25, 40, 55, 70, 80, 90, 95, 100, 100 };
    for (int i = 1; i <= 9; ++i) {
        cfg.sport_levels[i].flags = ASSIST_FLAG_PAS | ASSIST_FLAG_THROTTLE | ASSIST_FLAG_OVERRIDE_CADENCE;
        cfg.sport_levels[i].target_current_percent = sportPwrSteps[i - 1];
        cfg.sport_levels[i].max_throttle_current_percent = 100;
        cfg.sport_levels[i].max_cadence_percent = 100;
        cfg.sport_levels[i].max_speed_percent = 100;
        cfg.sport_levels[i].torque_amplification_factor_x10 = 15;
    }
}

bool serializeConfigToJson(const BbsFwConfigV5& cfg, JsonDocument& doc) {
    doc["freedomUnits"] = cfg.use_freedom_units;
    doc["maxCurrent"] = cfg.max_current_amps;
    doc["currentRamp"] = cfg.current_ramp_amps_s;

    uint16_t maxV_raw = cfg.max_battery_x100v_u16l | (cfg.max_battery_x100v_u16h << 8);
    doc["maxBatteryVolts"] = maxV_raw / 100.0f;
    doc["lowCutoffVolts"] = cfg.low_cut_off_v;
    doc["maxSpeed"] = cfg.max_speed_kph;

    doc["useSpeedSensor"] = cfg.use_speed_sensor != 0;
    doc["useShiftSensor"] = cfg.use_shift_sensor != 0;
    doc["usePushWalk"] = cfg.use_push_walk != 0;
    doc["temperatureSensor"] = cfg.use_temperature_sensor;
    doc["lightsMode"] = cfg.lights_mode;
    doc["usePretension"] = cfg.use_pretension != 0;
    doc["pretensionSpeedCutoff"] = cfg.pretension_speed_cutoff_kph;

    uint16_t wheel_raw = cfg.wheel_size_inch_x10_u16l | (cfg.wheel_size_inch_x10_u16h << 8);
    doc["wheelSizeInch"] = wheel_raw / 10.0f;
    doc["speedSensorSignals"] = cfg.speed_sensor_signals;

    doc["pasStartDelay"] = cfg.pas_start_delay_pulses;
    doc["pasStopDelayMs"] = cfg.pas_stop_delay_x100s * 10;
    doc["pasKeepCurrentPercent"] = cfg.pas_keep_current_percent;
    doc["pasKeepCurrentCadenceRpm"] = cfg.pas_keep_current_cadence_rpm;

    uint16_t thStart = cfg.throttle_start_voltage_mv_u16l | (cfg.throttle_start_voltage_mv_u16h << 8);
    uint16_t thEnd = cfg.throttle_end_voltage_mv_u16l | (cfg.throttle_end_voltage_mv_u16h << 8);
    doc["throttleStartMv"] = thStart;
    doc["throttleEndMv"] = thEnd;
    doc["throttleStartPercent"] = cfg.throttle_start_percent;
    doc["throttleGlobalSpdLimOpt"] = cfg.throttle_global_spd_lim_opt;
    doc["throttleGlobalSpdLimPercent"] = cfg.throttle_global_spd_lim_percent;

    uint16_t shiftDur = cfg.shift_interrupt_duration_ms_u16l | (cfg.shift_interrupt_duration_ms_u16h << 8);
    doc["shiftInterruptDurationMs"] = shiftDur;
    doc["shiftInterruptCurrentThreshold"] = cfg.shift_interrupt_current_threshold_percent;

    doc["walkModeDisplay"] = cfg.walk_mode_data_display;
    doc["assistModeSelect"] = cfg.assist_mode_select;
    doc["assistStartupLevel"] = cfg.assist_startup_level;

    // Standard levels array
    JsonArray stdArr = doc["standardLevels"].to<JsonArray>();
    for (int i = 0; i < 10; ++i) {
        JsonObject lvl = stdArr.add<JsonObject>();
        lvl["flags"] = cfg.standard_levels[i].flags;
        lvl["pas"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_PAS) != 0;
        lvl["throttle"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_THROTTLE) != 0;
        lvl["cruise"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_CRUISE) != 0;
        lvl["overrideCadence"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_OVERRIDE_CADENCE) != 0;
        lvl["overrideSpeed"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_OVERRIDE_SPEED) != 0;
        lvl["pasVariable"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_PAS_VARIABLE) != 0;
        lvl["pasTorque"] = (cfg.standard_levels[i].flags & ASSIST_FLAG_PAS_TORQUE) != 0;
        lvl["current"] = cfg.standard_levels[i].target_current_percent;
        lvl["maxThrottle"] = cfg.standard_levels[i].max_throttle_current_percent;
        lvl["cadence"] = cfg.standard_levels[i].max_cadence_percent;
        lvl["speed"] = cfg.standard_levels[i].max_speed_percent;
        lvl["torqueAmp"] = cfg.standard_levels[i].torque_amplification_factor_x10 / 10.0f;
    }

    // Sport levels array
    JsonArray sportArr = doc["sportLevels"].to<JsonArray>();
    for (int i = 0; i < 10; ++i) {
        JsonObject lvl = sportArr.add<JsonObject>();
        lvl["flags"] = cfg.sport_levels[i].flags;
        lvl["pas"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_PAS) != 0;
        lvl["throttle"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_THROTTLE) != 0;
        lvl["cruise"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_CRUISE) != 0;
        lvl["overrideCadence"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_OVERRIDE_CADENCE) != 0;
        lvl["overrideSpeed"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_OVERRIDE_SPEED) != 0;
        lvl["pasVariable"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_PAS_VARIABLE) != 0;
        lvl["pasTorque"] = (cfg.sport_levels[i].flags & ASSIST_FLAG_PAS_TORQUE) != 0;
        lvl["current"] = cfg.sport_levels[i].target_current_percent;
        lvl["maxThrottle"] = cfg.sport_levels[i].max_throttle_current_percent;
        lvl["cadence"] = cfg.sport_levels[i].max_cadence_percent;
        lvl["speed"] = cfg.sport_levels[i].max_speed_percent;
        lvl["torqueAmp"] = cfg.sport_levels[i].torque_amplification_factor_x10 / 10.0f;
    }

    return true;
}

bool deserializeConfigFromJson(const JsonDocument& doc, BbsFwConfigV5& cfg) {
    // Zero the struct first so fields absent from the JSON (e.g. pretension on a
    // version-5 write) are deterministic instead of uninitialized stack garbage.
    memset(&cfg, 0, sizeof(BbsFwConfigV5));

    if (doc["maxCurrent"].is<uint8_t>()) {
        cfg.max_current_amps = doc["maxCurrent"];
    }
    if (doc["currentRamp"].is<uint8_t>()) {
        cfg.current_ramp_amps_s = doc["currentRamp"];
    }
    if (doc["freedomUnits"].is<uint8_t>()) {
        cfg.use_freedom_units = doc["freedomUnits"];
    }

    if (doc["maxBatteryVolts"].is<float>()) {
        uint16_t maxV = (uint16_t)(doc["maxBatteryVolts"].as<float>() * 100.0f);
        cfg.max_battery_x100v_u16l = (uint8_t)(maxV & 0xFF);
        cfg.max_battery_x100v_u16h = (uint8_t)(maxV >> 8);
    }
    if (doc["lowCutoffVolts"].is<uint8_t>()) {
        cfg.low_cut_off_v = doc["lowCutoffVolts"];
    }
    if (doc["maxSpeed"].is<uint8_t>()) {
        cfg.max_speed_kph = doc["maxSpeed"];
    }

    if (doc["useSpeedSensor"].is<bool>()) cfg.use_speed_sensor = doc["useSpeedSensor"] ? 1 : 0;
    if (doc["useShiftSensor"].is<bool>()) cfg.use_shift_sensor = doc["useShiftSensor"] ? 1 : 0;
    if (doc["usePushWalk"].is<bool>()) cfg.use_push_walk = doc["usePushWalk"] ? 1 : 0;
    if (doc["temperatureSensor"].is<uint8_t>()) cfg.use_temperature_sensor = doc["temperatureSensor"];
    if (doc["lightsMode"].is<uint8_t>()) cfg.lights_mode = doc["lightsMode"];
    if (doc["usePretension"].is<bool>()) cfg.use_pretension = doc["usePretension"] ? 1 : 0;
    if (doc["pretensionSpeedCutoff"].is<uint8_t>()) cfg.pretension_speed_cutoff_kph = doc["pretensionSpeedCutoff"];

    if (doc["wheelSizeInch"].is<float>()) {
        uint16_t wheel = (uint16_t)(doc["wheelSizeInch"].as<float>() * 10.0f);
        cfg.wheel_size_inch_x10_u16l = (uint8_t)(wheel & 0xFF);
        cfg.wheel_size_inch_x10_u16h = (uint8_t)(wheel >> 8);
    }
    if (doc["speedSensorSignals"].is<uint8_t>()) cfg.speed_sensor_signals = doc["speedSensorSignals"];

    if (doc["pasStartDelay"].is<uint8_t>()) cfg.pas_start_delay_pulses = doc["pasStartDelay"];
    if (doc["pasStopDelayMs"].is<uint32_t>()) {
        uint32_t ms = doc["pasStopDelayMs"];
        cfg.pas_stop_delay_x100s = (uint8_t)(ms / 10);
    }
    if (doc["pasKeepCurrentPercent"].is<uint8_t>()) cfg.pas_keep_current_percent = doc["pasKeepCurrentPercent"];
    if (doc["pasKeepCurrentCadenceRpm"].is<uint8_t>()) cfg.pas_keep_current_cadence_rpm = doc["pasKeepCurrentCadenceRpm"];

    if (doc["throttleStartMv"].is<uint16_t>()) {
        uint16_t thStart = doc["throttleStartMv"];
        cfg.throttle_start_voltage_mv_u16l = (uint8_t)(thStart & 0xFF);
        cfg.throttle_start_voltage_mv_u16h = (uint8_t)(thStart >> 8);
    }
    if (doc["throttleEndMv"].is<uint16_t>()) {
        uint16_t thEnd = doc["throttleEndMv"];
        cfg.throttle_end_voltage_mv_u16l = (uint8_t)(thEnd & 0xFF);
        cfg.throttle_end_voltage_mv_u16h = (uint8_t)(thEnd >> 8);
    }
    if (doc["throttleStartPercent"].is<uint8_t>()) cfg.throttle_start_percent = doc["throttleStartPercent"];
    if (doc["throttleGlobalSpdLimOpt"].is<uint8_t>()) cfg.throttle_global_spd_lim_opt = doc["throttleGlobalSpdLimOpt"];
    if (doc["throttleGlobalSpdLimPercent"].is<uint8_t>()) cfg.throttle_global_spd_lim_percent = doc["throttleGlobalSpdLimPercent"];

    if (doc["shiftInterruptDurationMs"].is<uint16_t>()) {
        uint16_t shiftDur = doc["shiftInterruptDurationMs"];
        cfg.shift_interrupt_duration_ms_u16l = (uint8_t)(shiftDur & 0xFF);
        cfg.shift_interrupt_duration_ms_u16h = (uint8_t)(shiftDur >> 8);
    }
    if (doc["shiftInterruptCurrentThreshold"].is<uint8_t>()) {
        cfg.shift_interrupt_current_threshold_percent = doc["shiftInterruptCurrentThreshold"];
    }

    if (doc["walkModeDisplay"].is<uint8_t>()) cfg.walk_mode_data_display = doc["walkModeDisplay"];
    if (doc["assistModeSelect"].is<uint8_t>()) cfg.assist_mode_select = doc["assistModeSelect"];
    if (doc["assistStartupLevel"].is<uint8_t>()) cfg.assist_startup_level = doc["assistStartupLevel"];

    // Standard levels
    if (doc["standardLevels"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["standardLevels"].as<JsonArrayConst>();
        for (size_t i = 0; i < arr.size() && i < 10; ++i) {
            JsonObjectConst lvl = arr[i];
            uint8_t flags = 0;
            if (lvl["pas"].is<bool>() && lvl["pas"]) flags |= ASSIST_FLAG_PAS;
            if (lvl["throttle"].is<bool>() && lvl["throttle"]) flags |= ASSIST_FLAG_THROTTLE;
            if (lvl["cruise"].is<bool>() && lvl["cruise"]) flags |= ASSIST_FLAG_CRUISE;
            if (lvl["overrideCadence"].is<bool>() && lvl["overrideCadence"]) flags |= ASSIST_FLAG_OVERRIDE_CADENCE;
            if (lvl["overrideSpeed"].is<bool>() && lvl["overrideSpeed"]) flags |= ASSIST_FLAG_OVERRIDE_SPEED;
            if (lvl["pasVariable"].is<bool>() && lvl["pasVariable"]) flags |= ASSIST_FLAG_PAS_VARIABLE;
            if (lvl["pasTorque"].is<bool>() && lvl["pasTorque"]) flags |= ASSIST_FLAG_PAS_TORQUE;
            if (lvl["flags"].is<uint8_t>()) flags = lvl["flags"];

            cfg.standard_levels[i].flags = flags;
            if (lvl["current"].is<uint8_t>()) cfg.standard_levels[i].target_current_percent = lvl["current"];
            if (lvl["maxThrottle"].is<uint8_t>()) cfg.standard_levels[i].max_throttle_current_percent = lvl["maxThrottle"];
            if (lvl["cadence"].is<uint8_t>()) cfg.standard_levels[i].max_cadence_percent = lvl["cadence"];
            if (lvl["speed"].is<uint8_t>()) cfg.standard_levels[i].max_speed_percent = lvl["speed"];
            if (lvl["torqueAmp"].is<float>()) {
                cfg.standard_levels[i].torque_amplification_factor_x10 = (uint8_t)(lvl["torqueAmp"].as<float>() * 10.0f);
            }
        }
    }

    // Sport levels
    if (doc["sportLevels"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["sportLevels"].as<JsonArrayConst>();
        for (size_t i = 0; i < arr.size() && i < 10; ++i) {
            JsonObjectConst lvl = arr[i];
            uint8_t flags = 0;
            if (lvl["pas"].is<bool>() && lvl["pas"]) flags |= ASSIST_FLAG_PAS;
            if (lvl["throttle"].is<bool>() && lvl["throttle"]) flags |= ASSIST_FLAG_THROTTLE;
            if (lvl["cruise"].is<bool>() && lvl["cruise"]) flags |= ASSIST_FLAG_CRUISE;
            if (lvl["overrideCadence"].is<bool>() && lvl["overrideCadence"]) flags |= ASSIST_FLAG_OVERRIDE_CADENCE;
            if (lvl["overrideSpeed"].is<bool>() && lvl["overrideSpeed"]) flags |= ASSIST_FLAG_OVERRIDE_SPEED;
            if (lvl["pasVariable"].is<bool>() && lvl["pasVariable"]) flags |= ASSIST_FLAG_PAS_VARIABLE;
            if (lvl["pasTorque"].is<bool>() && lvl["pasTorque"]) flags |= ASSIST_FLAG_PAS_TORQUE;
            if (lvl["flags"].is<uint8_t>()) flags = lvl["flags"];

            cfg.sport_levels[i].flags = flags;
            if (lvl["current"].is<uint8_t>()) cfg.sport_levels[i].target_current_percent = lvl["current"];
            if (lvl["maxThrottle"].is<uint8_t>()) cfg.sport_levels[i].max_throttle_current_percent = lvl["maxThrottle"];
            if (lvl["cadence"].is<uint8_t>()) cfg.sport_levels[i].max_cadence_percent = lvl["cadence"];
            if (lvl["speed"].is<uint8_t>()) cfg.sport_levels[i].max_speed_percent = lvl["speed"];
            if (lvl["torqueAmp"].is<float>()) {
                cfg.sport_levels[i].torque_amplification_factor_x10 = (uint8_t)(lvl["torqueAmp"].as<float>() * 10.0f);
            }
        }
    }

    return true;
}

String getEventDescription(uint8_t eventId, int16_t data, bool hasData) {
    switch (eventId) {
        case 1:  return "Motor initialization successful.";
        case 2:  return "Successfully read configuration from EEPROM.";
        case 3:  return "Configuration reset performed.";
        case 4:  return "Configuration written to EEPROM.";
        case 5:  return "Reading configuration from EEPROM...";
        case 6:  return "Writing configuration to EEPROM...";
        case 7:  return "Reading persisted state from EEPROM...";
        case 8:  return "Successfully read persisted state from EEPROM.";
        case 9:  return "Writing persisted state to EEPROM...";
        case 10: return "Persisted state written to EEPROM.";

        case 64: return "Error: Failed to perform motor controller initialization.";
        case 65: return "Error: Failed to set target speed on motor controller.";
        case 66: return "Error: Failed to set target current on motor controller.";
        case 67: return "Error: Failed to read status from motor controller.";
        case 68: return "Error: Failed to read current from motor controller.";
        case 69: return "Error: Failed to read voltage from motor controller.";
        case 70: return "Error: Failed to read EEPROM.";
        case 71: return "Error: Failed to write EEPROM.";
        case 72: return "Error: Failed to erase EEPROM.";
        case 73: return "Error: EEPROM configuration version mismatch.";
        case 74: return "Error: EEPROM checksum failure.";
        case 75: return "Error: Throttle below lower threshold (check wiring/sensor).";
        case 76: return "Error: Throttle above upper threshold (check wiring/sensor).";
        case 77: return "Error: Watchdog timer reset triggered.";
        case 78: return "Error: Serial message checksum mismatch.";
        case 79: return "Warning: Serial packet discarded.";

        case 128: return "Target motor current changed to " + String(data) + "%.";
        case 129: return "Target motor speed changed to " + String((data * 100) / 255) + "%.";
        case 130: return "Motor status code changed to 0x" + String(data, HEX) + ".";
        case 131: return "Assist level changed to " + String(data) + ".";
        case 132: return "Operation mode: " + String(data == 1 ? "Sport" : "Standard") + ".";
        case 133: return "Max wheel speed limit: " + String(data) + " RPM.";
        case 134: return "Lights state: " + String(data ? "ON" : "OFF") + ".";
        case 135: {
            int8_t cTemp = (int8_t)(data & 0xFF);
            int8_t mTemp = (int8_t)((data >> 8) & 0xFF);
            return "Temperature: Controller=" + String(cTemp) + "C, Motor=" + String(mTemp) + "C.";
        }
        case 136: return data != 0 ? "Warning: Thermal limit reached! Power reduced." : "Thermal limiting cleared.";
        case 137: return data != 0 ? "Speed limit activated." : "Speed limit deactivated.";
        case 138: return "Max current ADC request: " + String(data) + ".";
        case 139: return "Max current ADC response: " + String(data) + ".";
        case 140: return "Main loop interval: " + String(data) + " ms.";
        case 141: return "Throttle raw ADC: " + String(data) + ".";
        case 142: return data != 0 ? ("Low voltage limiting active (cut to " + String(data / 100.0f, 1) + "V).") : "Low voltage limiting cleared.";
        case 143: return data != 0 ? "Shift sensor power cut started." : "Shift sensor power cut ended.";
        case 144: return data != 0 ? "BBSHD PTC thermistor detected." : "BBSHD NTC thermistor detected.";
        case 145: return "Battery voltage: " + String(data / 100.0f, 2) + "V.";
        case 146: return "Battery voltage calibrated: ADC factor=" + String(data / 100.0f, 2) + ".";
        case 147: return "Torque sensor raw ADC: " + String(data) + ".";
        case 148: return "Torque sensor calibrated: bias=" + String(data) + ".";
        default:
            if (hasData) {
                return "Unknown event ID " + String(eventId) + " (data=" + String(data) + ").";
            } else {
                return "Unknown event ID " + String(eventId) + ".";
            }
    }
}

const char* getControllerTypeName(ControllerType type) {
    switch (type) {
        case ControllerType::BBSHD: return "Bafang BBS-HD";
        case ControllerType::BBS02: return "Bafang BBS-02";
        case ControllerType::TSDZ2: return "Tongsheng TSDZ2";
        default: return "Unknown Controller";
    }
}
