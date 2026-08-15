package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

/**
 * Activity-scoped presentation owner. Local test state lives here; real Bluetooth authority lives
 * only behind [ConnectedDeviceSessionPort]. Unbinding or Activity destruction never stops it.
 */
class TrailActivityController(
    private val localController: CompanionAppController,
    private val permissionReader: NearbyDevicesPermissionReader,
    private val notificationPermissionReader: NotificationPermissionReader,
    private val serviceConnector: ConnectedDeviceServiceConnector,
    private val serviceStartupScheduler: BleRuntimeScheduler,
    private val threadVerifier: BleRuntimeThreadVerifier = CreationThreadBleRuntimeVerifier(),
) : TrailUiController, TrailLifecycleController {
    override var state: TrailAppUiState = TrailAppUiState.ChooseMode
        private set

    private var mode: TrailConnectionMode? = null
    private var permissionState = permissionReader.current()
    private var permissionRequestInFlight = false
    private var permissionWasDenied = false
    private var serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
    private var serviceFailure: ConnectedDeviceServiceStartFailure? = null
    private var notificationPermissionState = notificationPermissionReader.current()
    private var serviceRequested = false
    private var serviceLaunchSubmitted = false
    private var binding: ConnectedDeviceServiceBinding? = null
    private var port: ConnectedDeviceSessionPort? = null
    private var portObservation: ConnectedDeviceStateObservation? = null
    private var serviceStartupDeadline: BleReconnectLease? = null
    private var portGeneration = 0L
    private var observer: ((TrailAppUiState) -> Unit)? = null
    private var deliveringObserver = false
    private var bindingGeneration = 0L
    private var activeBindingGeneration = 0L
    private var deferredConnection: Pair<Long, ConnectedDeviceServiceConnection>? = null
    private var lifecycleActive = false
    private var closed = false
    private var deferredLifecycleActive: Boolean? = null
    private var deferredReturnToModeChoice = false
    private var closeDeferred = false

    init {
        requireOwnerThread()
        localController.observe(::onLocalState)
    }

    override fun observe(observer: ((TrailAppUiState) -> Unit)?) {
        requireOwnerThread()
        if (closed || deliveringObserver) return
        this.observer = observer
        deliver(observer, state)
    }

    override fun chooseLocalTestMode() {
        requireOwnerThread()
        if (!canMutate()) return
        stopServiceIfOwned(forceStop = true)
        mode = TrailConnectionMode.LOCAL_TEST
        publish(TrailAppUiState.LocalTest(localController.state))
    }

    override fun chooseBluetoothDeviceMode() {
        requireOwnerThread()
        if (!canMutate()) return
        releaseLocalSession()
        mode = TrailConnectionMode.BLUETOOTH_DEVICE
        permissionRequestInFlight = false
        permissionState = permissionReader.current()
        notificationPermissionState = notificationPermissionReader.current()
        if (permissionState != NearbyDevicesPermissionState.GRANTED) {
            stopServiceIfOwned(forceStop = true)
        }
        serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
        serviceFailure = null
        publishBluetooth()
    }

    override fun returnToModeChoice() {
        requireOwnerThread()
        if (deliveringObserver && !closed) {
            deferredReturnToModeChoice = true
            return
        }
        if (!canMutate()) return
        returnToModeChoiceNow()
    }

    private fun returnToModeChoiceNow() {
        releaseLocalSession()
        stopServiceIfOwned(forceStop = mode == TrailConnectionMode.BLUETOOTH_DEVICE)
        mode = null
        permissionRequestInFlight = false
        publish(TrailAppUiState.ChooseMode)
    }

    override fun chooseLocalDevice() = localOnly { localController.chooseDevice() }
    override fun cancelLocalSelection() = localOnly { localController.cancelSelection() }
    override fun connectLocalDevice(endpointToken: String) = localOnly { localController.connect(endpointToken) }
    override fun disconnectLocalDevice() = localOnly { localController.disconnect() }
    override fun retryLocalSelection() = localOnly { localController.retrySelection() }
    override fun submitLocalAction(request: CompanionActionRequest) = localOnly { localController.submitAction(request) }

    override fun beginNearbyDevicesPermissionRequest(): Boolean {
        requireOwnerThread()
        if (
            !canMutate() || mode != TrailConnectionMode.BLUETOOTH_DEVICE || permissionRequestInFlight ||
            permissionReader.current() != NearbyDevicesPermissionState.MISSING
        ) return false
        permissionState = NearbyDevicesPermissionState.MISSING
        permissionRequestInFlight = true
        publishBluetooth()
        return true
    }

    override fun onNearbyDevicesPermissionResult() {
        requireOwnerThread()
        if (!canMutate() || mode != TrailConnectionMode.BLUETOOTH_DEVICE || !permissionRequestInFlight) return
        permissionRequestInFlight = false
        permissionState = permissionReader.current()
        permissionWasDenied = permissionState != NearbyDevicesPermissionState.GRANTED
        if (permissionState != NearbyDevicesPermissionState.GRANTED) stopServiceIfOwned()
        publishBluetooth()
    }

    override fun refreshPermissionState() {
        requireOwnerThread()
        if (!canMutate()) return
        val previous = permissionState
        permissionState = permissionReader.current()
        notificationPermissionState = notificationPermissionReader.current()
        if (permissionState == NearbyDevicesPermissionState.GRANTED) permissionWasDenied = false
        if (
            mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            previous == NearbyDevicesPermissionState.GRANTED &&
            permissionState != NearbyDevicesPermissionState.GRANTED
        ) {
            port?.refreshPermissionState()
            stopServiceIfOwned()
        }
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    override fun startBluetoothService() {
        requireOwnerThread()
        if (!canMutate() || !lifecycleActive || mode != TrailConnectionMode.BLUETOOTH_DEVICE || serviceRequested) return
        permissionState = permissionReader.current()
        notificationPermissionState = notificationPermissionReader.current()
        val admission = ConnectedDeviceForegroundServicePolicy.admit(
            visibleUserAction = true,
            nearbyPermissionGranted = permissionState == NearbyDevicesPermissionState.GRANTED,
            notificationPermissionState = notificationPermissionState,
        )
        if (!admission.allowed) {
            serviceState = ConnectedDeviceServiceUiState.START_FAILED
            serviceFailure = admission.failure
            publishBluetooth()
            return
        }
        serviceRequested = true
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.STARTING
        serviceFailure = null
        publishBluetooth()
        if (
            !canMutate() || !lifecycleActive || mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            !serviceRequested
        ) return
        val failure = serviceConnector.startFromVisibleUserAction()
        if (failure != null) {
            serviceRequested = false
            serviceLaunchSubmitted = false
            serviceState = ConnectedDeviceServiceUiState.START_FAILED
            serviceFailure = failure
            publishBluetooth()
            return
        }
        serviceLaunchSubmitted = true
        bindToRequestedService()
    }

    override fun scanBluetoothDevices() = bluetoothOnly { it.scan() }
    override fun selectBluetoothDevice(endpointToken: String) = bluetoothOnly { it.authorize(endpointToken) }
    override fun replaceLostPhoneWithBluetoothDevice(endpointToken: String) =
        bluetoothOnly { it.replaceLostPhone(endpointToken) }
    override fun disconnectBluetoothDevice() = bluetoothOnly { it.disconnect() }
    override fun submitBluetoothAction(request: CompanionActionRequest): Boolean {
        requireOwnerThread()
        return if (canMutate() && mode == TrailConnectionMode.BLUETOOTH_DEVICE) {
            port?.submitAction(request) == true
        } else {
            false
        }
    }

    override fun onLifecycleStart() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            deferredLifecycleActive = true
            return
        }
        onLifecycleStartNow()
    }

    private fun onLifecycleStartNow() {
        lifecycleActive = true
        refreshPermissionState()
        if (serviceRequested && binding == null) bindToRequestedService()
    }

    override fun onLifecycleStop() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            deferredLifecycleActive = false
            return
        }
        onLifecycleStopNow()
    }

    private fun onLifecycleStopNow() {
        lifecycleActive = false
        if (serviceRequested && !serviceLaunchSubmitted) {
            serviceRequested = false
            serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
            serviceFailure = null
            if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
        }
        unbindObservation()
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
        observer = null
        localController.observe(null)
        releaseLocalSession()
        unbindObservation()
        // Closing an Activity is observation cleanup only. The user-started service remains owner.
        serviceConnector.close()
    }

    private fun bindToRequestedService() {
        if (closed || !serviceRequested || !lifecycleActive || binding != null) return
        val nextGeneration = nextBindingGeneration()
        if (nextGeneration == null) {
            serviceConnector.stopService()
            serviceRequested = false
            serviceLaunchSubmitted = false
            serviceState = ConnectedDeviceServiceUiState.START_FAILED
            serviceFailure = ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE
            publishBluetooth()
            return
        }
        activeBindingGeneration = nextGeneration
        val next = serviceConnector.bind { connection -> onServiceConnection(nextGeneration, connection) }
        if (next == null) {
            if (activeBindingGeneration == nextGeneration) activeBindingGeneration = 0L
            serviceConnector.stopService()
            serviceRequested = false
            serviceLaunchSubmitted = false
            serviceState = ConnectedDeviceServiceUiState.START_FAILED
            serviceFailure = ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE
            publishBluetooth()
        } else if (
            closed || !serviceRequested || mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            activeBindingGeneration != nextGeneration || binding != null
        ) {
            next.close()
        } else {
            binding = next
        }
    }

    private fun onServiceConnection(generation: Long, connection: ConnectedDeviceServiceConnection) {
        requireOwnerThread()
        if (
            closed || generation != activeBindingGeneration || !serviceRequested ||
            mode != TrailConnectionMode.BLUETOOTH_DEVICE
        ) return
        if (deliveringObserver) {
            deferredConnection = generation to connection
            return
        }
        when (connection) {
            is ConnectedDeviceServiceConnection.Connected -> {
                val nextPort = connection.port
                if (nextPort.generation < 0L) {
                    failServiceConnection()
                    return
                }
                portObservation?.close()
                portObservation = null
                port = nextPort
                portGeneration = nextPort.generation
                serviceState = if (portGeneration == 0L) {
                    ConnectedDeviceServiceUiState.STARTING
                } else {
                    ConnectedDeviceServiceUiState.RUNNING
                }
                serviceFailure = null
                val observation = nextPort.observe { next -> onPortState(nextPort.generation, next) }
                if (
                    observation == null || closed || !lifecycleActive || !serviceRequested ||
                    mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
                    generation != activeBindingGeneration || port !== nextPort
                ) {
                    observation?.close()
                    if (
                        observation == null && !closed && serviceRequested &&
                        mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
                        generation == activeBindingGeneration && port === nextPort
                    ) {
                        failServiceConnection()
                    }
                    return
                }
                portObservation = observation
                if (portGeneration == 0L) {
                    armServiceStartupDeadline(generation, nextPort)
                }
            }
            ConnectedDeviceServiceConnection.Disconnected -> failServiceConnection()
        }
    }

    private fun onPortState(generation: Long, next: TrailAppUiState.BluetoothDevice) {
        requireOwnerThread()
        if (portGeneration == 0L && generation > 0L && port?.generation == generation) {
            portGeneration = generation
            serviceState = ConnectedDeviceServiceUiState.RUNNING
            closeServiceStartupDeadline()
        }
        if (
            closed || mode != TrailConnectionMode.BLUETOOTH_DEVICE || !serviceRequested ||
            generation != portGeneration || port?.generation != generation
        ) return
        publish(
            next.copy(
                serviceState = serviceState,
                notificationPermissionState = notificationPermissionState,
                serviceFailure = null,
            ),
        )
    }

    private fun failServiceConnection() {
        closeServiceStartupDeadline()
        portObservation?.close()
        portObservation = null
        port = null
        portGeneration = 0L
        binding?.close()
        binding = null
        activeBindingGeneration = 0L
        serviceConnector.stopService()
        serviceRequested = false
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.START_FAILED
        serviceFailure = ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE
        publishBluetooth()
    }

    private fun stopServiceIfOwned(forceStop: Boolean = false) {
        val hadRequest = serviceRequested || binding != null || port != null
        unbindObservation()
        serviceRequested = false
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
        serviceFailure = null
        if (hadRequest || forceStop) serviceConnector.stopService()
    }

    private fun unbindObservation() {
        closeServiceStartupDeadline()
        portObservation?.close()
        portObservation = null
        port = null
        portGeneration = 0L
        binding?.close()
        binding = null
        activeBindingGeneration = 0L
    }

    private fun publishBluetooth() {
        val runtime = port?.state?.runtimeState ?: BleRuntimeState.Idle
        val authorization = port?.state?.authorizationState ?: DeviceAuthorizationUiState.None
        publish(
            TrailAppUiState.BluetoothDevice(
                runtimeState = runtime,
                permissionState = permissionState,
                permissionRequestInFlight = permissionRequestInFlight,
                permissionWasDenied = permissionWasDenied,
                authorizationState = authorization,
                serviceState = serviceState,
                notificationPermissionState = notificationPermissionState,
                serviceFailure = serviceFailure,
            ),
        )
    }

    private fun onLocalState(next: CompanionUiState) {
        requireOwnerThread()
        if (!closed && mode == TrailConnectionMode.LOCAL_TEST) publish(TrailAppUiState.LocalTest(next))
    }

    private fun publish(next: TrailAppUiState) {
        state = next
        deliver(observer, next)
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
            val deferred = deferredConnection
            deferredConnection = null
            if (deferred != null && !closed) onServiceConnection(deferred.first, deferred.second)
        }
    }

    private fun releaseLocalSession() {
        when (localController.state) {
            is CompanionUiState.Connected -> localController.disconnect()
            is CompanionUiState.Selecting -> localController.cancelSelection()
            else -> Unit
        }
    }

    private inline fun localOnly(block: () -> Unit) {
        requireOwnerThread()
        if (canMutate() && mode == TrailConnectionMode.LOCAL_TEST) block()
    }

    private inline fun bluetoothOnly(block: (ConnectedDeviceSessionPort) -> Unit) {
        requireOwnerThread()
        if (!canMutate() || mode != TrailConnectionMode.BLUETOOTH_DEVICE) return
        port?.let(block)
    }

    private fun canMutate(): Boolean = !closed && !deliveringObserver

    private fun armServiceStartupDeadline(
        bindingGeneration: Long,
        expectedPort: ConnectedDeviceSessionPort,
    ) {
        closeServiceStartupDeadline()
        val lease = try {
            serviceStartupScheduler.schedule(SERVICE_STARTUP_TIMEOUT_MILLIS) {
                requireOwnerThread()
                if (
                    !closed && serviceRequested && activeBindingGeneration == bindingGeneration &&
                    port === expectedPort && expectedPort.generation == 0L
                ) {
                    failServiceConnection()
                }
            }
        } catch (_: Exception) {
            failServiceConnection()
            return
        }
        if (
            !closed && serviceRequested && activeBindingGeneration == bindingGeneration &&
            port === expectedPort && expectedPort.generation == 0L
        ) {
            serviceStartupDeadline = lease
        } else {
            try {
                lease.close()
            } catch (_: Exception) {
                // The stale deadline owns no service authority.
            }
        }
    }

    private fun closeServiceStartupDeadline() {
        val deadline = serviceStartupDeadline
        serviceStartupDeadline = null
        try {
            deadline?.close()
        } catch (_: Exception) {
            // Binding generation is invalidated separately before stale callbacks can act.
        }
    }

    private fun drainDeferredLifecycle() {
        if (deliveringObserver || closed) return
        if (closeDeferred) {
            closeDeferred = false
            closeNow()
            return
        }
        if (deferredReturnToModeChoice) {
            deferredReturnToModeChoice = false
            returnToModeChoiceNow()
            return
        }
        when (val next = deferredLifecycleActive) {
            true -> {
                deferredLifecycleActive = null
                onLifecycleStartNow()
            }
            false -> {
                deferredLifecycleActive = null
                onLifecycleStopNow()
            }
            null -> Unit
        }
    }

    private fun nextBindingGeneration(): Long? {
        if (bindingGeneration == Long.MAX_VALUE) return null
        bindingGeneration += 1
        return bindingGeneration
    }

    private fun requireOwnerThread() {
        check(threadVerifier.isOwnerThread()) { "Trail Activity state must be used on its owner thread." }
    }

    private companion object {
        const val SERVICE_STARTUP_TIMEOUT_MILLIS = 10_000L
    }
}
