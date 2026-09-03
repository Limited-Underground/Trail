package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.COMPANION_FACTORY_RESET_CAPABILITY

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

internal const val FACTORY_RESET_CONFIRMATION_FRESHNESS_MILLIS = 30_000L

sealed interface TrailAppUiState {
    data object ChooseMode : TrailAppUiState

    data class LocalTest(val companionState: CompanionUiState) : TrailAppUiState

    data class BluetoothDevice(
        val runtimeState: BleRuntimeState,
        val permissionState: NearbyDevicesPermissionState,
        val permissionRequestInFlight: Boolean,
        val permissionWasDenied: Boolean,
        val authorizationState: DeviceAuthorizationUiState,
        val factoryResetConfirmationVisible: Boolean = false,
        val serviceState: ConnectedDeviceServiceUiState = ConnectedDeviceServiceUiState.START_REQUIRED,
        val notificationPermissionState: NotificationPermissionState = NotificationPermissionState.NOT_REQUIRED,
        val serviceFailure: ConnectedDeviceServiceStartFailure? = null,
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
    private val authorizationClient: DeviceAuthorizationClaimClient,
    private val authorizationScheduler: BleRuntimeScheduler,
    private val threadVerifier: BleRuntimeThreadVerifier = CreationThreadBleRuntimeVerifier(),
    private val bluetoothFacadeCloseable: AutoCloseable? = null,
) : TrailServiceController {
    override var state: TrailAppUiState = TrailAppUiState.ChooseMode
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
    private var authorizationState: DeviceAuthorizationUiState = DeviceAuthorizationUiState.None
    private var factoryResetConfirmation: FactoryResetConfirmationAuthority? = null
    private var factoryResetConfirmationDeadline: BleReconnectLease? = null
    private var factoryResetConfirmationGeneration = 0L
    private var deferredFactoryResetConfirmationExpiryGeneration: Long? = null
    private var authorizationGeneration = 0L
    private var activeAuthorizationGeneration = 0L
    private var authorizationClaimToken: String? = null
    private var authorizationLease: DeviceAuthorizationClaimLease? = null
    private var authorizationTimeoutLease: BleReconnectLease? = null
    private var authorizationCallbackArmed = false
    private val deferredAuthorizationEvents = ArrayDeque<DeviceAuthorizationClaimEvent>()
    private var deferredAuthorizationOverflow = false
    private var deferredAuthorizationInvalidResult = false
    private val deferredAuthorizationCallbacks = ArrayDeque<DeferredAuthorizationCallback>()

    private sealed interface DeferredAuthorizationCallback {
        data class Event(
            val generation: Long,
            val candidate: BleDiscoveredCompanion,
            val purpose: DeviceAuthorizationPurpose,
            val event: DeviceAuthorizationClaimEvent,
        ) : DeferredAuthorizationCallback

        data class Expired(
            val generation: Long,
            val purpose: DeviceAuthorizationPurpose,
        ) : DeferredAuthorizationCallback

        data class InvalidResult(
            val generation: Long,
            val purpose: DeviceAuthorizationPurpose,
        ) : DeferredAuthorizationCallback

        data class Overflow(
            val generation: Long,
            val purpose: DeviceAuthorizationPurpose,
        ) : DeferredAuthorizationCallback
    }

    private data class FactoryResetConfirmationAuthority(
        val endpointToken: String,
        val sessionNonce: Long,
        val generation: Long,
    )

    init {
        requireOwnerThread()
        localController.observe(::onLocalState)
        bluetoothRuntime.observe(::onBluetoothState)
        // An injected runtime cannot carry an earlier scan or selected session into this owner.
        bluetoothRuntime.disconnect()
    }

    override fun observe(observer: ((TrailAppUiState) -> Unit)?) {
        requireOwnerThread()
        if (deliveringObserver || closed) return
        this.observer = observer
        deliver(observer, state)
    }

    override fun chooseLocalTestMode() {
        requireOwnerThread()
        if (!canMutate()) return
        clearFactoryResetConfirmation()
        mode = TrailConnectionMode.LOCAL_TEST
        permissionRequestInFlight = false
        releaseAuthorization(DeviceAuthorizationUiState.None)
        bluetoothRuntime.disconnect()
        publish(TrailAppUiState.LocalTest(localController.state))
    }

    override fun chooseBluetoothDeviceMode() {
        requireOwnerThread()
        if (!canMutate()) return
        clearFactoryResetConfirmation()
        mode = TrailConnectionMode.BLUETOOTH_DEVICE
        releaseLocalSession()
        releaseAuthorization(DeviceAuthorizationUiState.None)
        bluetoothRuntime.disconnect()
        refreshPermissionState()
        publishBluetooth()
    }

    override fun returnToModeChoice() {
        requireOwnerThread()
        if (!canMutate()) return
        clearFactoryResetConfirmation()
        mode = null
        permissionRequestInFlight = false
        releaseLocalSession()
        releaseAuthorization(DeviceAuthorizationUiState.None)
        bluetoothRuntime.disconnect()
        publish(TrailAppUiState.ChooseMode)
    }

    override fun chooseLocalDevice() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.chooseDevice()
    }

    override fun cancelLocalSelection() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.cancelSelection()
    }

    override fun connectLocalDevice(endpointToken: String) {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.connect(endpointToken)
    }

    override fun disconnectLocalDevice() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.disconnect()
    }

    override fun retryLocalSelection() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.retrySelection()
    }

    override fun submitLocalAction(request: CompanionActionRequest) {
        requireOwnerThread()
        if (mode == TrailConnectionMode.LOCAL_TEST && canMutate()) localController.submitAction(request)
    }

    override fun beginNearbyDevicesPermissionRequest(): Boolean {
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
    override fun onNearbyDevicesPermissionResult() {
        requireOwnerThread()
        if (mode != TrailConnectionMode.BLUETOOTH_DEVICE || !canMutate() || !permissionRequestInFlight) return
        permissionRequestInFlight = false
        val current = permissionReader.current()
        permissionWasDenied = current != NearbyDevicesPermissionState.GRANTED
        permissionState = current
        if (current != NearbyDevicesPermissionState.GRANTED) {
            expirePendingAuthorization()
            bluetoothRuntime.disconnect()
        }
        publishBluetooth()
    }

    /** Used after returning from system settings and when the Activity resumes. */
    override fun refreshPermissionState() {
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
            expirePendingAuthorization()
            bluetoothRuntime.disconnect()
        }
        if (current == NearbyDevicesPermissionState.GRANTED) permissionWasDenied = false
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    override fun startBluetoothService() = Unit

    override fun scanBluetoothDevices() {
        requireOwnerThread()
        if (
            mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            canMutate() &&
            !permissionRequestInFlight &&
            permissionState == NearbyDevicesPermissionState.GRANTED
        ) {
            releaseAuthorization(DeviceAuthorizationUiState.None)
            bluetoothRuntime.requestScan()
        }
    }

    override fun selectBluetoothDevice(endpointToken: String) {
        requireOwnerThread()
        beginDeviceAuthorization(endpointToken, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
    }

    override fun disconnectBluetoothDevice() {
        requireOwnerThread()
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE && canMutate()) {
            clearFactoryResetConfirmation()
            releaseAuthorization(DeviceAuthorizationUiState.None)
            bluetoothRuntime.disconnect()
        }
    }

    override fun submitBluetoothAction(request: CompanionActionRequest): Boolean {
        requireOwnerThread()
        return mode == TrailConnectionMode.BLUETOOTH_DEVICE && canMutate() && bluetoothRuntime.submitAction(request)
    }

    override fun requestFactoryResetConfirmation(): Boolean {
        requireOwnerThread()
        if (mode != TrailConnectionMode.BLUETOOTH_DEVICE || !canMutate()) return false
        val ready = bluetoothRuntime.state as? BleRuntimeState.Ready ?: return false
        if ((ready.session.protocolInfo.capabilities and COMPANION_FACTORY_RESET_CAPABILITY) == 0) return false
        clearFactoryResetConfirmation()
        val generation = nextFactoryResetConfirmationGeneration() ?: run {
            publishBluetooth()
            return false
        }
        factoryResetConfirmation = FactoryResetConfirmationAuthority(
            ready.session.companion.endpointToken,
            ready.session.sessionNonce,
            generation,
        )
        val deadline = try {
            authorizationScheduler.schedule(FACTORY_RESET_CONFIRMATION_FRESHNESS_MILLIS) {
                onFactoryResetConfirmationExpired(generation)
            }
        } catch (_: Exception) {
            clearFactoryResetConfirmation()
            publishBluetooth()
            return false
        }
        if (!closed && factoryResetConfirmation?.generation == generation) {
            factoryResetConfirmationDeadline = deadline
        } else {
            try {
                deadline.close()
            } catch (_: Exception) {
                // The returned timer has no authority even if adapter cleanup reports failure.
            }
            return false
        }
        publishBluetooth()
        return true
    }

    override fun cancelFactoryResetConfirmation() {
        requireOwnerThread()
        if (!canMutate() || factoryResetConfirmation == null) return
        clearFactoryResetConfirmation()
        publishBluetooth()
    }

    override fun confirmFactoryReset(): Boolean {
        requireOwnerThread()
        if (mode != TrailConnectionMode.BLUETOOTH_DEVICE || !canMutate()) return false
        val confirmation = factoryResetConfirmation ?: return false
        val ready = bluetoothRuntime.state as? BleRuntimeState.Ready
        val stillExactSession =
            ready?.session?.companion?.endpointToken == confirmation.endpointToken &&
                ready.session.sessionNonce == confirmation.sessionNonce
        clearFactoryResetConfirmation()
        if (!stillExactSession) {
            publishBluetooth()
            return false
        }
        val submitted = bluetoothRuntime.submitFactoryReset()
        if (!submitted) publishBluetooth()
        return submitted
    }

    override fun retryFactoryResetVerification(): Boolean {
        requireOwnerThread()
        return mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            canMutate() &&
            bluetoothRuntime.retryFactoryResetVerification()
    }

    override fun onLifecycleStart() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            deferredLifecycleActive = true
        } else {
            bluetoothRuntime.onLifecycleStart()
        }
    }

    override fun onLifecycleStop() {
        requireOwnerThread()
        if (closed) return
        if (deliveringObserver) {
            deferredLifecycleActive = false
        } else {
            clearFactoryResetConfirmation()
            expirePendingAuthorization()
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
        clearFactoryResetConfirmation()
        releaseLocalSession()
        releaseAuthorization(DeviceAuthorizationUiState.None)
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
        factoryResetConfirmation?.let { confirmation ->
            val ready = next as? BleRuntimeState.Ready
            if (
                ready?.session?.companion?.endpointToken != confirmation.endpointToken ||
                ready.session.sessionNonce != confirmation.sessionNonce
            ) {
                clearFactoryResetConfirmation()
            }
        }
        if (!closed && mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth(next)
    }

    private fun publishBluetooth(runtimeState: BleRuntimeState = bluetoothRuntime.state) {
        publish(
            TrailAppUiState.BluetoothDevice(
                runtimeState = runtimeState,
                permissionState = permissionState,
                permissionRequestInFlight = permissionRequestInFlight,
                permissionWasDenied = permissionWasDenied,
                authorizationState = authorizationState,
                factoryResetConfirmationVisible = factoryResetConfirmation != null,
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
            drainDeferredAuthorizationCallbacks()
            drainDeferredFactoryResetConfirmationExpiry()
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
            false -> {
                clearFactoryResetConfirmation()
                expirePendingAuthorization()
                bluetoothRuntime.onLifecycleStop()
            }
            null -> Unit
        }
    }

    private fun nextFactoryResetConfirmationGeneration(): Long? {
        if (factoryResetConfirmationGeneration == Long.MAX_VALUE) return null
        factoryResetConfirmationGeneration += 1
        return factoryResetConfirmationGeneration
    }

    private fun onFactoryResetConfirmationExpired(generation: Long) {
        requireOwnerThread()
        if (closed || factoryResetConfirmation?.generation != generation) return
        if (deliveringObserver) {
            deferredFactoryResetConfirmationExpiryGeneration = generation
            return
        }
        clearFactoryResetConfirmation()
        if (mode == TrailConnectionMode.BLUETOOTH_DEVICE) publishBluetooth()
    }

    private fun drainDeferredFactoryResetConfirmationExpiry() {
        if (deliveringObserver || closed) return
        val generation = deferredFactoryResetConfirmationExpiryGeneration ?: return
        deferredFactoryResetConfirmationExpiryGeneration = null
        onFactoryResetConfirmationExpired(generation)
    }

    private fun clearFactoryResetConfirmation() {
        factoryResetConfirmation = null
        deferredFactoryResetConfirmationExpiryGeneration = null
        val deadline = factoryResetConfirmationDeadline
        factoryResetConfirmationDeadline = null
        try {
            deadline?.close()
        } catch (_: Exception) {
            // Local reset authority is already invalidated before timer cleanup is attempted.
        }
    }

    private fun beginDeviceAuthorization(endpointToken: String, purpose: DeviceAuthorizationPurpose) {
        if (
            mode != TrailConnectionMode.BLUETOOTH_DEVICE ||
            !canMutate() ||
            permissionState != NearbyDevicesPermissionState.GRANTED
        ) return
        val candidate = bluetoothRuntime.beginAuthorization(endpointToken) ?: return
        releaseAuthorization(DeviceAuthorizationUiState.None, endRuntimeAuthorization = false)
        val generation = nextAuthorizationGeneration()
        if (generation == null) {
            bluetoothRuntime.authorizationEnded()
            authorizationState = DeviceAuthorizationUiState.Unavailable(purpose)
            publishBluetooth()
            return
        }
        activeAuthorizationGeneration = generation
        authorizationState = DeviceAuthorizationUiState.Starting(candidate, purpose)
        authorizationCallbackArmed = false
        deferredAuthorizationEvents.clear()
        deferredAuthorizationOverflow = false
        deferredAuthorizationInvalidResult = false
        publishBluetooth()
        if (!canContinueAuthorizationStart(generation, candidate, purpose)) return
        val lease = try {
            authorizationClient.createClaim(candidate.endpointToken, purpose) { event ->
                onAuthorizationEvent(generation, candidate, purpose, event)
            }
        } catch (_: Exception) {
            null
        }
        if (lease == null) {
            finishAuthorizationUnavailable(purpose)
            return
        }
        if (!canContinueAuthorizationStart(generation, candidate, purpose)) {
            try {
                lease.close()
            } catch (_: Exception) {
                // The injected adapter cannot retain controller authority by failing cleanup.
            }
            return
        }
        authorizationLease = lease
        val started = try {
            lease.start()
        } catch (_: Exception) {
            false
        }
        if (!started) {
            finishAuthorizationUnavailable(purpose)
            return
        }
        if (deferredAuthorizationOverflow || deferredAuthorizationInvalidResult) {
            finishAuthorizationInvalidResult(purpose)
            return
        }
        if (!canRetainAuthorizationCallback(generation, candidate, purpose)) {
            finishAuthorizationLease()
            return
        }
        authorizationCallbackArmed = true
        while (deferredAuthorizationEvents.isNotEmpty() && activeAuthorizationGeneration == generation) {
            processAuthorizationEvent(generation, candidate, purpose, deferredAuthorizationEvents.removeFirst())
        }
    }

    private fun onAuthorizationEvent(
        generation: Long,
        candidate: BleDiscoveredCompanion,
        purpose: DeviceAuthorizationPurpose,
        event: DeviceAuthorizationClaimEvent,
    ) {
        requireOwnerThread()
        if (closed || generation != activeAuthorizationGeneration) return
        if (!validAuthorizationToken(event.claimToken)) {
            if (deliveringObserver) {
                deferAuthorizationCallback(
                    DeferredAuthorizationCallback.InvalidResult(generation, purpose),
                    generation,
                    purpose,
                )
            } else if (!authorizationCallbackArmed) {
                deferredAuthorizationInvalidResult = true
            } else {
                finishAuthorizationInvalidResult(purpose)
            }
            return
        }
        if (deliveringObserver) {
            deferAuthorizationCallback(
                DeferredAuthorizationCallback.Event(generation, candidate, purpose, event),
                generation,
                purpose,
            )
            return
        }
        if (!authorizationCallbackArmed) {
            if (deferredAuthorizationEvents.size >= 4) {
                deferredAuthorizationOverflow = true
            } else {
                deferredAuthorizationEvents.addLast(event)
            }
            return
        }
        processAuthorizationEvent(generation, candidate, purpose, event)
    }

    private fun processAuthorizationEvent(
        generation: Long,
        candidate: BleDiscoveredCompanion,
        purpose: DeviceAuthorizationPurpose,
        event: DeviceAuthorizationClaimEvent,
    ) {
        if (closed || mode != TrailConnectionMode.BLUETOOTH_DEVICE || generation != activeAuthorizationGeneration) return
        when (event) {
            is DeviceAuthorizationClaimEvent.Pending -> {
                val token = event.claimToken
                val current = authorizationState
                if (!validAuthorizationToken(token)) {
                    finishAuthorizationInvalidResult(purpose)
                } else if (current is DeviceAuthorizationUiState.Starting) {
                    authorizationClaimToken = token
                    authorizationState = DeviceAuthorizationUiState.Pending(candidate, purpose)
                    if (
                        !closed &&
                        activeAuthorizationGeneration == generation &&
                        authorizationState is DeviceAuthorizationUiState.Pending
                    ) {
                        armAuthorizationTimeout(generation, purpose)
                    }
                    if (
                        !closed &&
                        activeAuthorizationGeneration == generation &&
                        authorizationState is DeviceAuthorizationUiState.Pending
                    ) {
                        publishBluetooth()
                    }
                } else if (current !is DeviceAuthorizationUiState.Pending || authorizationClaimToken != token) {
                    finishAuthorizationInvalidResult(purpose)
                }
            }
            is DeviceAuthorizationClaimEvent.Accepted -> {
                if (
                    purpose != DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE ||
                    !matchesActiveAuthorizationToken(event.claimToken)
                ) {
                    finishAuthorizationInvalidResult(purpose)
                    return
                }
                finishAuthorizationLease()
                authorizationState = DeviceAuthorizationUiState.Accepted(candidate, purpose)
                if (!bluetoothRuntime.authorizationAccepted(candidate.endpointToken)) publishBluetooth()
            }
            is DeviceAuthorizationClaimEvent.Denied -> {
                if (!matchesActiveAuthorizationToken(event.claimToken)) {
                    finishAuthorizationInvalidResult(purpose)
                    return
                }
                finishAuthorizationDenied(purpose)
            }
            is DeviceAuthorizationClaimEvent.Unavailable -> {
                if (authorizationState !is DeviceAuthorizationUiState.Starting) {
                    finishAuthorizationInvalidResult(purpose)
                } else {
                    finishAuthorizationLease()
                    authorizationState = DeviceAuthorizationUiState.Unavailable(purpose)
                    bluetoothRuntime.authorizationEnded()
                    publishBluetooth()
                }
            }
            is DeviceAuthorizationClaimEvent.Unsupported -> {
                if (authorizationState !is DeviceAuthorizationUiState.Starting) {
                    finishAuthorizationInvalidResult(purpose)
                } else {
                    finishAuthorizationLease()
                    authorizationState = DeviceAuthorizationUiState.Unsupported(purpose)
                    bluetoothRuntime.authorizationEnded()
                    publishBluetooth()
                }
            }
            is DeviceAuthorizationClaimEvent.AuthorityUnknown -> {
                if (!matchesActiveAuthorizationToken(event.claimToken)) {
                    finishAuthorizationInvalidResult(purpose)
                } else {
                    finishAuthorizationLease()
                    authorizationState = DeviceAuthorizationUiState.AuthorityUnknown(purpose)
                    bluetoothRuntime.authorizationEnded()
                    publishBluetooth()
                }
            }
        }
    }

    private fun onAuthorizationExpired(generation: Long, purpose: DeviceAuthorizationPurpose) {
        requireOwnerThread()
        if (closed || generation != activeAuthorizationGeneration) return
        if (deliveringObserver) {
            deferAuthorizationCallback(
                DeferredAuthorizationCallback.Expired(generation, purpose),
                generation,
                purpose,
            )
            return
        }
        finishAuthorizationLease()
        authorizationState = DeviceAuthorizationUiState.Expired(purpose)
        bluetoothRuntime.authorizationEnded()
    }

    private fun armAuthorizationTimeout(generation: Long, purpose: DeviceAuthorizationPurpose) {
        if (authorizationTimeoutLease != null) return
        val timeoutLease = try {
            authorizationScheduler.schedule(DEVICE_AUTHORIZATION_CLAIM_TIMEOUT_MILLIS) {
                onAuthorizationExpired(generation, purpose)
            }
        } catch (_: Exception) {
            finishAuthorizationUnavailable(purpose)
            null
        }
        if (
            timeoutLease != null &&
            !closed &&
            activeAuthorizationGeneration == generation &&
            authorizationState is DeviceAuthorizationUiState.Pending
        ) {
            authorizationTimeoutLease = timeoutLease
        } else {
            try {
                timeoutLease?.close()
            } catch (_: Exception) {
                // The timer no longer has authority even if its adapter reports cleanup failure.
            }
        }
    }

    private fun finishAuthorizationDenied(purpose: DeviceAuthorizationPurpose) {
        finishAuthorizationLease()
        authorizationState = DeviceAuthorizationUiState.Denied(purpose)
        bluetoothRuntime.authorizationEnded()
    }

    private fun finishAuthorizationInvalidResult(purpose: DeviceAuthorizationPurpose) {
        finishAuthorizationLease()
        authorizationState = DeviceAuthorizationUiState.InvalidResult(purpose)
        bluetoothRuntime.authorizationEnded()
    }

    private fun finishAuthorizationUnavailable(purpose: DeviceAuthorizationPurpose) {
        finishAuthorizationLease()
        authorizationState = DeviceAuthorizationUiState.Unavailable(purpose)
        bluetoothRuntime.authorizationEnded()
    }

    private fun finishAuthorizationLease() {
        activeAuthorizationGeneration = 0
        authorizationClaimToken = null
        authorizationCallbackArmed = false
        deferredAuthorizationEvents.clear()
        deferredAuthorizationOverflow = false
        deferredAuthorizationInvalidResult = false
        deferredAuthorizationCallbacks.clear()
        val claim = authorizationLease
        authorizationLease = null
        val timeout = authorizationTimeoutLease
        authorizationTimeoutLease = null
        try {
            claim?.close()
        } catch (_: Exception) {
            // Continue releasing the independently owned timer and runtime authorization state.
        }
        try {
            timeout?.close()
        } catch (_: Exception) {
            // Local authority is already invalidated before cleanup is attempted.
        }
    }

    private fun deferAuthorizationCallback(
        callback: DeferredAuthorizationCallback,
        generation: Long,
        purpose: DeviceAuthorizationPurpose,
    ) {
        if (deferredAuthorizationCallbacks.size >= 8) {
            deferredAuthorizationCallbacks.clear()
            deferredAuthorizationCallbacks.addLast(DeferredAuthorizationCallback.Overflow(generation, purpose))
        } else {
            deferredAuthorizationCallbacks.addLast(callback)
        }
    }

    private fun drainDeferredAuthorizationCallbacks() {
        if (deliveringObserver || closed) return
        while (!deliveringObserver && !closed && deferredAuthorizationCallbacks.isNotEmpty()) {
            when (val callback = deferredAuthorizationCallbacks.removeFirst()) {
                is DeferredAuthorizationCallback.Event -> onAuthorizationEvent(
                    callback.generation,
                    callback.candidate,
                    callback.purpose,
                    callback.event,
                )
                is DeferredAuthorizationCallback.Expired -> onAuthorizationExpired(
                    callback.generation,
                    callback.purpose,
                )
                is DeferredAuthorizationCallback.InvalidResult -> {
                    if (callback.generation == activeAuthorizationGeneration) {
                        finishAuthorizationInvalidResult(callback.purpose)
                    }
                }
                is DeferredAuthorizationCallback.Overflow -> {
                    if (callback.generation == activeAuthorizationGeneration) {
                        finishAuthorizationInvalidResult(callback.purpose)
                    }
                }
            }
        }
    }

    private fun expirePendingAuthorization() {
        val pending = authorizationState
        if (pending is DeviceAuthorizationUiState.Starting || pending is DeviceAuthorizationUiState.Pending) {
            val purpose = when (pending) {
                is DeviceAuthorizationUiState.Starting -> pending.purpose
                is DeviceAuthorizationUiState.Pending -> pending.purpose
                else -> return
            }
            finishAuthorizationLease()
            authorizationState = if (pending is DeviceAuthorizationUiState.Pending) {
                DeviceAuthorizationUiState.AuthorityUnknown(purpose)
            } else {
                DeviceAuthorizationUiState.Unavailable(purpose)
            }
            bluetoothRuntime.authorizationEnded()
        }
    }

    private fun releaseAuthorization(
        next: DeviceAuthorizationUiState,
        endRuntimeAuthorization: Boolean = true,
    ) {
        finishAuthorizationLease()
        authorizationState = next
        if (endRuntimeAuthorization) bluetoothRuntime.authorizationEnded()
    }

    private fun nextAuthorizationGeneration(): Long? {
        if (authorizationGeneration == Long.MAX_VALUE) return null
        authorizationGeneration += 1
        return authorizationGeneration
    }

    private fun validAuthorizationToken(token: String): Boolean =
        token.isNotBlank() && token.length <= MAX_DEVICE_AUTHORIZATION_TOKEN_CHARS

    private fun canContinueAuthorizationStart(
        generation: Long,
        candidate: BleDiscoveredCompanion,
        purpose: DeviceAuthorizationPurpose,
    ): Boolean =
        !closed &&
            mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            activeAuthorizationGeneration == generation &&
            authorizationState == DeviceAuthorizationUiState.Starting(candidate, purpose) &&
            (bluetoothRuntime.state as? BleRuntimeState.AwaitingAuthorization)?.companion?.endpointToken ==
            candidate.endpointToken

    private fun canRetainAuthorizationCallback(
        generation: Long,
        candidate: BleDiscoveredCompanion,
        purpose: DeviceAuthorizationPurpose,
    ): Boolean =
        !closed &&
            mode == TrailConnectionMode.BLUETOOTH_DEVICE &&
            activeAuthorizationGeneration == generation &&
            authorizationState == DeviceAuthorizationUiState.Starting(candidate, purpose)

    private fun matchesActiveAuthorizationToken(token: String): Boolean =
        validAuthorizationToken(token) && token == authorizationClaimToken
}
