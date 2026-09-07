package io.github.nbjelanovic.otclient

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.os.Build
import android.os.Handler
import android.os.SystemClock
import android.provider.Settings
import java.time.Instant
import java.time.ZoneId

/** Lives only as long as the authenticated session that owns its callback. */
internal class AndroidDisplayClockWatcher private constructor(
    private val context: Context,
    private val handler: Handler,
    private val changed: () -> Unit,
) : BleReconnectLease {
    private var closed = false
    private var receiverRegistered = false
    private var observerRegistered = false
    private var offset = currentOffset()
    private var lastRefreshElapsed = SystemClock.elapsedRealtime()
    private fun currentOffset() = ZoneId.systemDefault().rules.getOffset(Instant.now())
    private fun refreshClock() {
        lastRefreshElapsed = SystemClock.elapsedRealtime()
        changed()
    }
    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (closed) return
            val nextOffset = currentOffset()
            when (intent?.action) {
                Intent.ACTION_TIME_CHANGED, Intent.ACTION_TIMEZONE_CHANGED, Intent.ACTION_LOCALE_CHANGED -> {
                    offset = nextOffset
                    refreshClock()
                }
                Intent.ACTION_TIME_TICK -> if (nextOffset != offset ||
                    SystemClock.elapsedRealtime() - lastRefreshElapsed >= REFRESH_MILLIS) {
                    offset = nextOffset
                    refreshClock()
                }
            }
        }
    }
    private val formatObserver = object : ContentObserver(handler) {
        override fun onChange(selfChange: Boolean) {
            if (!closed) refreshClock()
        }
    }
    private val refresh = object : Runnable {
        override fun run() {
            if (closed) return
            if (SystemClock.elapsedRealtime() - lastRefreshElapsed >= REFRESH_MILLIS) refreshClock()
            if (!closed) handler.postDelayed(this, REFRESH_MILLIS)
        }
    }

    private fun start() {
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_TIME_CHANGED)
            addAction(Intent.ACTION_TIMEZONE_CHANGED)
            addAction(Intent.ACTION_TIME_TICK)
            addAction(Intent.ACTION_LOCALE_CHANGED)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, null, handler, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(receiver, filter, null, handler)
        }
        receiverRegistered = true
        context.contentResolver.registerContentObserver(
            Settings.System.getUriFor(Settings.System.TIME_12_24), false, formatObserver,
        )
        observerRegistered = true
        check(handler.postDelayed(refresh, REFRESH_MILLIS))
    }

    override fun close() {
        if (closed) return
        closed = true
        handler.removeCallbacks(refresh)
        if (receiverRegistered) {
            context.unregisterReceiver(receiver)
            receiverRegistered = false
        }
        if (observerRegistered) {
            context.contentResolver.unregisterContentObserver(formatObserver)
            observerRegistered = false
        }
    }

    companion object {
        // Refresh well before the firmware's 24-hour clock expiration.
        private const val REFRESH_MILLIS = 6 * 60 * 60 * 1000L
        fun create(context: Context, handler: Handler, changed: () -> Unit): BleReconnectLease? {
            val watcher = AndroidDisplayClockWatcher(context, handler, changed)
            return try {
                watcher.start()
                watcher
            } catch (_: RuntimeException) {
                watcher.close()
                null
            }
        }
    }
}
