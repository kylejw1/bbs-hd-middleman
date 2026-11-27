package com.bbshd.middleman

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import android.os.SystemClock

/**
 * The phone's GPS Doppler speed, for the dashboard's speed hero.
 *
 * This is deliberately separate from anything barometric. The slope hero is the
 * controller's grade; the handset barometer is not used for anything in this app,
 * so speed gets its own small source instead of living inside a grade estimator.
 *
 * Doppler speed rather than a position delta: at 1 Hz it is far less noisy, and
 * it is the quantity a moving vehicle can actually report as "speed".
 */
class GpsSpeed(context: Context) {

    companion object {
        /**
         * A fix older than this is treated as absent. A phone in a tunnel or
         * under a bridge keeps its last Doppler value indefinitely; a frozen
         * speed on the bars is worse than a blank one.
         */
        private const val GPS_SPEED_STALE_MS = 5_000L
    }

    private val locations = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
    private val appContext = context.applicationContext

    /** GPS Doppler speed, km/h. NaN until the first fix. */
    var gpsSpeedKph: Double = Double.NaN
        private set

    private var gpsSpeedMs = 0L
    private var gpsRunning = false

    /**
     * GPS speed for the dashboard hero, or null once the fix is stale.
     *
     * `gpsSpeedKph` keeps its last value when the provider stops delivering.
     * The hero asks for this instead, so a tunnel reads "--" rather than
     * freezing at the last speed.
     */
    val freshGpsSpeedKph: Double?
        get() = if (gpsSpeedMs != 0L &&
            SystemClock.elapsedRealtime() - gpsSpeedMs <= GPS_SPEED_STALE_MS
        ) {
            gpsSpeedKph
        } else {
            null
        }

    val usingGps: Boolean get() = gpsRunning

    private val gpsListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            if (location.hasSpeed()) {
                gpsSpeedKph = location.speed.toDouble().coerceAtLeast(0.0) * 3.6
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

    fun stop() {
        if (!gpsRunning) return
        try {
            locations?.removeUpdates(gpsListener)
        } catch (e: SecurityException) {
            // Permission revoked underneath us; nothing to clean up.
        }
        gpsRunning = false
        // Do not let the next resume show the speed from before the pause.
        gpsSpeedKph = Double.NaN
        gpsSpeedMs = 0L
    }
}
