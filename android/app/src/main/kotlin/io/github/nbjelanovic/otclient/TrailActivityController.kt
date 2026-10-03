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
    private var bindingIsAttachment = false
    private var retiringPendingAttachment = false
    private var bindingSubmissionInFlight = false
    private var observationToken: Any? = null
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
            if (bindingIsAttachment) detachServiceAttachment() else {
                activeBindingGeneration = 0L
                observationToken = null
                port?.refreshPermissionState()
                stopServiceIfOwned()
            }
        }
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    override fun startBluetoothService() {
        requireOwnerThread()
        if (!canMutate() || !lifecycleActive || mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            retiringPendingAttachment) return
        if (serviceRequested) {
            if (!bindingIsAttachment || port != null) return
            retiringPendingAttachment = true
            try {
                // Retire the local pending request before lease cleanup can reenter this controller.
                serviceRequested = false
                serviceLaunchSubmitted = false
                serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
                serviceFailure = null
                unbindObservation()
            } finally {
                retiringPendingAttachment = false
            }
        }
        if (!canMutate() || !lifecycleActive || mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            serviceRequested || binding != null || port != null) return
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
        bindingIsAttachment = false
        serviceRequested = true
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.STARTING
        serviceFailure = null
        publishBluetooth()
        if (
            !canMutate() || !lifecycleActive || mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            !serviceRequested
        ) return
        // Presentation callbacks can change permission after the first visible-action admission.
        permissionState = permissionReader.current()
        notificationPermissionState = notificationPermissionReader.current()
        if (permissionState != NearbyDevicesPermissionState.GRANTED) {
            serviceRequested = false
            serviceState = ConnectedDeviceServiceUiState.START_FAILED
            serviceFailure = ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING
            publishBluetooth()
            return
        }
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
    override fun refreshGroupConfirmation(): Boolean {
        requireOwnerThread()
        return canMutate() && lifecycleActive && mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            port?.refreshGroupConfirmation() == true
    }

    override fun confirmGroupConfirmation(offer: V1GroupConfirmationOffer): Boolean {
        requireOwnerThread()
        return canMutate() && lifecycleActive && mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            port?.confirmGroupConfirmation(offer) == true
    }

    override fun cancelGroupConfirmation(offer: V1GroupConfirmationOffer): Boolean {
        requireOwnerThread()
        return canMutate() && lifecycleActive && mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            port?.cancelGroupConfirmation(offer) == true
    }

    override fun readRadioRegion(): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.readRadioRegion()==true }
    override fun writeRadioRegion(selectionId: Int): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.writeRadioRegion(selectionId)==true }
    override fun readPublicProfile(): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.readPublicProfile()==true }
    override fun writePublicProfile(name: String, visible: Boolean): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.writePublicProfile(name,visible)==true }
    override fun readDeviceName(): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.readDeviceName()==true }
    override fun writeDeviceName(name: String): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.writeDeviceName(name)==true }
    override fun synchronizeDisplayTime(): Boolean { requireOwnerThread(); return canMutate() && mode==TrailConnectionMode.BLUETOOTH_DEVICE && port?.synchronizeDisplayTime()==true }
    override fun selectBluetoothDevice(endpointToken: String) = bluetoothOnly { it.authorize(endpointToken) }
    override fun disconnectBluetoothDevice() = bluetoothOnly { it.disconnect() }
    override fun submitBluetoothAction(request: CompanionActionRequest): Boolean {
        requireOwnerThread()
        return if (canMutate() && mode == TrailConnectionMode.BLUETOOTH_DEVICE) {
            port?.submitAction(request) == true
        } else {
            false
        }
    }
    override fun requestFactoryResetConfirmation(): Boolean {
        requireOwnerThread()
        return canMutate() && mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            port?.requestFactoryResetConfirmation() == true
    }
    override fun cancelFactoryResetConfirmation() = bluetoothOnly { it.cancelFactoryResetConfirmation() }
    override fun confirmFactoryReset(): Boolean {
        requireOwnerThread()
        return canMutate() && mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            port?.confirmFactoryReset() == true
    }
    override fun retryFactoryResetVerification(): Boolean {
        requireOwnerThread()
        return canMutate() && mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            port?.retryFactoryResetVerification() == true
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
        if (
            !closed && lifecycleActive && !retiringPendingAttachment &&
            mode != TrailConnectionMode.LOCAL_TEST && binding == null &&
            permissionState == NearbyDevicesPermissionState.GRANTED
        ) bindToRequestedService(existingOnly = true)
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
        cancelServiceOwnedFactoryResetConfirmation()
        lifecycleActive = false
        if (serviceRequested && !serviceLaunchSubmitted && !bindingIsAttachment) {
            serviceRequested = false
            serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
            serviceFailure = null
            if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
        }
        unbindObservation()
        serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
        serviceFailure = null
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
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
        cancelServiceOwnedFactoryResetConfirmation()
        observer = null
        localController.observe(null)
        releaseLocalSession()
        unbindObservation()
        // Closing an Activity is observation cleanup only. The user-started service remains owner.
        serviceConnector.close()
    }

    private fun bindToRequestedService(existingOnly: Boolean = false) {
        if (closed || !lifecycleActive || binding != null || activeBindingGeneration != 0L) return
        if (!existingOnly && !serviceRequested) return
        bindingIsAttachment = existingOnly
        if (existingOnly) serviceRequested = true
        val nextGeneration = nextBindingGeneration()
        if (nextGeneration == null) {
            failServiceConnection()
            return
        }
        activeBindingGeneration = nextGeneration
        bindingSubmissionInFlight = true
        val next = try {
            val callback = { connection: ConnectedDeviceServiceConnection ->
                onServiceConnection(nextGeneration, connection)
            }
            if (existingOnly) serviceConnector.bindExisting(callback) else serviceConnector.bind(callback)
        } catch (_: Exception) {
            null
        } finally {
            bindingSubmissionInFlight = false
        }
        if (next == null) {
            if (activeBindingGeneration == nextGeneration) failServiceConnection()
        } else if (
            closed || !lifecycleActive || !serviceRequested ||
            activeBindingGeneration != nextGeneration || binding != null
        ) {
            closeObservationLease(next)
        } else {
            binding = next
            val deferred = deferredConnection
            deferredConnection = null
            if (deferred != null) onServiceConnection(deferred.first, deferred.second)
        }
    }

    private fun onServiceConnection(generation: Long, connection: ConnectedDeviceServiceConnection) {
        requireOwnerThread()
        if (
            closed || !lifecycleActive || generation != activeBindingGeneration || !serviceRequested ||
            mode == TrailConnectionMode.LOCAL_TEST
        ) return
        if (deliveringObserver || bindingSubmissionInFlight) {
            deferredConnection = generation to connection
            return
        }
        permissionState = permissionReader.current()
        notificationPermissionState = notificationPermissionReader.current()
        if (permissionState != NearbyDevicesPermissionState.GRANTED) {
            failServiceConnection()
            return
        }
        when (connection) {
            is ConnectedDeviceServiceConnection.Connected -> {
                val nextPort = connection.port
                val ownerGeneration = try {
                    nextPort.generation
                } catch (_: Exception) {
                    failServiceConnection()
                    return
                }
                if (ownerGeneration < 0L || (bindingIsAttachment && ownerGeneration == 0L)) {
                    failServiceConnection()
                    return
                }
                releasePortObservation()
                port = nextPort
                portGeneration = ownerGeneration
                val token = Any()
                observationToken = token
                var admitted = false
                var pendingPublication = false
                val observation = try {
                    nextPort.observe {
                        if (!admitted) pendingPublication = true else {
                            onPortState(generation, nextPort, ownerGeneration, token)
                        }
                    }
                } catch (_: Exception) {
                    null
                }
                if (
                    observation == null || !isCurrentObservation(generation, nextPort, ownerGeneration, token) ||
                    permissionReader.current() != NearbyDevicesPermissionState.GRANTED ||
                    currentPortGeneration(nextPort) != ownerGeneration
                ) {
                    if (isCurrentObservation(generation, nextPort, ownerGeneration, token)) {
                        permissionState = permissionReader.current()
                        failServiceConnection()
                    }
                    closeObservationLease(observation)
                    return
                }
                portObservation = observation
                admitted = true
                mode = TrailConnectionMode.BLUETOOTH_DEVICE
                serviceState = if (ownerGeneration == 0L) ConnectedDeviceServiceUiState.STARTING
                    else ConnectedDeviceServiceUiState.RUNNING
                serviceFailure = null
                // Read current binder state only after the observation lease is accepted.
                if (pendingPublication) onPortState(generation, nextPort, ownerGeneration, token)
                else publishBluetooth()
                if (isCurrentObservation(generation, nextPort, ownerGeneration, token) && ownerGeneration == 0L) {
                    armServiceStartupDeadline(generation, nextPort)
                }
            }
            ConnectedDeviceServiceConnection.Disconnected -> failServiceConnection()
        }
    }

    private fun isCurrentObservation(
        bindingGeneration: Long,
        expectedPort: ConnectedDeviceSessionPort,
        ownerGeneration: Long,
        token: Any,
    ): Boolean = !closed && lifecycleActive && serviceRequested &&
        activeBindingGeneration == bindingGeneration && port === expectedPort &&
        portGeneration == ownerGeneration && observationToken === token

    private fun onPortState(
        bindingGeneration: Long,
        expectedPort: ConnectedDeviceSessionPort,
        ownerGeneration: Long,
        token: Any,
    ) {
        requireOwnerThread()
        if (!isCurrentObservation(bindingGeneration, expectedPort, ownerGeneration, token)) return
        permissionState = permissionReader.current()
        notificationPermissionState = notificationPermissionReader.current()
        if (permissionState != NearbyDevicesPermissionState.GRANTED) {
            failServiceConnection()
            return
        }
        if (currentPortGeneration(expectedPort) != ownerGeneration) {
            // A binder can acquire or replace its owner. Admit a fresh observation for that owner.
            onServiceConnection(bindingGeneration, ConnectedDeviceServiceConnection.Connected(expectedPort))
            return
        }
        publishBluetooth()
    }

    private fun currentPortGeneration(expectedPort: ConnectedDeviceSessionPort): Long? = try {
        expectedPort.generation
    } catch (_: Exception) {
        null
    }

    private fun failServiceConnection() {
        if (bindingIsAttachment) {
            detachServiceAttachment()
            return
        }
        unbindObservation()
        serviceRequested = false
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.START_FAILED
        serviceFailure = ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE
        serviceConnector.stopService()
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    private fun detachServiceAttachment() {
        unbindObservation()
        serviceRequested = false
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
        serviceFailure = null
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    private fun stopServiceIfOwned(forceStop: Boolean = false) {
        val hadRequest = serviceRequested || binding != null || port != null
        activeBindingGeneration = 0L
        observationToken = null
        cancelServiceOwnedFactoryResetConfirmation()
        unbindObservation()
        serviceRequested = false
        serviceLaunchSubmitted = false
        serviceState = ConnectedDeviceServiceUiState.START_REQUIRED
        serviceFailure = null
        if (hadRequest || forceStop) serviceConnector.stopService()
    }

    private fun unbindObservation() {
        val oldBinding = binding
        binding = null
        activeBindingGeneration = 0L
        deferredConnection = null
        releasePortObservation()
        closeObservationLease(oldBinding)
    }

    private fun releasePortObservation() {
        val oldObservation = portObservation
        portObservation = null
        observationToken = null
        port = null
        portGeneration = 0L
        closeServiceStartupDeadline()
        closeObservationLease(oldObservation)
    }

    private fun closeObservationLease(lease: AutoCloseable?) {
        try {
            lease?.close()
        } catch (_: Exception) {
            // A retired observation has no service ownership or callback authority.
        }
    }

    private fun publishBluetooth() {
        val portState = try {
            port?.state
        } catch (_: Exception) {
            failServiceConnection()
            return
        }
        val runtime = portState?.runtimeState ?: BleRuntimeState.Idle
        val authorization = portState?.authorizationState ?: DeviceAuthorizationUiState.None
        publish(
            TrailAppUiState.BluetoothDevice(
                runtimeState = runtime,
                permissionState = permissionState,
                permissionRequestInFlight = permissionRequestInFlight,
                permissionWasDenied = permissionWasDenied,
                authorizationState = authorization,
                groupConfirmation = portState?.groupConfirmation ?: V1GroupConfirmationState.Unsupported,
                factoryResetConfirmationVisible = portState?.factoryResetConfirmationVisible == true,
                serviceState = serviceState,
                notificationPermissionState = notificationPermissionState,
                serviceFailure = serviceFailure,
            ),
        )
    }

    private fun cancelServiceOwnedFactoryResetConfirmation() {
        try {
            port?.cancelFactoryResetConfirmation()
        } catch (_: Exception) {
            // The service-side freshness deadline still bounds authority if the in-process adapter fails.
        }
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
