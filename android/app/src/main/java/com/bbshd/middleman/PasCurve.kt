package com.bbshd.middleman

import org.json.JSONObject
import kotlin.math.roundToInt

/**
 * The PAS assist curve: target current % against pedal cadence.
 *
 * This mirrors `computePasTargetCurrent()` in `web/index.html` exactly, and that
 * is deliberate — it is the third copy of the same model (the firmware's
 * `_computePasCurrent()` for the LCD is another), so a change to one belongs in
 * all of them or the phone, the bike and the web page disagree about the bike.
 *
 * The shape, from the firmware's model:
 *   - below `pasKeepCurrentCadenceRpm` the target is the level's full current;
 *   - from there to `cadence% x 120 rpm` it ramps linearly down;
 *   - past that it holds a floor of `pasKeepCurrentPercent%` of the level current.
 * PAS-disabled levels are flat zero; PAS-variable levels are flat maximum because
 * the throttle, not cadence, modulates them.
 */
object PasCurve {

    /** Nominal BBSHD maximum cadence, as the web page assumes. */
    const val MOTOR_MAX_CADENCE_RPM = 120f

    enum class Mode { DISABLED, VARIABLE, CONSTANT, RAMP }

    data class Parameters(
        val maxPct: Int,
        val minPct: Int,
        val rampStartRpm: Int,
        val rampEndRpm: Int,
        val mode: Mode,
    )

    fun parameters(config: JSONObject, sport: Boolean, level: Int): Parameters? {
        val levels = config.optJSONArray(if (sport) "sportLevels" else "standardLevels")
            ?: return null
        val entry = levels.optJSONObject(level.coerceIn(0, 9)) ?: return null

        val maxPct = entry.optInt("current", 0)
        val maxCadencePct = entry.optInt("cadence", 100)
        val pasEnabled = entry.optBoolean("pas", level > 0)
        val pasVariable = entry.optBoolean("pasVariable", false)

        val keepPct = config.optInt("pasKeepCurrentPercent", 60)
        val keepCadence = config.optInt("pasKeepCurrentCadenceRpm", 70)

        val rampStart = keepCadence
        val rampEnd = ((maxCadencePct * MOTOR_MAX_CADENCE_RPM) / 100f).roundToInt()
        val minPct = ((keepPct * maxPct) / 100f).roundToInt()

        val mode = when {
            !pasEnabled -> Mode.DISABLED
            pasVariable -> Mode.VARIABLE
            keepPct >= 100 || rampEnd <= rampStart -> Mode.CONSTANT
            else -> Mode.RAMP
        }

        return Parameters(maxPct, minPct, rampStart, rampEnd, mode)
    }

    /** Target current % for a given cadence, clamped to 0..100 like the web page. */
    fun currentPct(p: Parameters, cadenceRpm: Float): Float {
        val pct = when (p.mode) {
            Mode.DISABLED -> 0f
            Mode.VARIABLE, Mode.CONSTANT -> p.maxPct.toFloat()
            Mode.RAMP -> when {
                cadenceRpm <= p.rampStartRpm -> p.maxPct.toFloat()
                cadenceRpm >= p.rampEndRpm -> p.minPct.toFloat()
                else -> {
                    val span = (p.rampEndRpm - p.rampStartRpm).coerceAtLeast(1)
                    val ratio = (cadenceRpm - p.rampStartRpm) / span
                    p.maxPct - ratio * (p.maxPct - p.minPct)
                }
            }
        }
        return pct.coerceIn(0f, 100f)
    }
}
