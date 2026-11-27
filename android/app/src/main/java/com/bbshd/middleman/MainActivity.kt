package com.bbshd.middleman

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.text.InputType
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import org.json.JSONObject
import java.util.Locale
import kotlin.math.roundToInt

/**
 * The app shell: a header, a tab bar and one inflated view per screen.
 *
 * Screens are plain views swapped by visibility rather than fragments or a
 * ViewPager, which keeps the app free of AndroidX entirely. Adding a screen is
 * one entry in [screenSpecs] plus a layout.
 */
class MainActivity : Activity(), BleClient.Listener {

    companion object {
        private const val REQ_LOCATION = 1001
        private const val TAG = "MainActivity"
        private const val PREFS = "bbshd"
        private const val KEY_PIN = "ble_pin"
        private const val KEY_MAC = "last_mac"
        private const val REQ_PERMISSIONS = 1
        private const val STALE_AFTER_MS = 3_000L
        private const val COLUMNS = 3

        /** Bafang assist levels are 0..9. */
        private const val MAX_ASSIST = 9

        private const val SCREEN_DASHBOARD = 0
        private const val SCREEN_EVENTS = 1
        private const val SCREEN_SETTINGS = 2
        private const val SCREEN_CONFIG = 3
        private const val SCREEN_DEBUG = 4
    }

    private data class ScreenSpec(val titleRes: Int, val layoutRes: Int)

    private val screenSpecs = listOf(
        ScreenSpec(R.string.tab_dashboard, R.layout.screen_dashboard),
        ScreenSpec(R.string.tab_events, R.layout.screen_events),
        ScreenSpec(R.string.tab_settings, R.layout.screen_settings),
        ScreenSpec(R.string.tab_config, R.layout.screen_config),
        ScreenSpec(R.string.tab_debug, R.layout.screen_debug),
    )

    private lateinit var ble: BleClient
    private val main = Handler(Looper.getMainLooper())

    private lateinit var statusText: TextView
    private lateinit var connectButton: Button
    private lateinit var tabBar: LinearLayout
    private lateinit var screenContainer: FrameLayout

    private val screens = ArrayList<View>()
    private val tabs = ArrayList<LinearLayout>()
    private var activeScreen = SCREEN_DASHBOARD

    // Dashboard
    private lateinit var slopeValue: TextView
    private lateinit var speedValue: TextView
    private lateinit var pasValue: TextView
    private lateinit var cadenceValue: TextView
    private lateinit var targetSpeedValue: TextView
    private lateinit var targetCurrentValue: TextView
    private lateinit var curveView: PulseCurveView
    private lateinit var sendAltitudeCheck: CheckBox
    private lateinit var sendAltitudeState: TextView
    private lateinit var gridContainer: LinearLayout

    // Events
    private lateinit var eventsList: LinearLayout

    // Settings
    private lateinit var settingsContainer: LinearLayout

    // Config
    private lateinit var configScreen: ConfigScreen

    // Debug
    private lateinit var debugScreen: DebugScreen
    private val infoRows = LinkedHashMap<String, TextView>()

    /** Stat cells, keyed by the id used in [render]. */
    private val cells = LinkedHashMap<String, TextView>()

    private var state = BleClient.State.IDLE
    private var detail = "Not connected"
    private var lastTelemetryMs = 0L
    private var fwVersion: String? = null
    private var pinDialog: AlertDialog? = null

    private var events: List<EventLine> = emptyList()

    // Last values seen in telemetry. Assist is relative (+/-), so a command has
    // to start from what the bike actually reported, not from what was last
    // tapped -- otherwise a rejected command drifts further from reality.
    private var lastAssist: Int? = null
    private var lastTelemetry: Telemetry? = null

    // GPS speed
    private lateinit var gps: GpsSpeed
    /** Which level table the curve draws from; the mode is read, not switched. */
    private var lastModeSport = false
    private var gpsStarted = false

    /** Last config read, used only to draw the PAS curve. */
    private var bikeConfig: JSONObject? = null
    private var curveSport: Boolean? = null
    private var curveLevel: Int? = null

    private val statDefs = listOf(
        "power" to "WATTS",
        "current" to "AMPS",
        "battery" to "BATTERY",
        "voltage" to "VOLTS",
        "status" to "STATUS",
        "altitude" to "ALT (m)",
        // The phone's own sensors, not the bike's. Kept in the same grid because
        // they answer the same question: what is happening right now.
        "pressure" to "PRESSURE",
    )

    private val ticker = object : Runnable {
        override fun run() {
            updateStatusLine()
            updatePhoneReadouts()
            main.postDelayed(this, 1_000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // A phone on the bars must not sleep mid-ride. This needs no permission,
        // unlike a wake lock.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(R.layout.activity_main)

        statusText = findViewById(R.id.statusText)
        connectButton = findViewById(R.id.connectButton)
        tabBar = findViewById(R.id.tabBar)
        screenContainer = findViewById(R.id.screenContainer)

        buildScreens()
        buildTabs()
        bindDashboard()
        bindSettings()
        selectScreen(SCREEN_DASHBOARD)

        val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
        ble = BleClient(this, this).apply {
            pin = prefs.getString(KEY_PIN, "") ?: ""
            lastMac = prefs.getString(KEY_MAC, null)
        }

        configScreen = ConfigScreen(this, ble) { message -> toast(message) }
        configScreen.attach(screens[SCREEN_CONFIG])

        debugScreen = DebugScreen(this, ble)
        debugScreen.attach(screens[SCREEN_DEBUG])

        connectButton.setOnClickListener { toggleConnection() }
        connectButton.setOnLongClickListener {
            ble.forgetDevice()
            prefs.edit().remove(KEY_MAC).apply()
            toast("Forgot the saved device")
            true
        }

        updateStatusLine()
    }

    override fun onResume() {
        super.onResume()
        main.postDelayed(ticker, 1_000)
        // The speed hero is GPS, and the slope hero is the controller's grade.
        // No handset barometer is sampled anywhere.
        if (!gpsStarted) {
            gpsStarted = true
            ensureGpsStarted()
        }
    }

    override fun onPause() {
        super.onPause()
        if (::debugScreen.isInitialized) debugScreen.onHidden()
        // The ticker only exists to age the status line, so stop it while the
        // activity is not visible. The BLE link is deliberately left up: the app
        // has no foreground service, and dropping to "Disconnected" every time
        // the rider glances at a notification would make it useless.
        main.removeCallbacks(ticker)
        if (gpsStarted) {
            gps.stop()
            gpsStarted = false
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        pinDialog?.dismiss()
        ble.stop()
    }

    // --- screens & tabs -------------------------------------------------------

    private fun buildScreens() {
        screens.clear()
        screenSpecs.forEach { spec ->
            val view = layoutInflater.inflate(spec.layoutRes, screenContainer, false)
            view.visibility = View.GONE
            screenContainer.addView(view)
            screens.add(view)
        }
    }

    private fun buildTabs() {
        tabBar.removeAllViews()
        tabs.clear()

        screenSpecs.forEachIndexed { index, spec ->
            val tab = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                isClickable = true
            }
            val label = TextView(this).apply {
                text = getString(spec.titleRes)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                letterSpacing = 0.1f
                gravity = Gravity.CENTER
                setPadding(dp(14), dp(6), dp(14), dp(4))
            }
            val underline = View(this)

            tab.addView(label)
            tab.addView(
                underline,
                LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(2))
            )
            tab.setOnClickListener { selectScreen(index) }

            tabBar.addView(
                tab,
                LinearLayout.LayoutParams(
                    0,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    1f
                )
            )
            tabs.add(tab)
        }
    }

    private fun selectScreen(index: Int) {
        if (index !in screens.indices) return

        // Polling screens must be told when they lose the screen: a console that
        // keeps polling in the background burns bridge time for nothing.
        if (activeScreen == SCREEN_DEBUG && index != SCREEN_DEBUG) debugScreen.onHidden()
        activeScreen = index

        screens.forEachIndexed { i, view ->
            view.visibility = if (i == index) View.VISIBLE else View.GONE
        }
        tabs.forEachIndexed { i, tab ->
            val active = i == index
            (tab.getChildAt(0) as TextView).setTextColor(
                color(if (active) R.color.accent else R.color.dim)
            )
            tab.getChildAt(1).setBackgroundColor(
                if (active) color(R.color.accent) else 0x00000000
            )
        }

        if (index == SCREEN_EVENTS) renderEvents()
        if (index == SCREEN_SETTINGS) refreshInfo()
        if (index == SCREEN_CONFIG) configScreen.onShown()
        if (index == SCREEN_DEBUG) debugScreen.onShown()
    }

    private fun bindDashboard() {
        val dashboard = screens[SCREEN_DASHBOARD]
        slopeValue = dashboard.findViewById(R.id.slopeValue)
        speedValue = dashboard.findViewById(R.id.speedValue)
        pasValue = dashboard.findViewById(R.id.pasValue)
        cadenceValue = dashboard.findViewById(R.id.cadenceValue)
        targetSpeedValue = dashboard.findViewById(R.id.targetSpeedValue)
        targetCurrentValue = dashboard.findViewById(R.id.targetCurrentValue)
        curveView = dashboard.findViewById(R.id.curveView)
        sendAltitudeCheck = dashboard.findViewById(R.id.sendAltitudeCheck)
        sendAltitudeState = dashboard.findViewById(R.id.sendAltitudeState)
        gridContainer = dashboard.findViewById(R.id.gridContainer)

        buildGrid()

        dashboard.findViewById<Button>(R.id.assistUp).setOnClickListener { nudgeAssist(+1) }

        // setOnClickListener rather than a checked-change listener: refreshAltitudeSetting()
        // sets isChecked from the device, and a change listener would fire on that
        // and post the value straight back.
        sendAltitudeCheck.setOnClickListener { setSendAltitude(sendAltitudeCheck.isChecked) }
        dashboard.findViewById<Button>(R.id.assistDown).setOnClickListener { nudgeAssist(-1) }

        gps = GpsSpeed(this)

        eventsList = screens[SCREEN_EVENTS].findViewById(R.id.eventsList)
        renderEvents()
    }

    // --- settings -------------------------------------------------------------

    private fun bindSettings() {
        settingsContainer = screens[SCREEN_SETTINGS].findViewById(R.id.settingsContainer)
        buildSettings()
    }

    private fun buildSettings() {
        settingsContainer.removeAllViews()
        infoRows.clear()

        settingsContainer.addView(sectionHeader("MIDDLEMAN"))
        listOf(
            "fwVersion" to "Firmware",
            "fwBuild" to "Build",
            "model" to "Model",
            "chip" to "Chip revision",
            "freeHeap" to "Free heap",
            "flashSize" to "Flash",
            "uptime" to "Uptime",
            "wifiMode" to "Wi-Fi mode",
            "staIP" to "Station IP",
            "apIP" to "Hotspot IP",
            "mdnsHost" to "mDNS",
        ).forEach { (key, label) -> addInfoRow(key, label) }

        settingsContainer.addView(sectionHeader("ACTIONS"))
        addAction("Refresh device info") { refreshInfo() }
        addAction("Join Wi-Fi network") { promptWifi() }
        addAction("Bluetooth PIN") { promptBlePin() }
        addAction("Calibrate battery voltage") { promptCalibrate() }
        addAction("Reset controller config") { confirmReset() }
    }

    private fun sectionHeader(text: String) = TextView(this).apply {
        this.text = text
        setTextColor(color(R.color.dim))
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
        letterSpacing = 0.15f
        setPadding(0, dp(16), 0, dp(4))
    }

    private fun addInfoRow(key: String, label: String) {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(3), 0, dp(3))
        }
        val name = TextView(this).apply {
            text = label
            setTextColor(color(R.color.dim))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        }
        val value = TextView(this).apply {
            text = getString(R.string.dash)
            setTextColor(color(R.color.fg))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            gravity = Gravity.END
        }
        row.addView(name)
        row.addView(
            value,
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        )
        settingsContainer.addView(row)
        infoRows[key] = value
    }

    private fun addAction(label: String, onClick: () -> Unit) {
        val button = Button(this).apply {
            text = label
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            setOnClickListener { onClick() }
        }
        settingsContainer.addView(
            button,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
        )
    }

    private fun refreshInfo() {
        if (state != BleClient.State.READY) {
            setInfo("fwVersion", "not connected")
            return
        }
        ble.get("/api/info") { status, body ->
            val info = DeviceInfo.parse(body)
            if (status != 200 || info == null) {
                setInfo("fwVersion", "unavailable (${if (status < 0) "no reply" else "$status"})")
                return@get
            }
            renderInfo(info)
        }
    }

    private fun renderInfo(info: DeviceInfo) {
        setInfo("fwVersion", info.fwVersion)
        setInfo("fwBuild", info.fwBuild)
        setInfo("model", info.model)
        setInfo("chip", info.chipRevision?.let { "rev $it" })
        setInfo("freeHeap", info.freeHeap?.let { bytes(it) })
        setInfo("flashSize", info.flashSize?.let { bytes(it) })
        setInfo("uptime", info.uptimeSec?.let { clock(it * 1000) })
        setInfo("wifiMode", info.wifiMode)
        setInfo("staIP", info.staIP)
        setInfo("apIP", info.apIP)
        setInfo("mdnsHost", info.mdnsHost)
    }

    private fun setInfo(key: String, value: String?) {
        infoRows[key]?.text = value ?: getString(R.string.dash)
    }

    private fun bytes(value: Long): String = when {
        value >= 1024 * 1024 -> String.format(Locale.US, "%.1f MB", value / 1048576.0)
        value >= 1024 -> String.format(Locale.US, "%d KB", value / 1024)
        else -> "$value B"
    }

    private fun dialogColumn(vararg fields: View): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(20), dp(8), dp(20), 0)
        fields.forEach { addView(it) }
    }

    private fun promptWifi() {
        val ssid = EditText(this).apply {
            hint = "Network name"
            inputType = InputType.TYPE_CLASS_TEXT
        }
        val pass = EditText(this).apply {
            hint = "Password"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }

        AlertDialog.Builder(this)
            .setTitle("Join Wi-Fi network")
            .setMessage(
                "The middleman joins your network and gives up its own hotspot. " +
                    "Reach it at http://bbshd.local/ afterwards; if the join fails the " +
                    "hotspot comes back."
            )
            .setView(dialogColumn(ssid, pass))
            .setPositiveButton("Join") { _, _ ->
                val network = ssid.text.toString().trim()
                if (network.isEmpty()) {
                    toast("A network name is required")
                } else {
                    val body = JSONObject()
                        .put("ssid", network)
                        .put("pass", pass.text.toString())
                        .toString()
                    ble.post("/api/wifi", body) { status, _ ->
                        toast(
                            if (status == 200) "Joining $network"
                            else "Wi-Fi join failed (${if (status < 0) "no reply" else "$status"})"
                        )
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun promptBlePin() {
        val input = EditText(this).apply {
            hint = "4-16 characters, empty to clear"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }

        AlertDialog.Builder(this)
            .setTitle("Bluetooth PIN")
            .setMessage(
                "Gates the Bluetooth API only. The Wi-Fi page is never locked, which is " +
                    "how a forgotten PIN is recovered. Leave empty to remove it."
            )
            .setView(dialogColumn(input))
            .setPositiveButton("Save") { _, _ ->
                val pin = input.text.toString().trim()
                if (pin.isNotEmpty() && pin.length !in 4..16) {
                    toast("PIN must be 4-16 characters")
                } else {
                    val body = JSONObject().put("pin", pin).toString()
                    ble.post("/api/ble-pin", body) { status, _ ->
                        if (status == 200) {
                            // Keep the app's own copy in step, or the next connect
                            // presents a stale PIN and burns a lockout attempt.
                            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                                .putString(KEY_PIN, pin).apply()
                            ble.pin = pin
                            toast(
                                if (pin.isEmpty()) "Bluetooth PIN removed"
                                else "Bluetooth PIN set"
                            )
                        } else {
                            toast("Could not set PIN (${if (status < 0) "no reply" else "$status"})")
                        }
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun promptCalibrate() {
        val input = EditText(this).apply {
            hint = "Measured pack voltage"
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
        }

        AlertDialog.Builder(this)
            .setTitle("Calibrate battery voltage")
            .setMessage(
                "Measure the pack at the charge port with a multimeter, then enter the " +
                    "reading. The middleman rescales the controller's ADC so the display " +
                    "matches reality."
            )
            .setView(dialogColumn(input))
            .setPositiveButton("Calibrate") { _, _ ->
                val volts = input.text.toString().trim().toFloatOrNull()
                if (volts == null || volts < 20f || volts > 70f) {
                    // The firmware rejects outside 20-70 V anyway; saying so here
                    // keeps it from looking like a middleman fault.
                    toast("Enter a voltage between 20 and 70 V")
                } else {
                    val body = JSONObject().put("voltage", volts.toDouble()).toString()
                    ble.post("/api/calibrate", body) { status, _ ->
                        toast(
                            if (status == 200) "Calibrated to $volts V"
                            else "Calibration failed (${if (status < 0) "no reply" else "$status"})"
                        )
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun confirmReset() {
        AlertDialog.Builder(this)
            .setTitle("Reset controller config?")
            .setMessage(
                "This writes the controller's factory defaults into EEPROM, replacing " +
                    "every setting including your assist levels and the voltage " +
                    "calibration. It cannot be undone from this app."
            )
            .setPositiveButton("Reset") { _, _ ->
                ble.post("/api/reset") { status, _ ->
                    toast(
                        if (status == 200) "Controller config reset"
                        else "Reset failed (${if (status < 0) "no reply" else "$status"})"
                    )
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // --- permissions ----------------------------------------------------------

    private val neededPermissions: Array<String>
        get() = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private fun hasPermissions(): Boolean = neededPermissions.all {
        checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)

        if (requestCode == REQ_LOCATION) {
            val granted = grantResults.isNotEmpty() &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED
            if (granted) {
                if (!gps.startGps()) toast("GPS speed unavailable")
            } else {
                // The speed hero is GPS-only, so there is no wheel-speed
                // fallback to offer. It will read "--".
                toast("No GPS speed - the speed hero will read --")
            }
            return
        }

        if (requestCode != REQ_PERMISSIONS) return
        if (grantResults.isNotEmpty() &&
            grantResults.all { it == PackageManager.PERMISSION_GRANTED }
        ) {
            ble.start()
        } else {
            toast("Bluetooth permission is required to reach the bike")
        }
    }

    // --- connection -----------------------------------------------------------

    private fun toggleConnection() {
        when (state) {
            BleClient.State.READY, BleClient.State.SCANNING, BleClient.State.CONNECTING ->
                ble.stop()

            else -> {
                if (!hasPermissions()) {
                    requestPermissions(neededPermissions, REQ_PERMISSIONS)
                } else {
                    ble.start()
                }
            }
        }
    }

    override fun onState(state: BleClient.State, detail: String) {
        this.state = state
        this.detail = detail
        connectButton.text = getString(
            when (state) {
                BleClient.State.READY, BleClient.State.SCANNING, BleClient.State.CONNECTING ->
                    R.string.disconnect

                else -> R.string.connect
            }
        )
        if (state == BleClient.State.READY) {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putString(KEY_MAC, ble.lastMac)
                .apply()
            // The curve needs the level parameters, which only /api/config carries.
            // One read per connection; it costs about a second of bridge time.
            refreshAltitudeSetting()
            fetchCurveConfig()
        }
        if (state == BleClient.State.FAILED) {
            toast(detail)
        }
        updateStatusLine()
    }

    override fun onPinRequired() {
        if (isFinishing || isDestroyed) return
        pinDialog?.dismiss()

        val input = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            hint = "PIN"
        }
        pinDialog = AlertDialog.Builder(this)
            .setTitle("Bluetooth PIN")
            .setMessage("This middleman is locked. Enter its PIN to unlock the API.")
            .setView(input)
            .setPositiveButton("Unlock") { _, _ ->
                val value = input.text.toString().trim()
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putString(KEY_PIN, value)
                    .apply()
                ble.submitPin(value)
            }
            .setNegativeButton("Cancel") { _, _ -> ble.stop() }
            .setCancelable(false)
            .show()
    }

    // --- commands -------------------------------------------------------------

    private fun nudgeAssist(delta: Int) {
        val current = lastAssist
        if (current == null) {
            toast("No assist level reported yet")
            return
        }
        val target = (current + delta).coerceIn(0, MAX_ASSIST)
        if (target == current) return

        // Optimistic, then corrected by the next telemetry push (<= 1 s). Waiting
        // for the push would make the button feel broken, and the label is the
        // value the bike reports, so a rejected command corrects itself.
        lastAssist = target

        ble.post("/api/cmd/pas", "{\"level\":$target}") { status, _ ->
            // Revert only on a real error status. A timeout usually means the
            // command landed and the reply was dropped; the next push corrects
            // the number either way, so a toast here would be crying wolf.
            if (status > 0 && status != 200) {
                lastAssist = current
                toast("Assist change failed ($status)")
            }
        }
    }



    // --- telemetry ------------------------------------------------------------

    override fun onTelemetry(json: String) {
        val telemetry = Telemetry.parse(json)
        if (telemetry == null) {
            Log.w(TAG, "unparseable telemetry")
            return
        }
        lastTelemetryMs = SystemClock.elapsedRealtime()
        fwVersion = telemetry.fwVersion
        render(telemetry)
        updateStatusLine()
    }

    override fun onEvents(json: String) {
        val parsed = parseEvents(json)
        if (parsed == events) return
        events = parsed
        if (activeScreen == SCREEN_EVENTS) renderEvents()
    }

    private fun renderEvents() {
        eventsList.removeAllViews()

        if (events.isEmpty()) {
            eventsList.addView(TextView(this).apply {
                text = getString(R.string.no_events)
                setTextColor(color(R.color.dim))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            })
            return
        }

        // The firmware replays the whole ring buffer, so cap what is drawn: a long
        // ride would otherwise rebuild hundreds of rows on every change.
        events.takeLast(200).forEach { event ->
            eventsList.addView(TextView(this).apply {
                text = "${clock(event.timeMs)}  ${event.message}"
                setTextColor(color(if (event.level > 0) R.color.warn else R.color.fg))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
                setPadding(0, dp(3), 0, dp(3))
            })
        }
    }

    private val DASH: String get() = getString(R.string.dash)

    private fun render(t: Telemetry) {
        lastTelemetry = t
        // Everything from the display's own queries -- volts, battery, amps,
        // assist, status -- is only meaningful while that display is actually
        // polling. With it unplugged the firmware has nothing to report, and
        // showing the last constructed value would be a reading of a battery
        // that was never measured.
        val displayOk = t.displayConnected
        // Voltage, battery, amps and watts are the controller's own readings,
        // obtained either by a display's queries or by this bridge polling for
        // them directly. They are valid whenever the firmware says one arrived
        // recently -- a display has nothing to do with it.
        val vehicleOk = t.hasVehicleData || displayOk

        // Speed is the phone's GPS, updated by updatePhoneReadouts(). The bike's
        // wheel speed is not shown and, now that the handset barometer is no
        // longer used, not needed by the app either.
        //
        // The slope hero is the controller's grade and nothing else. bbs-fw
        // computes it from the altitude stream and reports it in the 0xEB
        // snapshot; the middleman forwards it. No DPS310 fallback and no
        // handset barometer -- a number the controller did not compute is not
        // this number. bbs-fw reports permille; the hero shows percent.
        if (t.hasBikeSlope) {
            showSlopePercent(t.bikeSlopePermille?.div(10.0))
        } else {
            showSlopePercent(null)
        }

        // Always show the level the bridge believes is in force: a display's
        // writes, this app's own nudges, and the override all feed it. There is
        // no display query that reports the level back, so hiding it when none of
        // those has happened yet left the hero blank while the bike sat at a
        // perfectly real level.
        pasValue.text = t.assistLevel?.toString() ?: getString(R.string.dash)

        // Actual pedalling cadence, measured on the bike and carried in the same
        // 0xEB snapshot as the targets. Absent means the controller has not
        // reported one on this link -- shown as "--", never as 0 rpm.
        cadenceValue.text = if (t.hasCadence) {
            t.cadenceRpm?.roundToInt()?.toString() ?: DASH
        } else {
            DASH
        }

        // Live targets from bbs-fw's 0xEB snapshot, refreshed once a second and
        // supplemented by its on-change 0xED frames. The middleman applies the
        // 3 s freshness window and gates on the controller link, so with no
        // controller there is nothing to show and both read "--".
        val targetsOk = t.hasTargetTelemetry
        // bbs-fw's target is a percentage of maximum cadence, not rpm -- but the
        // graph's x-axis and the cadence marker are both rpm, so showing a
        // percentage next to them invites the wrong comparison. Converted with
        // the same 120 rpm maximum the curve uses, so the number and the curve
        // agree.
        targetSpeedValue.text = if (targetsOk) {
            t.targetSpeedPercent?.let {
                "${(it * PasCurve.MOTOR_MAX_CADENCE_RPM / 100f).roundToInt()}"
            } ?: DASH
        } else {
            DASH
        }
        targetCurrentValue.text = if (targetsOk) {
            t.targetCurrentPercent?.let { "$it%" } ?: DASH
        } else {
            DASH
        }

        // Refreshed here as well as on the 1 Hz ticker: this is where the bike's
        // barometer reading actually arrives.
        updateAltitudeReadout()

        put("power", if (vehicleOk) t.powerWatts?.let { "$it" } else null)
        put("current", if (vehicleOk) number(t.currentAmps, 1) else null)
        put("battery", if (vehicleOk) t.batteryPercent?.let { "$it%" } else null)
        put("voltage", if (vehicleOk) number(t.batteryVoltage, 1) else null)
        put("status", if (displayOk) t.statusCode?.let { if (it == 0) "OK" else "E$it" } else null)

        // The mode is not shown any more, but the curve still has to know which
        // level table the controller is using.
        lastModeSport = t.operationMode.equals("Sport", ignoreCase = true)

        // Re-select the curve only when the level or mode actually changed; the
        // marker moves on every push. With no cadence frame the curve is still
        // drawn, just without a dot -- better than inventing a position.
        if (bikeConfig != null) {
            val sport = lastModeSport
            val level = curveLevelFor(t)
            if (sport != curveSport || level != curveLevel) {
                curveSport = sport
                curveLevel = level
                refreshCurve()
            }
        }
        curveView.setMarker(
            if (t.hasCadence) t.cadenceRpm?.toFloat() else null,
            if (targetsOk) t.targetCurrentPercent?.toFloat() else null,
        )

        lastAssist = t.assistLevel
    }

    // --- GPS speed ------------------------------------------------------------

    private fun ensureGpsStarted() {
        // The speed hero is GPS, so this is the only sensor start the dashboard
        // needs. Location is requested here as well as in the BLE permission
        // set because Android 12+ does not include location in that set, and
        // without it the hero would silently read "--" on every phone.
        if (gps.usingGps) return
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            if (!gps.startGps()) toast("GPS speed unavailable")
            return
        }
        requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), REQ_LOCATION)
    }

    private fun showSlopePercent(percent: Double?) {
        if (percent == null || percent.isNaN()) {
            slopeValue.text = getString(R.string.dash)
            slopeValue.setTextColor(color(R.color.dim))
            return
        }
        slopeValue.text = signed(percent, 1, "%")
        // Amber once the grade is steep enough to matter for gearing. 10% is a
        // hard climb on a bike; the old 8-degree threshold was ~14%.
        val steep = percent >= 10.0 || percent <= -10.0
        slopeValue.setTextColor(color(if (steep) R.color.warn else R.color.accent))
    }

    private fun updatePhoneReadouts() {
        if (!::gps.isInitialized) return
        // The hero speed is GPS and only GPS. No fallback to the bike's wheel
        // speed: the two are different measurements, and swapping between them
        // without telling the rider is worse than showing "--". The value is
        // dropped once the fix goes stale, so a tunnel reads "--" rather than
        // freezing at the last speed.
        speedValue.text = number(gps.freshGpsSpeedKph, 1)

        // Barometer readings come from the middleman over BLE and nowhere else.
        // The handset has its own sensor, but it measures the air in a pocket or
        // on the bars -- a different place, a different datum, and it disappears
        // when the phone does. A number that silently changes source is worse
        // than no number, so with no barometer on the bike these read "--".
        put("pressure", number(lastTelemetry?.takeIf { it.hasBaro }?.baroPressureHpa, 2))
        updateAltitudeReadout()
    }

    /**
     * Altitude, from the middleman's DPS310 over BLE.
     *
     * No handset fallback: the bike's sensor measures the air the bike is riding
     * through, and the two altimeters do not share a sea-level reference, so
     * swapping between them would move the number by tens of metres for reasons
     * the rider cannot see.
     */
    private fun updateAltitudeReadout() {
        val altitude = lastTelemetry?.takeIf { it.hasBaro }?.baroAltitudeM
        put("altitude", number(altitude, 0))
    }

    /**
     * Read the controller's assist-level parameters once per connection so the
     * curve can be drawn from them. The controller intermittently misses the
     * firmware's 3.5 s read budget on the first attempt (503), exactly as the
     * Config tab sees; without a retry the curve silently never appears.
     */
    private fun fetchCurveConfig(attempt: Int = 1) {
        ble.get("/api/config") config@{ status, body ->
            if (status == 503 && attempt < 3) {
                main.postDelayed({ fetchCurveConfig(attempt + 1) }, 500)
                return@config
            }
            if (status != 200) {
                Log.w(TAG, "curve config read failed ($status)")
                return@config
            }
            val parsed = runCatching { JSONObject(body) }.getOrNull() ?: return@config
            bikeConfig = parsed
            Log.i(TAG, "curve config v${parsed.optInt("configVersion")} loaded")

            // Draw straight away rather than waiting for the next telemetry push.
            curveSport = lastModeSport
            curveLevel = curveLevelFor(lastTelemetry)
            refreshCurve()
        }
    }

    /**
     * Which assist level the curve should model.
     *
     * The controller only reports a level once something has set one. With no
     * display attached it reports 0, and level 0 is PAS-disabled -- the curve is
     * then a flat line on the axis, which reads as "the graph is broken". The
     * configured startup level is what the controller will actually use, so the
     * curve shows that instead: still truthful about the shape of the assist,
     * and visible.
     */
    private fun curveLevelFor(t: Telemetry?): Int {
        val reported = t?.assistLevel ?: 0
        if (reported > 0) return reported
        return bikeConfig?.optInt("assistStartupLevel", 1) ?: 1
    }

    private fun refreshCurve() {
        val config = bikeConfig ?: return
        curveView.setCurve(
            PasCurve.parameters(config, curveSport ?: false, curveLevel ?: 0)
        )
    }

    /**
     * Ask the middleman whether it is forwarding altitude, and whether it has a
     * barometer to forward. Enabling without one is refused by the firmware, so
     * the checkbox is disabled rather than left to fail on tap.
     */
    private fun refreshAltitudeSetting() {
        if (state != BleClient.State.READY) return
        ble.get("/api/altitude") { status, body ->
            if (status != 200) {
                sendAltitudeState.text = if (status < 0) "no reply" else "err $status"
                return@get
            }
            val o = runCatching { JSONObject(body) }.getOrNull() ?: return@get
            val baro = o.optBoolean("baro", false)
            sendAltitudeCheck.isChecked = o.optBoolean("enabled", false)
            sendAltitudeCheck.isEnabled = baro
            sendAltitudeState.text = if (!baro) {
                "no baro"
            } else {
                val m = o.optDouble("altitudeM", Double.NaN)
                if (m.isNaN()) getString(R.string.dash) else String.format(Locale.US, "%.0f m", m)
            }
        }
    }

    private fun setSendAltitude(on: Boolean) {
        if (state != BleClient.State.READY) return
        ble.post("/api/altitude", "{\"enabled\":$on}") { status, _ ->
            if (status != 200) {
                // Revert: the device refused, so the tick would be a lie.
                sendAltitudeCheck.isChecked = !on
                toast(
                    when (status) {
                        409 -> "No barometer on the bike"
                        else -> "Altitude setting failed (${if (status < 0) "no reply" else "$status"})"
                    }
                )
            }
            refreshAltitudeSetting()
        }
    }

    private fun put(key: String, value: String?) {
        cells[key]?.text = value ?: getString(R.string.dash)
    }

    private fun number(value: Double?, decimals: Int): String =
        if (value == null || value.isNaN() || value.isInfinite()) {
            getString(R.string.dash)
        } else {
            String.format(Locale.US, "%.${decimals}f", value)
        }

    /** Always carries a sign, so a descent reads as clearly as a climb. */
    private fun signed(value: Double?, decimals: Int, suffix: String): String =
        if (value == null || value.isNaN() || value.isInfinite()) {
            getString(R.string.dash)
        } else {
            String.format(Locale.US, "%+.${decimals}f%s", value, suffix)
        }

    /** Controller uptime as h:mm:ss or m:ss -- there is no wall clock on the bike. */
    private fun clock(ms: Long): String {
        val totalSeconds = ms / 1000
        val hours = totalSeconds / 3600
        val minutes = (totalSeconds % 3600) / 60
        val seconds = totalSeconds % 60
        return if (hours > 0) {
            String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds)
        } else {
            String.format(Locale.US, "%d:%02d", minutes, seconds)
        }
    }

    // --- status ---------------------------------------------------------------

    private fun updateStatusLine() {
        val text = when {
            state != BleClient.State.READY -> detail
            lastTelemetryMs == 0L -> "Connected — waiting for data"
            else -> {
                val age = SystemClock.elapsedRealtime() - lastTelemetryMs
                if (age > STALE_AFTER_MS) {
                    // A frozen dashboard is the failure mode worth naming: the
                    // link can stay "connected" while the device stopped sending.
                    "Connected — no data for ${age / 1000}s"
                } else {
                    "Live"
                }
            }
        }
        val fw = fwVersion
        statusText.text = if (fw != null) "$text  ·  fw $fw" else text
        statusText.setTextColor(color(R.color.dim))
    }

    // --- view building --------------------------------------------------------

    private fun buildGrid() {
        gridContainer.removeAllViews()
        cells.clear()
        var row: LinearLayout? = null

        statDefs.forEachIndexed { index, (key, label) ->
            if (index % COLUMNS == 0) {
                row = LinearLayout(this).apply {
                    orientation = LinearLayout.HORIZONTAL
                }
                gridContainer.addView(
                    row,
                    LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT
                    )
                )
            }

            val cell = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_HORIZONTAL
                setPadding(0, dp(6), 0, dp(6))
            }

            val value = TextView(this).apply {
                text = getString(R.string.dash)
                setTextColor(color(R.color.fg))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 22f)
                typeface = Typeface.MONOSPACE
                gravity = Gravity.CENTER_HORIZONTAL
            }
            cells[key] = value

            val caption = TextView(this).apply {
                text = label
                setTextColor(color(R.color.dim))
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
                letterSpacing = 0.12f
                gravity = Gravity.CENTER_HORIZONTAL
            }

            cell.addView(value)
            cell.addView(caption)
            row?.addView(
                cell,
                LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            )
        }
    }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    private fun color(id: Int): Int = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
        getColor(id)
    } else {
        @Suppress("DEPRECATION")
        resources.getColor(id)
    }

    private fun toast(message: String) {
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
    }
}
