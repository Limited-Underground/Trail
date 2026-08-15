package io.github.nbjelanovic.otclient

import android.app.ForegroundServiceStartNotAllowedException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Build
import android.os.IBinder

class AndroidConnectedDeviceServiceConnector(private val context: Context) : ConnectedDeviceServiceConnector {
    private val appContext = context.applicationContext
    private var activeBinding: AndroidBinding? = null

    override fun startFromVisibleUserAction(): ConnectedDeviceServiceStartFailure? = try {
        val intent = Intent(appContext, TrailConnectedDeviceService::class.java).apply {
            action = TrailConnectedDeviceService.ACTION_START_FROM_VISIBLE_USER
        }
        appContext.startForegroundService(intent)
        null
    } catch (error: RuntimeException) {
        when {
            Build.VERSION.SDK_INT >= 31 && error is ForegroundServiceStartNotAllowedException ->
                ConnectedDeviceServiceStartFailure.PLATFORM_REJECTED
            error is SecurityException || error is IllegalStateException ->
                ConnectedDeviceServiceStartFailure.PLATFORM_REJECTED
            else -> ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE
        }
    }

    override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? {
        if (activeBinding != null) return null
        val binding = AndroidBinding(observer)
        val accepted = try {
            appContext.bindService(
                Intent(appContext, TrailConnectedDeviceService::class.java),
                binding.connection,
                Context.BIND_AUTO_CREATE,
            )
        } catch (_: SecurityException) {
            false
        } catch (_: IllegalStateException) {
            false
        }
        if (!accepted) return null
        if (binding.isClosed) return null
        activeBinding = binding
        return binding
    }

    override fun stopService() {
        activeBinding?.close()
        try {
            appContext.stopService(Intent(appContext, TrailConnectedDeviceService::class.java))
        } catch (_: SecurityException) {
            // Local ownership is already detached; Android retains final service cleanup authority.
        }
    }

    override fun close() {
        activeBinding?.close()
    }

    private inner class AndroidBinding(
        private var observer: ((ConnectedDeviceServiceConnection) -> Unit)?,
    ) : ConnectedDeviceServiceBinding {
        private var closed = false
        val isClosed: Boolean
            get() = closed
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                if (closed) return
                val port = binder as? ConnectedDeviceSessionPort
                deliver(
                    if (port != null) {
                        ConnectedDeviceServiceConnection.Connected(port)
                    } else {
                        ConnectedDeviceServiceConnection.Disconnected
                    },
                )
            }

            override fun onServiceDisconnected(name: ComponentName?) {
                deliver(ConnectedDeviceServiceConnection.Disconnected)
            }

            override fun onBindingDied(name: ComponentName?) {
                deliver(ConnectedDeviceServiceConnection.Disconnected)
            }

            override fun onNullBinding(name: ComponentName?) {
                deliver(ConnectedDeviceServiceConnection.Disconnected)
            }
        }

        override fun close() {
            if (closed) return
            closed = true
            observer = null
            if (activeBinding === this) activeBinding = null
            try {
                appContext.unbindService(connection)
            } catch (_: IllegalArgumentException) {
                // An already-lost binding owns no service authority.
            }
        }

        private fun deliver(connection: ConnectedDeviceServiceConnection) {
            if (closed) return
            try {
                observer?.invoke(connection)
            } catch (_: Exception) {
                close()
            }
        }
    }

}
