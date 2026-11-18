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
    // Owned but unarmed: no protected exchange or production runtime wiring is enabled.
    private val packetAuthority = BlePacketAuthorityAdapter(this) { android.os.SystemClock.elapsedRealtime() }
    private val binder = LocalBinder()
    private var owner: ConnectedDeviceSessionOwner? = null
    private var foregroundStarted = false
    private var destroyed = false

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        assertMainThread()
        if (intent?.action != ACTION_START_FROM_VISIBLE_USER) {
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        if (foregroundStarted) return START_NOT_STICKY
        if (!nearbyPermissionsGranted()) {
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        if (!promoteToForeground()) {
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        foregroundStarted = true
        // An explicit start can arrive while a foreground-only owner is already bound.
        // Promote the service in that case and keep the same owner.
        val existingOwner = owner
        if (!activateOwner(requireReturningBond = false)) {
            foregroundStarted = false
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelfResult(startId)
        } else if (existingOwner != null) {
            binder.publishCurrentState()
        }
        return START_NOT_STICKY
    }

    private fun activateOwner(requireReturningBond: Boolean): Boolean {
        assertMainThread()
        if (destroyed || !nearbyPermissionsGranted()) return false
        if (owner != null) return true
        val generation = nextServiceGeneration() ?: return false
        val created = try {
            createOwner(generation, requireReturningBond)
        } catch (_: RuntimeException) {
            null
        } ?: return false
        owner = created
        binder.attach(created)
        return true
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        assertMainThread()
        if (!destroyed) {
            destroyed = true
            packetAuthority.invalidate(BlePacketAuthorityAdapter.Invalidation.RESTART_AMBIGUITY)
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
                if (foregroundStarted) stopForeground(STOP_FOREGROUND_REMOVE)
            }
        }
        super.onDestroy()
    }

    private fun createOwner(generation: Long, requireReturningBond: Boolean): ConnectedDeviceSessionOwner? {
        packetAuthority.invalidate(BlePacketAuthorityAdapter.Invalidation.OWNER_REPLACEMENT)
        val facade = AndroidBluetoothGattFacade(applicationContext)
        var controller: TrailAppController? = null
        return try {
            if (requireReturningBond && !facade.canLookForReturningOwner()) {
                facade.close()
                return null
            }
            val scheduler = AndroidMainThreadBleRuntimeScheduler()
            val confirmationClock = { android.os.SystemClock.elapsedRealtime() }
            val evaluationConfirmation = application is EvaluationGroupConfirmationOptIn
            val runtime = BleCompanionRuntime(
                facade = facade,
                scheduler = scheduler,
                threadVerifier = AndroidMainThreadBleRuntimeVerifier(),
                evaluationConfirmationEnabled = evaluationConfirmation,
                confirmationClockMillis = confirmationClock,
            )
            val createdController = TrailAppController(
                localController = CompanionAppController(UnavailableProductionLocalTransport),
                bluetoothRuntime = runtime,
                permissionReader = AndroidNearbyDevicesPermissionReader(applicationContext),
                authorizationClient = RuntimeDeviceAuthorizationClaimClient(runtime),
                authorizationScheduler = scheduler,
                bluetoothFacadeCloseable = facade,
                groupConfirmationAdapter = if (evaluationConfirmation) RuntimeV1GroupConfirmationAdapter(runtime)
                    else DisabledV1GroupConfirmationAdapter,
                groupConfirmationClock = confirmationClock,
            )
            controller = createdController
            val observation = runCatching {
                (application as? ConnectedDeviceSessionObservationProvider)?.createObservation(generation)
            }.getOrNull()
            ConnectedDeviceSessionOwner(generation, createdController, observation)
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
        // Resolve this installed variant's launcher (V1-Test has its own entry),
        // without referencing or including diagnostic code in production.
        val launcher = runCatching {
            packageManager.getLaunchIntentForPackage(packageName)?.component
        }.getOrNull()?.takeIf { it.packageName == packageName && it.className.isNotBlank() }
        val openApp = Intent(this, MainActivity::class.java).apply {
            // Keep the fallback explicit and local if launcher resolution is unavailable.
            if (launcher != null) component = launcher
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
                val lease = next?.observe { observer(servicePresentation(it)) }
                if (lease != null) {
                    ownerObservations[token] = lease
                } else if (next == null) {
                    notifyObserver(token, observer, unavailableState())
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
                return attached?.state?.let(::servicePresentation) ?: unavailableState()
            }

        override fun observe(
            observer: (TrailAppUiState.BluetoothDevice) -> Unit,
        ): ConnectedDeviceStateObservation? {
            assertMainThread()
            if (observers.size >= MAX_BINDER_OBSERVERS || nextObserverToken == Long.MAX_VALUE) return null
            nextObserverToken += 1
            val token = nextObserverToken
            observers[token] = observer
            val ownerObservation = attached?.observe { observer(servicePresentation(it)) }
            if (ownerObservation != null) {
                ownerObservations[token] = ownerObservation
            } else if (attached == null) {
                if (!notifyObserver(token, observer, unavailableState())) return null
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

        override fun activateReturningOwner(): Boolean = activateOwner(requireReturningBond = true)

        override fun refreshPermissionState() {
            assertMainThread()
            attached?.refreshPermissionState()
        }
        override fun scan() {
            assertMainThread()
            attached?.scan()
        }
        override fun retryDiscovery() {
            assertMainThread()
            attached?.retryDiscovery()
        }
        override fun authorize(endpointToken: String) {
            assertMainThread()
            attached?.authorize(endpointToken)
        }
        override fun disconnect() {
            assertMainThread()
            attached?.disconnect()
        }
        override fun submitText(recipient: V1QuickStatusRecipient, text: String): Boolean {
            assertMainThread()
            return attached?.submitText(recipient, text) == true
        }
        override fun submitQuickStatus(recipient: V1QuickStatusRecipient, status: io.github.nbjelanovic.otprotocol.CompanionQuickStatus): Boolean {
            assertMainThread()
            return attached?.submitQuickStatus(recipient, status) == true
        }
        override fun submitAction(request: io.github.nbjelanovic.otprotocol.CompanionActionRequest): Boolean {
            assertMainThread()
            return attached?.submitAction(request) == true
        }
        override fun refreshGroupConfirmation(): Boolean {
            assertMainThread()
            return attached?.refreshGroupConfirmation() == true
        }
        override fun confirmGroupConfirmation(offer: V1GroupConfirmationOffer): Boolean {
            assertMainThread()
            return attached?.confirmGroupConfirmation(offer) == true
        }
        override fun cancelGroupConfirmation(offer: V1GroupConfirmationOffer): Boolean {
            assertMainThread()
            return attached?.cancelGroupConfirmation(offer) == true
        }
        override fun readRadioRegion(): Boolean {
            assertMainThread()
            return attached?.readRadioRegion() == true
        }
        override fun writeRadioRegion(selectionId: Int): Boolean {
            assertMainThread()
            return attached?.writeRadioRegion(selectionId) == true
        }
        override fun readPublicProfile(): Boolean {
            assertMainThread()
            return attached?.readPublicProfile() == true
        }
        override fun writePublicProfile(name: String, visible: Boolean): Boolean {
            assertMainThread()
            return attached?.writePublicProfile(name,visible) == true
        }
        override fun readDeviceName(): Boolean {
            assertMainThread()
            return attached?.readDeviceName() == true
        }
        override fun writeDeviceName(name: String): Boolean {
            assertMainThread()
            return attached?.writeDeviceName(name) == true
        }
        override fun synchronizeDisplayTime(): Boolean {
            assertMainThread()
            return attached?.synchronizeDisplayTime() == true
        }
        override fun requestFactoryResetConfirmation(): Boolean {
            assertMainThread()
            return attached?.requestFactoryResetConfirmation() == true
        }
        override fun cancelFactoryResetConfirmation() {
            assertMainThread()
            attached?.cancelFactoryResetConfirmation()
        }
        override fun confirmFactoryReset(): Boolean {
            assertMainThread()
            return attached?.confirmFactoryReset() == true
        }
        override fun retryFactoryResetVerification(): Boolean {
            assertMainThread()
            return attached?.retryFactoryResetVerification() == true
        }

        private fun unavailableState() = TrailAppUiState.BluetoothDevice(
            runtimeState = BleRuntimeState.Inactive,
            permissionState = AndroidNearbyDevicesPermissionReader(applicationContext).current(),
            permissionRequestInFlight = false,
            permissionWasDenied = false,
            authorizationState = DeviceAuthorizationUiState.None,
            serviceState = ConnectedDeviceServiceUiState.STARTING,
        )

        internal fun publishCurrentState() {
            val current = state
            observers.toList().forEach { (token, observer) ->
                if (observers[token] === observer) notifyObserver(token, observer, current)
            }
        }

        private fun servicePresentation(state: TrailAppUiState.BluetoothDevice) =
            if (foregroundStarted) state else state.copy(serviceState = ConnectedDeviceServiceUiState.BOUND_ONLY)

        private fun notifyObserver(
            token: Long,
            observer: (TrailAppUiState.BluetoothDevice) -> Unit,
            state: TrailAppUiState.BluetoothDevice,
        ): Boolean = try {
            observer(state)
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
