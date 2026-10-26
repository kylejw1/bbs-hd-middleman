package com.bbshd.middleman

import org.json.JSONArray
import org.json.JSONObject

/**
 * One parsed `/api/telemetry` snapshot.
 *
 * The firmware serialises most floats as JSON *strings* (`String(value, 1)`) so it
 * can control the decimal places, which is why every numeric read goes through
 * [JSONObject.num] instead of `optDouble`. Fields that are absent or null become
 * `null` here rather than 0, so the UI can tell "no sensor" from "reading zero".
 */
data class Telemetry(
    val speedKph: Double?,
    val powerWatts: Int?,
    val currentAmps: Double?,
    val batteryPercent: Int?,
    val batteryVoltage: Double?,
    val cadenceRpm: Double?,
    val motorRpm: Double?,
    val targetCurrentPercent: Int?,
    val targetSpeedPercent: Int?,
    val hasTargetTelemetry: Boolean,
    val assistLevel: Int?,
    val operationMode: String?,
    val lights: Boolean,
    val leanDeg: Double?,
    val statusCode: Int?,
    val displayConnected: Boolean,
    val controllerConnected: Boolean,
    val fwVersion: String?,
    val controllerType: String?,
) {
    companion object {
        fun parse(json: String): Telemetry? = try {
            val o = JSONObject(json)
            Telemetry(
                speedKph = o.num("speedKph"),
                powerWatts = o.num("powerWatts")?.toInt(),
                currentAmps = o.num("currentAmps"),
                batteryPercent = o.num("batteryPercent")?.toInt(),
                batteryVoltage = o.num("batteryVoltage"),
                cadenceRpm = o.num("cadenceRpm"),
                motorRpm = o.num("motorRpm"),
                targetCurrentPercent = o.num("targetCurrentPercent")?.toInt(),
                targetSpeedPercent = o.num("targetSpeedPercent")?.toInt(),
                hasTargetTelemetry = o.flag("hasTargetTelemetry"),
                assistLevel = o.num("assistLevel")?.toInt(),
                operationMode = o.text("operationMode"),
                lights = o.flag("lights"),
                leanDeg = o.num("imuLeanDeg"),
                statusCode = o.num("statusCode")?.toInt(),
                displayConnected = o.flag("displayConnected"),
                controllerConnected = o.flag("controllerConnected"),
                fwVersion = o.text("fwVersion"),
                controllerType = o.text("controllerType"),
            )
        } catch (e: Exception) {
            null
        }
    }
}

/**
 * One line of the bbs-fw event log.
 *
 * `timeMs` is millis since the controller booted, not a wall clock -- the
 * firmware has no RTC. It is rendered as an offset so two events can be told
 * apart in time without pretending to know what time it was.
 */
/**
 * The parts of `/api/info` the Device screen shows. All optional: a firmware that
 * predates a field renders `--` rather than failing the whole read.
 */
data class DeviceInfo(
    val fwVersion: String?,
    val fwBuild: String?,
    val model: String?,
    val chipRevision: Int?,
    val cpuFreqMHz: Int?,
    val freeHeap: Long?,
    val flashSize: Long?,
    val uptimeSec: Long?,
    val wifiMode: String?,
    val staIP: String?,
    val apIP: String?,
    val mdnsHost: String?,
) {
    companion object {
        fun parse(json: String): DeviceInfo? = try {
            val o = JSONObject(json)
            DeviceInfo(
                fwVersion = o.text("fwVersion"),
                fwBuild = o.text("fwBuild"),
                model = o.text("model"),
                chipRevision = o.num("chipRevision")?.toInt(),
                cpuFreqMHz = o.num("cpuFreqMHz")?.toInt(),
                freeHeap = o.num("freeHeap")?.toLong(),
                flashSize = o.num("flashSize")?.toLong(),
                uptimeSec = o.num("uptimeSec")?.toLong(),
                wifiMode = o.text("wifiMode"),
                staIP = o.text("staIP"),
                apIP = o.text("apIP"),
                mdnsHost = o.text("mdnsHost"),
            )
        } catch (e: Exception) {
            null
        }
    }
}

data class EventLine(val timeMs: Long, val level: Int, val message: String)

/**
 * Parses the event log. `/api/events` and the BLE events push both go through
 * `TelemetryTracker::buildEventsJson`, which emits a bare array — but the HTTP
 * route has wrapped it in an object in the past, so both shapes are accepted.
 */
fun parseEvents(json: String): List<EventLine> = try {
    val trimmed = json.trim()
    val array: JSONArray = if (trimmed.startsWith("[")) {
        JSONArray(trimmed)
    } else {
        JSONObject(trimmed).optJSONArray("events") ?: JSONArray()
    }
    buildList(array.length()) {
        for (i in 0 until array.length()) {
            val item = array.optJSONObject(i) ?: continue
            val message = item.text("msg") ?: continue
            add(
                EventLine(
                    timeMs = item.num("time")?.toLong() ?: 0L,
                    level = item.num("level")?.toInt() ?: 0,
                    message = message,
                )
            )
        }
    }
} catch (e: Exception) {
    emptyList()
}

// --- JSON helpers -------------------------------------------------------------

/** Reads a number that the firmware may have serialised as a JSON string. */
private fun JSONObject.num(key: String): Double? {
    if (!has(key) || isNull(key)) return null
    return when (val value = opt(key)) {
        is Number -> value.toDouble()
        is String -> value.toDoubleOrNull()
        else -> null
    }
}

private fun JSONObject.text(key: String): String? {
    if (!has(key) || isNull(key)) return null
    return opt(key)?.toString()
}

private fun JSONObject.flag(key: String): Boolean = when (val value = opt(key)) {
    is Boolean -> value
    is String -> value.equals("true", ignoreCase = true)
    is Number -> value.toInt() != 0
    else -> false
}
