package com.bbshd.middleman

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import kotlin.math.asin
import kotlin.math.floor
import kotlin.math.tan

/**
 * Hill grade from the phone's own orientation sensor.
 *
 * Why the phone rather than the bike: the middleman's MPU-6050 captures its zero
 * at every power-on, so booting on a slope makes that slope "flat" for the rest
 * of the ride. The phone has a fused rotation vector and a gyro, and its zero is
 * set here explicitly on ground the rider knows is level.
 *
 * Mounting matters. This measures how far the device's +Y axis (top of the phone)
 * is tilted out of the horizontal plane, so it assumes the phone lies **flat on
 * the bars with its top pointing forward**. A screen-vertical ("facing the
 * rider") mount puts +Y about 90 degrees out and the signal collapses; [zero]
 * records that and the UI warns about it.
 *
 * The rotation matrix is read directly instead of using the Euler pitch from
 * `getOrientation`, which is only defined in (-90, 90) and flips at the poles.
 */
class HillAssist(
    context: Context,
    private val onReading: (gradePct: Double) -> Unit,
) : SensorEventListener {

    companion object {
        /**
         * Road grade changes over seconds, so the filter is heavy on purpose. The
         * raw signal is dominated by fork and motor vibration, and a level that
         * twitches is a level that commands the motor to twitch.
         */
        private const val SMOOTHING = 0.12

        /** Anything beyond this is a mounting mistake, not a hill. */
        private const val MAX_GRADE_PCT = 40.0

        /**
         * Level for a grade: 1 + one per whole percent, clamped to the Bafang
         * range. Negative grade floors to a value below 1 and clamps, so downhill
         * keeps level 1 like flat ground.
         */
        fun levelFor(gradePct: Double): Int =
            (1 + floor(gradePct).toInt()).coerceIn(1, 9)
    }

    private val sensors = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager
    private val rotation: Sensor? =
        sensors?.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
            ?: sensors?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)

    private val matrix = FloatArray(9)

    private var offsetRad = 0.0
    private var smoothedRad = 0.0
    private var primed = false
    private var rawPitchRad = 0.0

    val available: Boolean get() = rotation != null

    /** What was treated as level, in degrees — the analogue of the bike's boot zero. */
    val zeroDegrees: Double get() = Math.toDegrees(offsetRad)

    fun start() {
        rotation?.let {
            sensors?.registerListener(this, it, SensorManager.SENSOR_DELAY_GAME)
        }
    }

    fun stop() {
        sensors?.unregisterListener(this)
    }

    /** Treat the current attitude as level ground. */
    fun zero() {
        offsetRad = if (primed) smoothedRad else rawPitchRad
        primed = false
        smoothedRad = 0.0
    }

    override fun onSensorChanged(event: SensorEvent) {
        val type = event.sensor.type
        if (type != Sensor.TYPE_ROTATION_VECTOR && type != Sensor.TYPE_GAME_ROTATION_VECTOR) {
            return
        }
        if (event.values.size < 3) return

        SensorManager.getRotationMatrixFromVector(matrix, event.values)

        // The rotation matrix maps device axes to world axes with +Z up, row
        // major. Element [7] is therefore the world-vertical component of the
        // device's +Y axis: sin(tilt) directly, no Euler angles involved.
        rawPitchRad = asin(matrix[7].toDouble().coerceIn(-1.0, 1.0))

        if (!primed) {
            smoothedRad = rawPitchRad
            primed = true
        } else {
            smoothedRad += SMOOTHING * (rawPitchRad - smoothedRad)
        }

        val relative = smoothedRad - offsetRad
        val grade = (tan(relative) * 100.0).coerceIn(-MAX_GRADE_PCT, MAX_GRADE_PCT)
        onReading(grade)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
