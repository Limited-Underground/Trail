package io.github.nbjelanovic.otclient

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner

/** Bound to the visible activity. The protected board owner is the only binding source. */
internal class AndroidV1ForegroundLocation(
    context: Context,
    private val owner: LifecycleOwner,
    private val boardBinding: () -> LocationBinding?,
    board: PhoneLocationBoard,
    onStateChanged: (PhoneLocationState) -> Unit,
) : DefaultLifecycleObserver, AutoCloseable {
    private val app = context.applicationContext
    private val gps = app.getSystemService(LocationManager::class.java)
    private val main = Handler(Looper.getMainLooper())
    private var visible = false
    private var subscription: LocationListener? = null
    private var closed = false
    private val sharing = V1ForegroundLocation(board, SystemClock::elapsedRealtime) { state ->
        if (state != PhoneLocationState.SHARING && state != PhoneLocationState.WAITING_FOR_FIX) stopGps()
        onStateChanged(state)
    }

    val state: PhoneLocationState get() = sharing.state

    private val poll = object : Runnable {
        override fun run() {
            if (!visible || closed) return
            refresh()
            main.postDelayed(this, 1_000)
        }
    }

    init { owner.lifecycle.addObserver(this) }

    override fun onResume(owner: LifecycleOwner) {
        visible = true
        poll.run()
    }

    override fun onPause(owner: LifecycleOwner) {
        visible = false
        main.removeCallbacks(poll)
        refresh()
    }

    override fun onDestroy(owner: LifecycleOwner) = close()

    fun chooseShare() {
        refreshEnvironment()
        sharing.chooseShare()
        refresh()
    }

    fun resumeSharing() {
        refreshEnvironment()
        sharing.resume()
        refresh()
    }

    fun stopSharing() {
        sharing.stopSharing()
        refresh()
    }

    fun askPermission(launch: (Array<String>) -> Unit) {
        if (visible && !closed && state == PhoneLocationState.ALLOW_PHONE_LOCATION) {
            launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION))
        }
    }

    /** Call on a permission result or a protected binding change as well as the visible tick. */
    fun refresh() {
        refreshEnvironment()
        if (sharing.sampling) {
            if (subscription == null) startGps()
            sharing.tick()
        } else {
            stopGps()
        }
    }

    override fun close() {
        if (closed) return
        closed = true
        visible = false
        main.removeCallbacks(poll)
        sharing.stopSharing()
        stopGps()
        owner.lifecycle.removeObserver(this)
    }

    private fun refreshEnvironment() {
        val foreground = visible && !closed
        sharing.updateEnvironment(PhoneLocationEnvironment(
            foreground = foreground,
            precisePermission = foreground && hasPrecisePermission(),
            gpsEnabled = foreground && gpsEnabled(),
            binding = if (foreground) runCatching(boardBinding).getOrNull() else null,
        ))
    }

    private fun hasPrecisePermission() =
        app.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    private fun gpsEnabled(): Boolean =
        try { gps?.isProviderEnabled(LocationManager.GPS_PROVIDER) == true }
        catch (_: RuntimeException) { false }

    @SuppressLint("MissingPermission")
    private fun startGps() {
        val provider = gps ?: return
        // The permission can be revoked after the environment check.
        if (!visible || !hasPrecisePermission()) return
        val listener = object : LocationListener {
            override fun onLocationChanged(location: Location) {
                if (subscription !== this) return
                refreshEnvironment()
                if (subscription === this && location.provider == LocationManager.GPS_PROVIDER) {
                    sharing.phoneFix(location.latitude, location.longitude, location.elapsedRealtimeNanos / 1_000_000)
                }
            }

            override fun onProviderEnabled(provider: String) { if (subscription === this) refresh() }
            override fun onProviderDisabled(provider: String) { if (subscription === this) refresh() }

            @Deprecated("Required by the location listener on older Android versions")
            override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) = Unit
        }
        subscription = listener
        try {
            provider.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1_000L, 0f, listener, main.looper)
        } catch (_: SecurityException) {
            stopGps()
            refreshEnvironment()
        } catch (_: IllegalArgumentException) {
            stopGps()
            refreshEnvironment()
        }
    }

    private fun stopGps() {
        val listener = subscription ?: return
        subscription = null
        try { gps?.removeUpdates(listener) } catch (_: RuntimeException) { }
    }
}
