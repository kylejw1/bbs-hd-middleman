package com.bbshd.middleman

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import android.os.SystemClock
import kotlin.math.floor
import kotlin.math.ln

/**
 * Road grade from barometric altitude against distance travelled.
 *
 * Grade is dh/ds, so it needs two things: a height and a distance. The barometer
 * gives height (about 8.4 m per hPa near sea level, and phone sensors resolve a
 * few pascals, so a metre of climb is roughly 12 Pa — enough to see a 5% grade
 * inside ten metres). Distance comes from the bike's speed sensor, falling back
 * to GPS.
 *
 * This replaces an IMU-based estimate on purpose. A phone in a pocket has no
 * fixed attitude relative to the road, so its tilt says nothing about the hill;
 * barometric pressure does not care where the phone is or which way it points.
 * The cost is that it needs the phone to move — a stationary bike cannot measure
 * a slope this way, so the last grade is held while stopped.
 *
 * The slope is a least-squares fit over a travelling window rather than a
 * differentiated reading, because differentiating two noisy quantities produces
 * a much noisier ratio than fitting one against the other.
 */
class BaroGrade(
    context: Context,
    private val onGrade: (gradePct: Double) -> Unit,
) : SensorEventListener {

    companion object {
        /** Slope of the standard atmosphere: metres per e-fold of pressure. */
        private const val SCALE_HEIGHT_M = 8434.5

        /**
         * Shortest distance the window may span. Below this the regression is
         * fitting noise: 10 m at 5% is 0.5 m of climb, about 6 Pa, still well
         * clear of a phone barometer's few-pascal floor.
         */
        private const val MIN_SPAN_M = 10.0

        /**
         * Longest the window may span before old samples are dropped. This is
         * also the response time: a window is a boxcar filter, so a change in
         * grade arrives as a ramp of roughly this length. 8 s settles in about
         * seven seconds while keeping the estimate's own scatter near 0.2 %,
         * which is well inside one assist level.
         */
        private const val MAX_WINDOW_MS = 8_000L

        /** Under roughly 4 km/h no useful distance accumulates. */
        private const val MIN_SPEED_MPS = 1.2

        /** Samples older than this mean the bike has stopped. */
        private const val STOPPED_MS = 2_000L

        private const val BIKE_SPEED_STALE_MS = 3_000L
        private const val GPS_SPEED_STALE_MS = 5_000L

        private const val CAPACITY = 192
        private const val MIN_SAMPLES = 12
        private const val MAX_GRADE_PCT = 40.0

        /**
         * Level for a grade: 1 + one per whole percent, clamped to the Bafang
         * range. Negative grade floors below 1 and clamps, so downhill keeps
         * level 1 like flat ground.
         */
        fun levelFor(gradePct: Double): Int = (1 + floor(gradePct).toInt()).coerceIn(1, 9)
    }

    private val sensors = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager
    private val pressure: Sensor? = sensors?.getDefaultSensor(Sensor.TYPE_PRESSURE)
    private val locations = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
    private val appContext = context.applicationContext

    // Distance/height window. Parallel arrays with a ring index: a few hundred
    // samples at sensor rate would otherwise be a steady drip of allocations.
    private val distBuf = DoubleArray(CAPACITY)
    private val altBuf = DoubleArray(CAPACITY)
    private val timeBuf = LongArray(CAPACITY)
    private var head = 0
    private var count = 0

    private var distanceM = 0.0
    private var lastSampleMs = 0L
    private var lastMovingMs = 0L

    // Three raw pressures for a median, which is what rejects a pocket or a gust
    // without the lag of a long average.
    private var p0 = 0.0
    private var p1 = 0.0
    private var p2 = 0.0

    private var bikeSpeedMps = 0.0
    private var bikeSpeedMs = 0L
    private var gpsSpeedMps = 0.0
    private var gpsSpeedMs = 0L
    private var gpsRunning = false

    private var lastGrade = 0.0

    val available: Boolean get() = pressure != null

    /** Latest barometric pressure, hPa. NaN until the first sample. */
    var pressureHpa: Double = Double.NaN
        private set

    /**
     * Pressure altitude relative to the 1013.25 hPa datum, in metres.
     *
     * This is *not* elevation above sea level: it uses a fixed reference
     * pressure, so weather moving through shifts it by tens of metres over a
     * day. It is stable over the seconds that grade cares about, which is all
     * the estimator uses it for, and it is shown on the dashboard as a live sign
     * that the barometer is working.
     */
    var pressureAltitudeM: Double = Double.NaN
        private set

    /** GPS Doppler speed, km/h. NaN until the first fix. */
    var gpsSpeedKph: Double = Double.NaN
        private set
    val usingGps: Boolean get() = gpsRunning

    private val gpsListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            // Doppler speed, not a position difference: far less noisy at 1 Hz,
            // and it is exactly the quantity a grade needs.
            if (location.hasSpeed()) {
                gpsSpeedMps = location.speed.toDouble().coerceAtLeast(0.0)
                gpsSpeedKph = gpsSpeedMps * 3.6
                gpsSpeedMs = SystemClock.elapsedRealtime()
            }
        }

        @Deprecated("Required by the interface below API 30")
        override fun onStatusChanged(provider: String?, status: Int, extras: android.os.Bundle?) = Unit
        @Deprecated("Required by the interface below API 30")
        override fun onProviderEnabled(provider: String) = Unit
        @Deprecated("Required by the interface below API 30")
        override fun onProviderDisabled(provider: String) = Unit
    }

    fun start() {
        pressure?.let { sensors?.registerListener(this, it, SensorManager.SENSOR_DELAY_UI) }
        startGps()
    }

    fun stop() {
        sensors?.unregisterListener(this)
        stopGps()
    }

    /** Bike speed in km/h, straight from telemetry. */
    fun updateBikeSpeed(kph: Double?) {
        if (kph == null || kph < 0.0) return
        bikeSpeedMps = kph / 3.6
        bikeSpeedMs = SystemClock.elapsedRealtime()
    }

    fun startGps(): Boolean {
        if (gpsRunning) return true
        val manager = locations ?: return false
        if (appContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return false
        }
        return try {
            manager.requestLocationUpdates(
                LocationManager.GPS_PROVIDER, 1_000L, 0f, gpsListener, Looper.getMainLooper()
            )
            gpsRunning = true
            true
        } catch (e: SecurityException) {
            false
        } catch (e: IllegalArgumentException) {
            // No GPS provider on this device.
            false
        }
    }

    private fun stopGps() {
        if (!gpsRunning) return
        try {
            locations?.removeUpdates(gpsListener)
        } catch (e: SecurityException) {
            // Permission revoked underneath us; nothing to clean up.
        }
        gpsRunning = false
    }

    private fun speedMps(now: Long): Double {
        // The bike's sensor is derived from the wheel and is the better source
        // when it is present and fresh; GPS only has to cover the gaps. A bike
        // reported 0 while GPS says otherwise means the controller is not
        // talking, so GPS wins there too.
        if (now - bikeSpeedMs <= BIKE_SPEED_STALE_MS && bikeSpeedMps > 0.3) return bikeSpeedMps
        if (now - gpsSpeedMs <= GPS_SPEED_STALE_MS) return gpsSpeedMps
        if (now - bikeSpeedMs <= BIKE_SPEED_STALE_MS) return bikeSpeedMps
        return 0.0
    }

    /** Height relative to a fixed reference. Only differences matter here. */
    private fun altitudeOf(pressureHpa: Double): Double = -SCALE_HEIGHT_M * ln(pressureHpa / 1013.25)

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type != Sensor.TYPE_PRESSURE) return
        if (event.values.isEmpty()) return

        p2 = p1
        p1 = p0
        p0 = event.values[0].toDouble()
        val median = medianOfThree(p0, p1, p2)
        pressureHpa = median
        pressureAltitudeM = altitudeOf(median)

        val now = SystemClock.elapsedRealtime()
        val speed = speedMps(now)

        if (speed < MIN_SPEED_MPS) {
            lastSampleMs = now
            // Standing still tells us nothing about the slope, and holding the
            // window would let a stale slope outlive the hill it came from.
            if (now - lastMovingMs > STOPPED_MS && count > 0) clearWindow()
            return
        }

        lastMovingMs = now
        val dt = if (lastSampleMs == 0L) 0.0
                 else ((now - lastSampleMs) / 1000.0).coerceAtMost(0.5)
        lastSampleMs = now
        distanceM += speed * dt

        push(distanceM, altitudeOf(median), now)

        val grade = fit(now) ?: return
        lastGrade = grade
        onGrade(grade)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit

    private fun push(dist: Double, alt: Double, now: Long) {
        when {
            count < CAPACITY -> {
                val at = (head + count) % CAPACITY
                distBuf[at] = dist
                altBuf[at] = alt
                timeBuf[at] = now
                count++
            }
            else -> {
                distBuf[head] = dist
                altBuf[head] = alt
                timeBuf[head] = now
                head = (head + 1) % CAPACITY
            }
        }
    }

    private fun clearWindow() {
        head = 0
        count = 0
    }

    /**
     * Least-squares dh/ds over the window, or null when the window is too short
     * to mean anything. Means are subtracted before squaring so a large distance
     * total does not eat the precision of a slowly-climbing altitude.
     */
    private fun fit(now: Long): Double? {
        while (count > 0 && now - timeBuf[head] > MAX_WINDOW_MS) {
            head = (head + 1) % CAPACITY
            count--
        }
        if (count < MIN_SAMPLES) return null

        val first = (head + count - 1) % CAPACITY
        val span = distBuf[first] - distBuf[head]
        if (span < MIN_SPAN_M) return null

        var sumS = 0.0
        var sumH = 0.0
        for (i in 0 until count) {
            val at = (head + i) % CAPACITY
            sumS += distBuf[at]
            sumH += altBuf[at]
        }
        val meanS = sumS / count
        val meanH = sumH / count

        var cov = 0.0
        var varS = 0.0
        for (i in 0 until count) {
            val at = (head + i) % CAPACITY
            val ds = distBuf[at] - meanS
            cov += ds * (altBuf[at] - meanH)
            varS += ds * ds
        }
        if (varS <= 1e-6) return null

        return ((cov / varS) * 100.0).coerceIn(-MAX_GRADE_PCT, MAX_GRADE_PCT)
    }

    private fun medianOfThree(a: Double, b: Double, c: Double): Double {
        val lo = if (a < b) a else b
        val hi = if (a < b) b else a
        return when {
            c < lo -> lo
            c > hi -> hi
            else -> c
        }
    }

    /** Held while climbing stops, so the UI never shows a fabricated zero. */
    val gradePct: Double get() = lastGrade
}
