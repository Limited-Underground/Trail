package io.github.nbjelanovic.otclient

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.Looper
import java.util.concurrent.atomic.AtomicLong

class TrailConnectedDeviceService : Service() {
    private val binder = LocalBinder()
    private var owner: ConnectedDeviceSessionOwner? = null
    private var destroyed = false

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        assertMainThread()
        if (intent?.action != ACTION_START_FROM_VISIBLE_USER) {
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        if (owner != null) return START_NOT_STICKY
        if (!nearbyPermissionsGranted()) {
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        if (!promoteToForeground()) {
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        val generation = nextServiceGeneration()
        if (generation == null) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        val created = try {
            createOwner(generation)
        } catch (_: RuntimeException) {
            null
        }
        if (created == null) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        owner = created
        binder.attach(owner)
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        assertMainThread()
        if (!destroyed) {
            destroyed = true
            try {
                binder.attach(null)
                val current = owner
                owner = null
                try {
                    current?.close()
                } catch (_: Exception) {
                    // Ownership is invalidated before cleanup; Android service teardown continues.
                }
            } finally {
                stopForeground(STOP_FOREGROUND_REMOVE)
            }
        }
        super.onDestroy()
    }

    private fun createOwner(generation: Long): ConnectedDeviceSessionOwner {
        val facade = AndroidBluetoothGattFacade(applicationContext)
        var controller: TrailAppController? = null
        return try {
            val scheduler = AndroidMainThreadBleRuntimeScheduler()
            val runtime = BleCompanionRuntime(
                facade = facade,
                scheduler = scheduler,
                threadVerifier = AndroidMainThreadBleRuntimeVerifier(),
            )
            val createdController = TrailAppController(
                localController = CompanionAppController(UnavailableProductionLocalTransport),
                bluetoothRuntime = runtime,
                permissionReader = AndroidNearbyDevicesPermissionReader(applicationContext),
                authorizationClient = RuntimeDeviceAuthorizationClaimClient(runtime),
                authorizationScheduler = scheduler,
                bluetoothFacadeCloseable = facade,
            )
            controller = createdController
            ConnectedDeviceSessionOwner(generation, createdController)
        } catch (error: RuntimeException) {
            try {
                if (controller != null) controller.close() else facade.close()
            } catch (_: Exception) {
                // The service cannot retain a partially constructed Bluetooth authority.
            }
            throw error
        }
    }

    private fun promoteToForeground(): Boolean = try {
        createNotificationChannel()
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        true
    } catch (_: SecurityException) {
        false
    } catch (_: IllegalArgumentException) {
        false
    } catch (_: IllegalStateException) {
        false
    }

    private fun createNotificationChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(
            CONNECTED_DEVICE_NOTIFICATION_CHANNEL_ID,
            CONNECTED_DEVICE_NOTIFICATION_CHANNEL_NAME,
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = "Shows that the user-started Bluetooth device service is running."
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val openApp = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            openApp,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CONNECTED_DEVICE_NOTIFICATION_CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher)
            .setContentTitle(CONNECTED_DEVICE_NOTIFICATION_TITLE)
            .setContentText(CONNECTED_DEVICE_NOTIFICATION_TEXT)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setCategory(Notification.CATEGORY_SERVICE)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .build()
    }

    private fun nearbyPermissionsGranted(): Boolean =
        Build.VERSION.SDK_INT >= 31 &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private fun assertMainThread() {
        check(Looper.myLooper() == Looper.getMainLooper()) { "Connected-device service must run on main thread." }
    }

    inner class LocalBinder : Binder(), ConnectedDeviceSessionPort {
        private var attached: ConnectedDeviceSessionOwner? = null
        private var nextObserverToken = 0L
        private val observers = linkedMapOf<Long, (TrailAppUiState.BluetoothDevice) -> Unit>()
        private val ownerObservations = mutableMapOf<Long, ConnectedDeviceStateObservation>()

        internal fun attach(next: ConnectedDeviceSessionOwner?) {
            assertMainThread()
            ownerObservations.values.forEach { it.close() }
            ownerObservations.clear()
            attached = next
            observers.toList().forEach { (token, observer) ->
                val lease = next?.observe(observer)
                if (lease != null) {
                    ownerObservations[token] = lease
                } else if (next == null) {
                    notifyUnavailable(token, observer)
                } else {
                    observers.remove(token)
                }
            }
        }

        override val generation: Long
            get() {
                assertMainThread()
                return attached?.generation ?: 0L
            }

        override val state: TrailAppUiState.BluetoothDevice
            get() {
                assertMainThread()
                return attached?.state ?: unavailableState()
            }

        override fun observe(
            observer: (TrailAppUiState.BluetoothDevice) -> Unit,
        ): ConnectedDeviceStateObservation? {
            assertMainThread()
            if (observers.size >= MAX_BINDER_OBSERVERS || nextObserverToken == Long.MAX_VALUE) return null
            nextObserverToken += 1
            val token = nextObserverToken
            observers[token] = observer
            val ownerObservation = attached?.observe(observer)
            if (ownerObservation != null) {
                ownerObservations[token] = ownerObservation
            } else if (attached == null) {
                if (!notifyUnavailable(token, observer)) return null
            } else {
                observers.remove(token)
                return null
            }
            return ConnectedDeviceStateObservation {
                assertMainThread()
                observers.remove(token)
                ownerObservations.remove(token)?.close()
            }
        }

        override fun refreshPermissionState() {
            assertMainThread()
            attached?.refreshPermissionState()
        }
        override fun scan() {
            assertMainThread()
            attached?.scan()
        }
        override fun authorize(endpointToken: String) {
            assertMainThread()
            attached?.authorize(endpointToken)
        }
        override fun replaceLostPhone(endpointToken: String) {
            assertMainThread()
            attached?.replaceLostPhone(endpointToken)
        }
        override fun disconnect() {
            assertMainThread()
            attached?.disconnect()
        }
        override fun submitAction(request: io.github.nbjelanovic.otprotocol.CompanionActionRequest): Boolean {
            assertMainThread()
            return attached?.submitAction(request) == true
        }

        private fun unavailableState() = TrailAppUiState.BluetoothDevice(
            runtimeState = BleRuntimeState.Inactive,
            permissionState = AndroidNearbyDevicesPermissionReader(applicationContext).current(),
            permissionRequestInFlight = false,
            permissionWasDenied = false,
            authorizationState = DeviceAuthorizationUiState.None,
            serviceState = ConnectedDeviceServiceUiState.STARTING,
        )

        private fun notifyUnavailable(
            token: Long,
            observer: (TrailAppUiState.BluetoothDevice) -> Unit,
        ): Boolean = try {
            observer(unavailableState())
            true
        } catch (_: Exception) {
            observers.remove(token)
            ownerObservations.remove(token)?.close()
            false
        }
    }

    companion object {
        const val ACTION_START_FROM_VISIBLE_USER = "io.github.nbjelanovic.otclient.action.START_CONNECTED_DEVICE"
        private const val NOTIFICATION_ID = 1701
        private const val MAX_BINDER_OBSERVERS = 4
        private val serviceGeneration = AtomicLong(0L)

        private fun nextServiceGeneration(): Long? {
            while (true) {
                val current = serviceGeneration.get()
                if (current == Long.MAX_VALUE) return null
                val next = current + 1L
                if (serviceGeneration.compareAndSet(current, next)) return next
            }
        }
    }
}
