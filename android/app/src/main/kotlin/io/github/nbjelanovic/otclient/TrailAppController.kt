package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

enum class TrailConnectionMode {
    LOCAL_TEST,
    BLUETOOTH_DEVICE,
}

enum class NearbyDevicesPermissionState {
    UNSUPPORTED,
    MISSING,
    GRANTED,
}

fun interface NearbyDevicesPermissionReader {
    fun current(): NearbyDevicesPermissionState
}

sealed interface TrailAppUiState {
    data object ChooseMode : TrailAppUiState

    data class LocalTest(val companionState: CompanionUiState) : TrailAppUiState

    data class BluetoothDevice(
        val runtimeState: BleRuntimeState,
        val permissionState: NearbyDevicesPermissionState,
        val permissionRequestInFlight: Boolean,
        val permissionWasDenied: Boolean,
    ) : TrailAppUiState
}

/**
 * Pure presentation owner for the explicit fake/Bluetooth mode boundary.
 * It never substitutes one transport for the other and releases the current mode before switching.
 */
class TrailAppController(
    private val localController: CompanionAppController,
    private val bluetoothRuntime: BleCompanionRuntime,
    private val permissionReader: NearbyDevicesPermissionReader,
    private val threadVerifier: BleRuntimeThreadVerifier = CreationThreadBleRuntimeVerifier(),
    private val bluetoothFacadeCloseable: AutoCloseable? = null,
) : AutoCloseable {
    var state: TrailAppUiState = TrailAppUiState.ChooseMode
        private set

    private var mode: TrailConnectionMode? = null
    private var permissionState = permissionReader.current()
    private var permissionRequestInFlight = false
    private var permissionWasDenied = false
    private var observer: ((TrailAppUiState) -> Unit)? = null
    private var deliveringObserver = false
    private var closed = false
    private var deferredLifecycleActive: Boolean? = null
    private var closeDeferred = false

    init {
        requireOwnerThread()
        localController.observe(::onLocalState)
        bluetoothRuntime.observe(::onBluetoothState)
        // An injected runtime cannot carry an earlier scan or selected session into this owner.
        bluetoothRuntime.disconnect()
    }

    fun observe(observer: ((TrailAppUiState) -> Unit)?) {
        requireOwnerThread()
        if (deliveringObserver || closed) return
        this.observer = observer
        deliver(observer, state)
    }

    fun chooseLocalTestMode() {
        requireOwnerThread()
        if (!canMutate()) return
        mode = TrailConnectionMode.LOCAL_TEST
        permissionRequestInFlight = false
        bluetoothRuntime.disconnect()
        publish(TrailAppUiState.LocalTest(localController.state))
    }

    fun chooseBluetoothDeviceMode() {
        requireOwnerThread()
        if (!canMutate()) return
        mode = TrailConnectionMode.BLUETOOTH_DEVICE
        releaseLocalSession()
        bluetoothRuntime.disconnect()
        refreshPermissionState()
        publishBluetooth()
    }

    fun returnToModeChoice() {
        requireOwnerThread()
        if (!canMutate()) return
        mode = null
        permissionRequestInFlight = false
        releaseLocalSession()
        bluetoothRuntime.disconnect()
        publish(TrailAppUiState.ChooseMode)
    }

    fun chooseLocalDevice() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.chooseDevice()
    }

    fun cancelLocalSelection() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.cancelSelection()
    }

    fun connectLocalDevice(endpointToken: String) {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.connect(endpointToken)
    }

    fun disconnectLocalDevice() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.disconnect()
    }

    fun retryLocalSelection() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.retrySelection()
    }

    fun submitLocalAction(request: CompanionActionRequest) {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.submitAction(request)
    }

    fun beginNearbyDevicesPermissionRequest(): Boolean {
        requireOwnerThread()
        if (
            mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            !canMutate() ||
            permissionRequestInFlight ||
            permissionReader.current() != NearbyDevicesPermissionState.MISSING
        ) return false
        permissionState = NearbyDevicesPermissionState.MISSING
        permissionRequestInFlight = true
        publishBluetooth()
        return true
    }

    /** Re-reads platform authority; callback result maps are never treated as the permission source of truth. */
    fun onNearbyDevicesPermissionResult() {
        requireOwnerThread()
        if (mode != TrailConnectionMode.BLUETOOTH_DEVICE || !canMutate() || !permissionRequestInFlight) return
        permissionRequestInFlight = false
        val current = permissionReader.current()
        permissionWasDenied = current != NearbyDevicesPermissionState.GRANTED
        permissionState = current
        if (current != NearbyDevicesPermissionState.GRANTED) bluetoothRuntime.disconnect()
        publishBluetooth()
    }

    /** Used after returning from system settings and when the Activity resumes. */
    fun refreshPermissionState() {
        requireOwnerThread()
        if (!canMutate()) return
        val current = permissionReader.current()
        val previous = permissionState
        permissionState = current
        if (
            mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            previous == NearbyDevicesPermissionState.GRANTED &&
            current != NearbyDevicesPermissionState.GRANTED
        ) {
            bluetoothRuntime.disconnect()
        }
        if (current == NearbyDevicesPermissionState.GRANTED) permissionWasDenied = false
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    fun scanBluetoothDevices() {
        requireOwnerThread()
        if (
            mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            canMutate() &&
            !permissionRequestInFlight &&
            permissionState == NearbyDevicesPermissionState.GRANTED
        ) bluetoothRuntime.requestScan()
    }

    fun selectBluetoothDevice(endpointToken: String) {
        requireOwnerThread()
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE && canMutate()) bluetoothRuntime.select(endpointToken)
    }

    fun disconnectBluetoothDevice() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE && canMutate()) bluetoothRuntime.disconnect()
    }

    fun submitBluetoothAction(request: CompanionActionRequest): Boolean {
        requireOwnerThread()
        return mode == TrailConnectionMode.BLUETOOTH_DEVICE && canMutate() && bluetoothRuntime.submitAction(request)
    }

    fun onLifecycleStart() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            deferredLifecycleActive = true
        } else {
            bluetoothRuntime.onLifecycleStart()
        }
    }

    fun onLifecycleStop() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            deferredLifecycleActive = false
        } else {
            bluetoothRuntime.onLifecycleStop()
        }
    }

    override fun close() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            closeDeferred = true
            return
        }
        closeNow()
    }

    private fun closeNow() {
        if (closed) return
        closed = true
        mode = null
        permissionRequestInFlight = false
        deferredLifecycleActive = null
        closeDeferred = false
        releaseLocalSession()
        localController.observe(null)
        bluetoothRuntime.observe(null)
        observer = null
        bluetoothRuntime.close()
        bluetoothFacadeCloseable?.close()
    }

    private fun onLocalState(next: CompanionUiState) {
        requireOwnerThread()
        if (!closed && mode == TrailConnectionMode.LOCAL_TEST) publish(TrailAppUiState.LocalTest(next))
    }

    private fun onBluetoothState(next: BleRuntimeState) {
        requireOwnerThread()
        if (!closed && mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth(next)
    }

    private fun publishBluetooth(runtimeState: BleRuntimeState = bluetoothRuntime.state) {
        publish(
            TrailAppUiState.BluetoothDevice(
                runtimeState = runtimeState,
                permissionState = permissionState,
                permissionRequestInFlight = permissionRequestInFlight,
                permissionWasDenied = permissionWasDenied,
            ),
        )
    }

    private fun releaseLocalSession() {
        when (localController.state) {
            is CompanionUiState.Connected -> localController.disconnect()
            is CompanionUiState.Selecting -> localController.cancelSelection()
            else -> Unit
        }
    }

    private fun publish(next: TrailAppUiState) {
        state = next
        val currentObserver = observer ?: return
        deliver(currentObserver, next)
    }

    private fun deliver(observer: ((TrailAppUiState) -> Unit)?, next: TrailAppUiState) {
        if (observer == null || deliveringObserver) return
        deliveringObserver = true
        try {
            observer(next)
        } catch (_: Exception) {
            if (this.observer === observer) this.observer = null
        } finally {
            deliveringObserver = false
            drainDeferredLifecycle()
        }
    }

    private fun canMutate(): Boolean = !closed && !deliveringObserver

    private fun requireOwnerThread() {
        check(threadVerifier.isOwnerThread()) { "Trail app state must be used on its owner thread." }
    }

    private fun drainDeferredLifecycle() {
        if (deliveringObserver || closed) return
        if (closeDeferred) {
            closeNow()
            return
        }
        val requestedLifecycleState = deferredLifecycleActive
        deferredLifecycleActive = null
        when (requestedLifecycleState) {
            true -> bluetoothRuntime.onLifecycleStart()
            false -> bluetoothRuntime.onLifecycleStop()
            null -> Unit
        }
    }
}
