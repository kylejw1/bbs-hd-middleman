package com.bbshd.middleman

import android.app.Activity
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.util.Locale

/**
 * The serial trace console: `/api/serial-trace?after=<seq>` polled while the tab
 * is visible.
 *
 * Tracing is off by default in the firmware (`DEBUG_TRACE_ENABLED_DEFAULT 0`)
 * because it costs flash and bus time, so this screen owns an explicit start/stop
 * control rather than turning it on implicitly. Polling starts and stops with the
 * tab: an invisible console that keeps polling burns radio and bridge time for
 * nothing.
 */
class DebugScreen(
    private val activity: Activity,
    private val ble: BleClient,
) {

    companion object {
        private const val MAX_LINES = 300

        /** Large enough that the probe returns no entries, only the newest seq. */
        private const val PROBE_CURSOR = 4294967280L

        /**
         * How many entries to actually pull. A full ring is ~25 KB of JSON and
         * the BLE stack drops notifications under that load, so reassembly never
         * completes and the request times out. ~96 entries is a few KB, which
         * survives.
         */
        private const val WINDOW = 96
    }

    private lateinit var output: TextView
    private lateinit var status: TextView
    private lateinit var toggle: Button
    private lateinit var scroll: ScrollView

    private val main = Handler(Looper.getMainLooper())
    private val lines = ArrayDeque<String>()

    /** Incremental cursor: the device returns only entries after this sequence. */
    private var cursor = 0
    private var tracing = false
    private var polling = false
    private var visible = false
    private var dropped = 0

    fun attach(screen: View) {
        output = screen.findViewById(R.id.debugOutput)
        status = screen.findViewById(R.id.debugStatus)
        toggle = screen.findViewById(R.id.debugToggle)
        scroll = screen.findViewById(R.id.debugScroll)

        toggle.setOnClickListener { setTracing(!tracing) }
        screen.findViewById<Button>(R.id.debugFetch).setOnClickListener { poll() }
        screen.findViewById<Button>(R.id.debugClear).setOnClickListener {
            lines.clear()
            render()
        }

        render()
    }

    /**
     * A full ring is 512 entries and roughly 20 KB of JSON. Pushing that
     * repeatedly over BLE does not fit the link: with a timer it starved the
     * telemetry push *and* started failing writes outright
     * ("writeCharacteristic refused", request timeouts), which broke config
     * reads too. So the console fetches one batch on demand instead of
     * streaming. Trading liveness for a link that still works is the right
     * trade -- a debug console that breaks the thing being debugged is useless.
     */
    fun onShown() {
        visible = true
        // No timer means nothing refreshes the header on its own, so pick up the
        // current link state each time the tab is opened.
        setStatus(
            when {
                !ble.isReady() -> "Not connected"
                tracing -> "Tracing on"
                else -> "Tracing off"
            }
        )
        render()
    }

    fun onHidden() {
        visible = false
    }

    private fun setTracing(on: Boolean) {
        if (!ble.isReady()) {
            setStatus("Connect to the middleman first")
            return
        }
        ble.post("/api/debug", "{\"enabled\":$on}") { code, _ ->
            if (code == 200) {
                tracing = on
                setStatus(if (on) "Tracing on — press Fetch" else "Tracing off")
                if (on) {
                    // A fresh cursor, so starting the console shows new traffic
                    // rather than replaying whatever predates the session.
                    cursor = 0
                    lines.clear()
                }
                render()
            } else {
                setStatus("Could not ${if (on) "start" else "stop"} tracing (${code})")
            }
        }
    }

    private fun poll() {
        if (polling || !ble.isReady()) return
        polling = true
        setStatus("Fetching…")

        ble.get("/api/serial-trace?after=$PROBE_CURSOR") probe@{ code, body ->
            val probe = runCatching { JSONObject(body) }.getOrNull()
            if (code != 200 || probe == null) {
                polling = false
                setStatus(if (code < 0) "Fetch failed — no reply" else "Fetch failed ($code)")
                return@probe
            }

            tracing = probe.optBoolean("enabled", tracing)
            dropped = probe.optInt("dropped", dropped)
            val newest = probe.optInt("seq", 0)
            val from = (newest - WINDOW).coerceAtLeast(0)
            Log.i("DebugScreen", "probe seq=$newest from=$from")

            ble.get("/api/serial-trace?after=$from") window@{ code2, body2 ->
                polling = false
                if (code2 != 200) {
                    setStatus(if (code2 < 0) "Fetch failed — no reply" else "Fetch failed ($code2)")
                    return@window
                }

                val doc = runCatching { JSONObject(body2) }.getOrNull()
                if (doc == null) {
                    // A truncated body means notifications were dropped mid-batch.
                    setStatus("Batch lost in transit — fetch again")
                    return@window
                }

                cursor = doc.optInt("seq", cursor)
                val bytes = doc.optJSONArray("bytes")
                var added = 0
                if (bytes != null) {
                    for (i in 0 until bytes.length()) {
                        val entry = bytes.optJSONObject(i) ?: continue
                        append(
                            String.format(
                                Locale.US,
                                "%s  %-3s 0x%02X",
                                stamp(entry.optLong("t")),
                                entry.optString("d"),
                                entry.optInt("b") and 0xFF
                            )
                        )
                        added++
                    }
                }

                val texts = doc.optJSONArray("texts")
                if (texts != null) {
                    for (i in 0 until texts.length()) {
                        val line = texts.optString(i)
                        if (line.isNotEmpty()) append(line)
                    }
                }

                Log.i("DebugScreen", "window added=$added texts=" +
                    (texts?.length() ?: 0) + " seq=${doc.optInt("seq", -1)}")
                setStatus(
                    if (!tracing) "Tracing is off — nothing new"
                    else "Fetched $added entries · $dropped dropped so far"
                )
                render()
            }
        }
    }

    /** Controller uptime as m:ss.mmm — the trace has no wall clock. */
    private fun stamp(ms: Long): String {
        val minutes = ms / 60_000
        val seconds = (ms % 60_000) / 1000
        val millis = ms % 1000
        return String.format(Locale.US, "%d:%02d.%03d", minutes, seconds, millis)
    }

    private fun append(line: String) {
        lines.addLast(line)
        while (lines.size > MAX_LINES) lines.removeFirst()
    }

    private fun render() {
        if (!::output.isInitialized) return

        toggle.text = if (tracing) "Stop" else "Start"

        output.text = if (lines.isEmpty()) {
            "Press Start to turn the firmware's trace on, then Fetch to pull a batch.\n\n" +
                "A full ring is ~20 KB of JSON, which does not stream over BLE without " +
                "starving telemetry and failing writes, so this pulls on demand."
        } else {
            lines.joinToString("\n")
        }

        // Follow the tail; a console that stays pinned to the top is useless.
        scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun setStatus(text: String) {
        if (::status.isInitialized) status.text = text
    }
}
