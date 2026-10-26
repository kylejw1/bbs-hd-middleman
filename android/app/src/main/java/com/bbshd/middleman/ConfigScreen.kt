package com.bbshd.middleman

import android.app.Activity
import android.app.AlertDialog
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/**
 * The controller configuration editor.
 *
 * It edits the JSON that `/api/config` already speaks rather than the binary
 * struct: `serializeConfigToJson` / `deserializeConfigFromJson` on the firmware
 * are the contract, and re-deriving a 154-byte packed layout in Kotlin would be
 * a second place to get the packing wrong.
 *
 * Editing model: the fetched JSONObject is the document. Every edit mutates it,
 * and [render] rebuilds the form from it. Switching assist level therefore
 * collects first, so unsaved edits are never lost by navigation.
 *
 * Both v5 and v6 are editable: the v6 level struct is 7 bytes and the JSON
 * carries all of them, so a round-trip is lossless.
 */
private const val MAX_READ_ATTEMPTS = 3

class ConfigScreen(
    private val activity: Activity,
    private val ble: BleClient,
    private val notify: (String) -> Unit,
) {

    private lateinit var container: LinearLayout
    private lateinit var statusLine: TextView
    private lateinit var writeButton: Button

    private val config = JSONObject()

    private var version = 0
    private var levelSport = false
    private var level = 1
    private var loaded = false

    /** Rendered in [statusView]; kept here because [render] rebuilds that view. */
    private var statusText = "Not read yet"

    /**
     * A config transaction ties up the 1200-baud bridge for over a second and the
     * firmware's read budget is 3.5 s. Two overlapping reads make the second one
     * time out and report "Controller did not respond" (503), which looks like a
     * bike fault. Only ever run one at a time.
     */
    private var busy = false

    /** Flattened view of the config as it was read, used to summarise edits. */
    private var baseline: Map<String, String> = emptyMap()

    private val main = Handler(Looper.getMainLooper())

    private val commonInt = LinkedHashMap<String, EditText>()
    private val commonFloat = LinkedHashMap<String, EditText>()
    private val commonBool = LinkedHashMap<String, CheckBox>()
    private val levelInt = LinkedHashMap<String, EditText>()
    private val levelFloat = LinkedHashMap<String, EditText>()
    private val levelBool = LinkedHashMap<String, CheckBox>()

    // --- field specifications -------------------------------------------------

    private val commonIntFields = listOf(
        "maxCurrent" to "Max current (A)",
        "currentRamp" to "Current ramp (A/s)",
        "lowCutoffVolts" to "Low cut-off (V)",
        "maxSpeed" to "Max speed (km/h)",
        "speedSensorSignals" to "Speed sensor signals",
        "pasStartDelay" to "PAS start delay (pulses)",
        "pasStopDelayMs" to "PAS stop delay (ms)",
        "throttleStartMv" to "Throttle start (mV)",
        "throttleEndMv" to "Throttle end (mV)",
        "throttleStartPercent" to "Throttle start (%)",
        "throttleGlobalSpdLimOpt" to "Throttle speed limit mode",
        "throttleGlobalSpdLimPercent" to "Throttle speed limit (%)",
        "shiftInterruptDurationMs" to "Shift interrupt (ms)",
        "shiftInterruptCurrentThreshold" to "Shift threshold (%)",
        "walkModeDisplay" to "Walk mode display",
        "assistModeSelect" to "Assist mode select",
        "assistStartupLevel" to "Startup assist level",
        "pretensionSpeedCutoff" to "Pretension cut-off (km/h)",
        "temperatureSensor" to "Temperature sensor",
        "lightsMode" to "Lights mode",
    )

    private val commonFloatFields = listOf(
        "maxBatteryVolts" to "Max battery (V)",
        "wheelSizeInch" to "Wheel size (in)",
    )

    private val commonBoolFields = listOf(
        "freedomUnits" to "Freedom units (mph / miles)",
        "useSpeedSensor" to "Speed sensor fitted",
        "useShiftSensor" to "Shift sensor fitted",
        "usePushWalk" to "Push-walk fitted",
        "usePretension" to "Pretension enabled",
    )

    /** Present in the v5 and v6 layouts alike; the v6 struct keeps both. */
    private val v5OnlyIntFields = listOf(
        "pasKeepCurrentPercent" to "Keep current (%)",
        "pasKeepCurrentCadenceRpm" to "Keep current cadence (rpm)",
    )

    private val levelIntFields = listOf(
        "current" to "Target current (%)",
        "maxThrottle" to "Max throttle (%)",
        "cadence" to "Max cadence (%)",
        "speed" to "Max speed (%)",
    )

    private val levelFloatFields = listOf(
        "torqueAmp" to "Torque amplification",
    )

    private val v6LevelIntFields = listOf(
        "targetRpmOffset" to "Target rpm offset",
    )

    private val levelFlagFields = listOf(
        "pas" to "PAS",
        "throttle" to "Throttle",
        "cruise" to "Cruise",
        "overrideCadence" to "Override cadence",
        "overrideSpeed" to "Override speed",
        "pasVariable" to "PAS variable",
        "pasTorque" to "PAS torque",
    )

    // --- lifecycle ------------------------------------------------------------

    fun attach(screen: View) {
        container = screen.findViewById(R.id.configContainer)
        render()
    }

    /**
     * Reading is deliberately explicit. Auto-reading on tab open fires the
     * request in the same instant the link settles, which the controller misses
     * far more often than a read issued a moment later -- and a failure banner
     * nobody asked for is worse than a button that works.
     */
    fun onShown() = Unit

    // --- read / write ---------------------------------------------------------

    /**
     * The controller sometimes misses the 3.5 s read budget on the first attempt
     * after a fresh connection -- it is a transient timing miss, not a fault, and
     * an immediate retry always succeeds. Retrying once automatically keeps that
     * from looking like a broken bike.
     */
    fun read(attempt: Int = 1) {
        if (!ble.isReady()) {
            notify("Connect to the middleman first")
            return
        }
        if (busy) return
        busy = true
        setStatus("Reading configuration…")
        ble.get("/api/config") { status, body ->
            val parsed = runCatching { JSONObject(body) }.getOrNull()
            busy = false
            if (status == 503 && attempt < MAX_READ_ATTEMPTS) {
                setStatus("Controller busy — retry ${attempt + 1}/$MAX_READ_ATTEMPTS…")
                main.postDelayed({ read(attempt + 1) }, 400L * attempt)
                return@get
            }
            if (status != 200 || parsed == null) {
                setStatus(
                    when (status) {
                        // 503 is the firmware saying the controller did not
                        // answer within its own budget, not a protocol error.
                        503 -> "Read failed — controller did not respond"
                        -1 -> "Read failed — no reply from the middleman"
                        else -> "Read failed ($status)"
                    }
                )
                return@get
            }
            // Android's org.json has no JSONObject.clear(), unlike the desktop
            // reference implementation, so drop keys explicitly. Stale keys from
            // a previous read would otherwise linger across a version change.
            val stale = ArrayList<String>()
            config.keys().forEach { key -> stale.add(key) }
            stale.forEach { config.remove(it) }
            parsed.keys().forEach { key -> config.put(key, parsed.get(key)) }

            version = config.optInt("configVersion", 0)
            loaded = true
            level = config.optInt("assistStartupLevel", 1).coerceIn(0, 9)

            baseline = significant(flatten(config))
            setStatus("Read v$version — ready to edit")
            render()
        }
    }

    private fun write() {
        if (!ble.isReady()) {
            notify("Connect to the middleman first")
            return
        }
        if (busy) return

        // Critical: deserializeConfigFromJson zeroes the struct before applying
        // the keys it is given, so posting an empty document would erase the
        // controller's whole configuration. Never write what was never read.
        if (!loaded || config.length() == 0) {
            notify("Read the configuration before writing")
            return
        }

        collect()

        val current = significant(flatten(config))
        val changed = (baseline.keys + current.keys)
            .filter { baseline[it] != current[it] }
            .sorted()

        if (changed.isEmpty()) {
            // Nothing to say, and no reason to spend an EEPROM write cycle.
            notify("No changes since the last read")
            setStatus("Nothing to write")
            return
        }

        val list = changed.take(8).joinToString("\n") { "• $it" }
        val more = if (changed.size > 8) "\n…and ${changed.size - 8} more" else ""

        AlertDialog.Builder(activity)
            .setTitle("Write ${changed.size} change(s)?")
            .setMessage(
                "$list$more\n\n" +
                    "This writes the whole configuration to the controller's EEPROM. " +
                    "It takes about a second, during which the display is answered " +
                    "with mock data. A power loss mid-write can corrupt the " +
                    "controller's settings."
            )
            .setPositiveButton("Write") { _, _ -> sendWrite() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun sendWrite() {
        if (busy) return
        busy = true
        setStatus("Writing to EEPROM…")
        ble.post("/api/config", config.toString()) { status, body ->
            busy = false
            if (status == 200) {
                setStatus("Written — re-reading")
                notify("Configuration written")
                read()
            } else if (status == 409) {
                // The firmware refuses a version mismatch outright, which is the
                // guard that stops a v6 profile landing on a v5 controller.
                setStatus("Rejected: config version mismatch")
                notify("Version mismatch — re-read the config")
            } else {
                val detail = runCatching { JSONObject(body).optString("error") }.getOrNull()
                setStatus("Write failed (${if (status < 0) "no reply" else "$status"})")
                notify(detail?.takeIf { it.isNotEmpty() } ?: "Write failed")
            }
        }
    }

    /**
     * Flattens the config into `path -> value` so two documents can be compared
     * field by field, including inside the assist-level arrays.
     */
    private fun flatten(root: JSONObject): Map<String, String> {
        val out = LinkedHashMap<String, String>()

        fun walk(obj: JSONObject, prefix: String) {
            obj.keys().forEach { key ->
                when (val value = obj.opt(key)) {
                    is JSONObject -> walk(value, "$prefix$key.")
                    is JSONArray -> {
                        for (i in 0 until value.length()) {
                            val entry = value.opt(i)
                            if (entry is JSONObject) {
                                walk(entry, "$prefix$key[$i].")
                            } else {
                                out["$prefix$key[$i]"] = normalise(entry)
                            }
                        }
                    }
                    else -> out["$prefix$key"] = normalise(value)
                }
            }
        }

        walk(root, "")
        return out
    }

    /**
     * Collapses JSON representation differences that are not value changes.
     *
     * The firmware writes `use_freedom_units` as `0`/`1` but reads it back as a
     * boolean, and writes `26.0f` as `26`; widening a float through Float on the
     * way back (`40.8f` -> 40.79999923706055) makes a round-trip look like an
     * edit. Without this, opening the config and pressing Write reported five
     * "changes" nobody made, which is exactly how a real change gets missed.
     */
    private fun normalise(value: Any?): String = when (value) {
        null -> "null"
        is Boolean -> if (value) "1" else "0"
        is Number -> {
            val d = value.toDouble()
            if (d == Math.floor(d) && Math.abs(d) < 1e9) {
                d.toLong().toString()
            } else {
                String.format(Locale.US, "%.6g", d)
            }
        }
        else -> value.toString()
    }

    /**
     * `flags` is dropped because collectLevel() deliberately removes it so the
     * firmware rebuilds the byte from the checkboxes. Keeping it would report a
     * change on every level the user merely looked at.
     */
    private fun significant(map: Map<String, String>): Map<String, String> =
        map.filterKeys { !it.endsWith(".flags") }

    // --- collection -----------------------------------------------------------

    private fun collect() {
        commonInt.forEach { (key, view) ->
            view.text.toString().trim().toIntOrNull()?.let { config.put(key, it) }
        }
        commonFloat.forEach { (key, view) ->
            view.text.toString().trim().toFloatOrNull()?.let { config.put(key, it.toDouble()) }
        }
        commonBool.forEach { (key, view) -> config.put(key, view.isChecked) }
        collectLevel()
    }

    private fun levelsKey(sport: Boolean) = if (sport) "sportLevels" else "standardLevels"

    private fun levels(sport: Boolean): JSONArray =
        config.optJSONArray(levelsKey(sport)) ?: JSONArray()

    private fun collectLevel() {
        val array = levels(levelSport)
        val entry = array.optJSONObject(level) ?: JSONObject()

        levelInt.forEach { (key, view) ->
            view.text.toString().trim().toIntOrNull()?.let { entry.put(key, it) }
        }
        levelFloat.forEach { (key, view) ->
            view.text.toString().trim().toFloatOrNull()?.let { entry.put(key, it.toDouble()) }
        }
        // deserializeLevelFlags() lets an explicit `flags` byte win over the
        // booleans, and GET always returns one. Dropping it makes the firmware
        // rebuild the byte from the checkboxes, which is the edited view.
        entry.remove("flags")
        levelBool.forEach { (key, view) -> entry.put(key, view.isChecked) }

        array.put(level, entry)
        config.put(levelsKey(levelSport), array)
    }

    // --- rendering ------------------------------------------------------------

    private fun render() {
        container.removeAllViews()
        commonInt.clear()
        commonFloat.clear()
        commonBool.clear()

        container.addView(headerRow())
        container.addView(statusView())

        if (!loaded) {
            container.addView(note("Read the configuration to edit it."))
            return
        }

        container.addView(sectionHeader("BASIC"))
        commonIntFields.take(5).forEach { (key, label) -> addInt(container, commonInt, key, label) }
        commonFloatFields.forEach { (key, label) -> addFloat(container, commonFloat, key, label) }
        commonBoolFields.forEach { (key, label) -> addBool(container, commonBool, key, label) }

        container.addView(sectionHeader("PAS, THROTTLE & SHIFT"))
        commonIntFields.drop(5).forEach { (key, label) -> addInt(container, commonInt, key, label) }

        container.addView(sectionHeader("KEEP CURRENT"))
        v5OnlyIntFields.forEach { (key, label) -> addInt(container, commonInt, key, label) }

        container.addView(sectionHeader("ASSIST LEVELS"))
        container.addView(levelSelector())
        renderLevelFields()
    }

    private fun renderLevelFields() {
        levelInt.clear()
        levelFloat.clear()
        levelBool.clear()

        val entry = levels(levelSport).optJSONObject(level) ?: JSONObject()

        container.addView(sectionHeader("LEVEL $level — ${if (levelSport) "SPORT" else "STANDARD"}"))
        levelIntFields.forEach { (key, label) -> addInt(container, levelInt, key, label, entry) }
        levelFloatFields.forEach { (key, label) ->
            addFloat(container, levelFloat, key, label, entry)
        }
        if (version >= 6) {
            v6LevelIntFields.forEach { (key, label) -> addInt(container, levelInt, key, label, entry) }
        }
        levelFlagFields.forEach { (key, label) -> addBool(container, levelBool, key, label, entry) }
    }

    private fun headerRow(): View {
        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val title = TextView(activity).apply {
            text = "CONFIGURATION"
            setTextColor(color(R.color.dim))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            letterSpacing = 0.15f
        }
        row.addView(title, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))

        row.addView(smallButton("Read") { read() })
        writeButton = smallButton("Write") { write() }
        row.addView(writeButton)
        return row
    }

    private fun statusView(): TextView {
        statusLine = TextView(activity).apply {
            text = statusText
            setTextColor(color(R.color.dim))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = android.graphics.Typeface.MONOSPACE
            setPadding(0, dp(6), 0, dp(6))
        }
        return statusLine
    }

    private fun levelSelector(): View {
        val column = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }

        val modes = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(false to "STANDARD", true to "SPORT").forEach { (sport, label) ->
            val button = smallButton(label) {
                collect()
                levelSport = sport
                render()
            }
            button.setTextColor(
                color(if (sport == levelSport) R.color.accent else R.color.dim)
            )
            modes.addView(button)
        }
        column.addView(modes)

        val levels = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(4), 0, 0)
        }
        for (i in 0..9) {
            val button = smallButton("$i") {
                collect()
                level = i
                render()
            }
            button.setTextColor(color(if (i == level) R.color.accent else R.color.dim))
            levels.addView(button, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        }
        column.addView(levels)
        return column
    }

    // --- row builders ---------------------------------------------------------

    private fun addInt(
        parent: LinearLayout,
        map: MutableMap<String, EditText>,
        key: String,
        label: String,
        source: JSONObject = config
    ) {
        val field = numberField(source.opt(key)?.toString() ?: "", decimal = false)
        map[key] = field
        parent.addView(labelledRow(label, field))
    }

    private fun addFloat(
        parent: LinearLayout,
        map: MutableMap<String, EditText>,
        key: String,
        label: String,
        source: JSONObject = config
    ) {
        val field = numberField(source.opt(key)?.toString() ?: "", decimal = true)
        map[key] = field
        parent.addView(labelledRow(label, field))
    }

    private fun addBool(
        parent: LinearLayout,
        map: MutableMap<String, CheckBox>,
        key: String,
        label: String,
        source: JSONObject = config
    ) {
        val box = CheckBox(activity).apply {
            text = label
            isChecked = source.optBoolean(key, false)
            setTextColor(color(R.color.fg))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setPadding(0, 0, 0, 0)
        }
        map[key] = box
        parent.addView(box)
    }

    private fun labelledRow(label: String, field: EditText): View {
        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(4), 0, dp(4))
        }
        val name = TextView(activity).apply {
            text = label
            setTextColor(color(R.color.dim))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        }
        row.addView(name, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(
            field,
            LinearLayout.LayoutParams(dp(96), LinearLayout.LayoutParams.WRAP_CONTENT)
        )
        return row
    }

    private fun numberField(value: String, decimal: Boolean) = EditText(activity).apply {
        setText(if (decimal) {
            value.toDoubleOrNull()?.let { String.format(Locale.US, "%.1f", it) } ?: value
        } else {
            value
        })
        inputType = if (decimal) {
            InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
        } else {
            InputType.TYPE_CLASS_NUMBER
        }
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
        gravity = Gravity.END
        typeface = android.graphics.Typeface.MONOSPACE
        setPadding(0, 0, 0, 0)
        isSingleLine = true
    }

    private fun smallButton(label: String, onClick: () -> Unit) = Button(activity).apply {
        text = label
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        minWidth = 0
        minimumWidth = 0
        setPadding(dp(12), 0, dp(12), 0)
        setOnClickListener { onClick() }
    }

    private fun sectionHeader(text: String) = TextView(activity).apply {
        this.text = text
        setTextColor(color(R.color.dim))
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
        letterSpacing = 0.15f
        setPadding(0, dp(16), 0, dp(4))
    }

    private fun note(text: String) = TextView(activity).apply {
        this.text = text
        setTextColor(color(R.color.warn))
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        setPadding(0, dp(8), 0, 0)
    }

    private fun setStatus(text: String) {
        statusText = text
        if (::statusLine.isInitialized) statusLine.text = text
    }

    private fun color(id: Int): Int = activity.getColor(id)

    private fun dp(value: Int): Int =
        (value * activity.resources.displayMetrics.density).toInt()
}
