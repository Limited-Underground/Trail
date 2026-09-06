package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

/** Exact-once service owner for one real controller/runtime/facade graph. */
class ConnectedDeviceSessionOwner(
    override val generation: Long,
    private val controller: TrailServiceController,
    observer: ConnectedDeviceSessionLifecycleObserver? = null,
) : ConnectedDeviceSessionPort, AutoCloseable {
    private var nextObserverToken = 0L
    private val observers = linkedMapOf<Long, (TrailAppUiState.BluetoothDevice) -> Unit>()
    private var closed = false
    private var lifecycleObserver = observer
    private var lastState = conservativeState()

    init {
        try {
            require(generation > 0L)
            controller.observe { next ->
                if (closed) return@observe
                val bluetooth = next as? TrailAppUiState.BluetoothDevice ?: return@observe
                lastState = bluetooth.copy(serviceState = ConnectedDeviceServiceUiState.RUNNING)
                runCatching { lifecycleObserver?.onState(lastState) }
                if (!closed) notifyObservers(lastState)
            }
            controller.chooseBluetoothDeviceMode()
            controller.onLifecycleStart()
        } catch (failure: Exception) {
            // Construction did not transfer controller ownership; the service
            // still closes that graph. Release the observation acquired for it.
            closed = true
            releaseLifecycleObserver()
            throw failure
        }
    }

    override val state: TrailAppUiState.BluetoothDevice
        get() = lastState

    override fun observe(observer: (TrailAppUiState.BluetoothDevice) -> Unit): ConnectedDeviceStateObservation? {
        if (closed || observers.size >= MAX_OBSERVERS || nextObserverToken == Long.MAX_VALUE) return null
        nextObserverToken += 1
        val token = nextObserverToken
        observers[token] = observer
        try {
            observer(lastState)
        } catch (_: Exception) {
            observers.remove(token)
            return null
        }
        return ConnectedDeviceStateObservation { observers.remove(token) }
    }

    override fun refreshPermissionState() {
        if (!closed) controller.refreshPermissionState()
    }

    override fun scan() {
        if (!closed) controller.scanBluetoothDevices()
    }

    override fun authorize(endpointToken: String) {
        if (!closed) controller.selectBluetoothDevice(endpointToken)
    }

    override fun disconnect() {
        if (!closed) controller.disconnectBluetoothDevice()
    }

    override fun submitAction(request: CompanionActionRequest): Boolean =
        !closed && controller.submitBluetoothAction(request)
    override fun readDeviceName(): Boolean = !closed && controller.readDeviceName()
    override fun writeDeviceName(name: String): Boolean = !closed && controller.writeDeviceName(name)
    override fun synchronizeDisplayTime(): Boolean = !closed && controller.synchronizeDisplayTime()

    override fun requestFactoryResetConfirmation(): Boolean =
        !closed && controller.requestFactoryResetConfirmation()

    override fun cancelFactoryResetConfirmation() {
        if (!closed) controller.cancelFactoryResetConfirmation()
    }

    override fun confirmFactoryReset(): Boolean = !closed && controller.confirmFactoryReset()

    override fun retryFactoryResetVerification(): Boolean =
        !closed && controller.retryFactoryResetVerification()

    override fun close() {
        if (closed) return
        closed = true
        observers.clear()
        try {
            controller.observe(null)
        } finally {
            try {
                controller.close()
            } finally {
                lastState = conservativeState()
                releaseLifecycleObserver()
            }
        }
    }

    private fun releaseLifecycleObserver() {
        val previous = lifecycleObserver
        lifecycleObserver = null
        runCatching { previous?.close() }
    }

    private fun conservativeState() = TrailAppUiState.BluetoothDevice(
        runtimeState = if (closed) BleRuntimeState.Closed else BleRuntimeState.Idle,
        permissionState = NearbyDevicesPermissionState.MISSING,
        permissionRequestInFlight = false,
        permissionWasDenied = false,
        authorizationState = DeviceAuthorizationUiState.None,
        serviceState = if (closed) ConnectedDeviceServiceUiState.START_REQUIRED else ConnectedDeviceServiceUiState.RUNNING,
    )

    private fun notifyObservers(next: TrailAppUiState.BluetoothDevice) {
        observers.toList().forEach { (token, observer) ->
            if (closed) return
            try {
                observer(next)
            } catch (_: Exception) {
                observers.remove(token)
            }
        }
    }

    private companion object {
        const val MAX_OBSERVERS = 4
    }
}
