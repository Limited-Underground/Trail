package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.COMPANION_MINIMUM_ATT_MTU
import io.github.nbjelanovic.otprotocol.COMPANION_STATUS_SNAPSHOT_BYTES
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimOutcome
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimStart
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfoCodec
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProvisionalEvidence
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationPurpose
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationResponsePhase
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationResponseTracker
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationWireCodec
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationWireError
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionFragment
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodecError
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot

internal const val REQUIRED_ACTION_CAPABILITIES = 0x07
internal const val MAX_DISCOVERED_COMPANIONS = 16
internal const val MAX_PUBLIC_LABEL_CHARS = 64
internal const val MAX_ENDPOINT_TOKEN_CHARS = 256
internal const val DEFAULT_MAX_RECONNECT_ATTEMPTS = 3
internal const val NEGOTIATION_STEP_TIMEOUT_MILLIS = 15_000L
internal const val ACTION_RESULT_TIMEOUT_MILLIS = 10_000L
internal const val AUTHORIZATION_RESULT_TIMEOUT_MILLIS = 30_000L

object CompanionGattV0Contract {
    const val SERVICE_UUID = "5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d0"
    const val PROTOCOL_INFO_UUID = "5e0f2a01-7c6b-4ea3-a210-0c4f1f43b7d0"
    const val COMMAND_UUID = "5e0f2a02-7c6b-4ea3-a210-0c4f1f43b7d0"
    const val STREAM_UUID = "5e0f2a03-7c6b-4ea3-a210-0c4f1f43b7d0"
}

enum class BleRuntimeBlock {
    ADAPTER_IMPLEMENTATION_MISSING,
    PLATFORM_UNSUPPORTED,
    BLUETOOTH_UNAVAILABLE,
    BLUETOOTH_DISABLED,
    SCAN_PERMISSION_MISSING,
    CONNECT_PERMISSION_MISSING,
}

enum class BleRuntimeFailure {
    SCAN_START_FAILED,
    CONNECTION_START_FAILED,
    SECURITY_REQUIREMENT_FAILED,
    MTU_NEGOTIATION_FAILED,
    PROTOCOL_INFO_FAILED,
    STREAM_SUBSCRIPTION_FAILED,
    INITIAL_SNAPSHOT_FAILED,
    NEGOTIATION_TIMEOUT,
    PROTOCOL_VIOLATION,
    RECONNECT_EXHAUSTED,
    SESSION_COUNTER_EXHAUSTED,
    ACTION_WRITE_FAILED,
    ACTION_RESULT_TIMEOUT,
    AUTHORIZATION_CONNECTION_LOST,
}

data class BlePreflight(
    val blocker: BleRuntimeBlock? = null,
) {
    val isReady: Boolean get() = blocker == null
}

data class BleDiscoveredCompanion(
    val endpointToken: String,
    val publicLabel: String,
)

data class BleSecurityEvidence(
    val encrypted: Boolean,
    val authenticatedBond: Boolean,
    val applicationAuthorized: Boolean,
)

enum class BleGattFailure {
    TRANSIENT_LINK,
    PERMISSION_REVOKED,
    SECURITY_REJECTED,
    PLATFORM_FAILURE,
}

sealed interface BleScanEvent {
    data class Candidate(val companion: BleDiscoveredCompanion) : BleScanEvent
    data object Complete : BleScanEvent
    data class Failed(val failure: BleGattFailure) : BleScanEvent
}

sealed interface BleGattEvent {
    data class SecurityEstablished(val evidence: BleSecurityEvidence) : BleGattEvent
    data class MtuChanged(val mtu: Int) : BleGattEvent
    data class ProtocolInfoRead(val value: ByteArray) : BleGattEvent
    data object StreamIndicationsSubscribed : BleGattEvent
    data class StreamIndication(val value: ByteArray) : BleGattEvent
    data class Failed(val failure: BleGattFailure) : BleGattEvent
    data object Disconnected : BleGattEvent
}

interface BleLease : AutoCloseable {
    override fun close()
}

interface BleScanLease : BleLease {
    fun start(): Boolean
}

interface BleGattLease : BleLease {
    fun start(): Boolean
    fun requestMtu(mtu: Int): Boolean
    fun readProtocolInfo(): Boolean
    /** Enables Stream indications; Notify-only subscription is not accepted by this v0 boundary. */
    fun subscribeStreamIndications(): Boolean
    fun writeCommandWithResponse(value: ByteArray): Boolean
}

/**
 * Injectable seam around Android Bluetooth APIs. Implementations must keep endpoint tokens opaque,
 * invoke callbacks serially on the runtime owner thread, and must not emit a successful security
 * event until the platform link is encrypted and the bond is authenticated. The evidence must keep
 * application authorization separate: false may enter only the restricted v0.1 claim path, while
 * true may enter the normal v0.0 path. Scans expose only the exact
 * [CompanionGattV0Contract.SERVICE_UUID], and connections use only that service and its three fixed
 * characteristics.
 */
interface AndroidBluetoothFacade {
    fun preflight(): BlePreflight
    fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease?
    fun createConnection(endpointToken: String, observer: (BleGattEvent) -> Unit): BleGattLease?
}

/** Production default: no Android Bluetooth implementation and therefore no scan/connect authority. */
class DisabledAndroidBluetoothFacade : AndroidBluetoothFacade {
    override fun preflight() = BlePreflight(BleRuntimeBlock.ADAPTER_IMPLEMENTATION_MISSING)
    override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease? = null
    override fun createConnection(endpointToken: String, observer: (BleGattEvent) -> Unit): BleGattLease? = null
}

interface BleReconnectLease : AutoCloseable {
    override fun close()
}

/** Runs callbacks on the runtime owner thread after [schedule] returns; cancellation suppresses unstarted work. */
fun interface BleRuntimeScheduler {
    fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease
}

fun interface BleRuntimeThreadVerifier {
    fun isOwnerThread(): Boolean
}

/** Binds a runtime to the thread on which it is created (the Android owner must create it on main). */
class CreationThreadBleRuntimeVerifier : BleRuntimeThreadVerifier {
    private val ownerThread = Thread.currentThread()
    override fun isOwnerThread(): Boolean = Thread.currentThread() === ownerThread
}

enum class BleNegotiationPhase {
    LINK_SECURITY,
    ATT_MTU,
    PROTOCOL_INFO,
    STREAM_SUBSCRIPTION,
    AUTHORIZATION_CLAIM,
    INITIAL_SNAPSHOT,
}

data class BleActiveSession(
    val companion: BleDiscoveredCompanion,
    val sessionNonce: Long,
    val snapshot: CompanionStatusSnapshot,
    val protocolInfo: CompanionProtocolInfo,
    val lastActionResult: CompanionActionResult? = null,
)

sealed interface BleRuntimeState {
    data object Inactive : BleRuntimeState
    data object Idle : BleRuntimeState
    data class Blocked(val reason: BleRuntimeBlock) : BleRuntimeState
    data class Scanning(val candidates: List<BleDiscoveredCompanion>) : BleRuntimeState
    data class AwaitingAuthorization(val companion: BleDiscoveredCompanion) : BleRuntimeState
    data class Connecting(val companion: BleDiscoveredCompanion) : BleRuntimeState
    data class Negotiating(
        val companion: BleDiscoveredCompanion,
        val phase: BleNegotiationPhase,
    ) : BleRuntimeState
    data class Ready(val session: BleActiveSession) : BleRuntimeState
    data class Reconnecting(
        val companion: BleDiscoveredCompanion,
        val attempt: Int,
        val maximumAttempts: Int,
    ) : BleRuntimeState
    data class Failed(val reason: BleRuntimeFailure) : BleRuntimeState
    data object Closed : BleRuntimeState
}

/**
 * Lifecycle-controlled owner for one BLE scan or one GATT connection. This class owns every lease,
 * rejects stale callbacks by generation, performs the accepted GATT v0 negotiation in order, and
 * bounds reconnect and request counters without wrap. It does not implement Android Bluetooth.
 */
class BleCompanionRuntime(
    private val facade: AndroidBluetoothFacade,
    private val scheduler: BleRuntimeScheduler,
    private val maximumReconnectAttempts: Int = DEFAULT_MAX_RECONNECT_ATTEMPTS,
    private val firstRequestId: Long = 1,
    private val threadVerifier: BleRuntimeThreadVerifier = CreationThreadBleRuntimeVerifier(),
) : AutoCloseable {
    init {
        require(maximumReconnectAttempts in 0..16)
        require(firstRequestId in 1..0xffff_ffffL)
    }

    @Volatile
    var state: BleRuntimeState = BleRuntimeState.Inactive
        private set

    private var observer: ((BleRuntimeState) -> Unit)? = null
    private var deliveringObserver = false
    private val deferredControlActions = ArrayDeque<DeferredRuntimeControl>()
    private var lifecycleActive = false
    private var closed = false
    private var generation = 0L
    private var scanLease: BleScanLease? = null
    private var gattLease: BleGattLease? = null
    private var reconnectLease: BleReconnectLease? = null
    private var operationTimeoutLease: BleReconnectLease? = null
    private var selected: BleDiscoveredCompanion? = null
    private var reconnectAttempt = 0
    private var negotiatedMtu = 0
    private var protocolInfo: CompanionProtocolInfo? = null
    private var activeSessionNonce = 0L
    private var lastDeviceEventId = 0L
    private var nextRequestId = firstRequestId
    private var pendingAction: PendingAction? = null
    private var authorizationProtocolInfo: CompanionAuthorizationProtocolInfo? = null
    private var authorizationSecurityEvidence: BleSecurityEvidence? = null
    private var authorizationTransportGeneration = 0L
    private var authorizationOperationCounter = 0L
    private var deliveringAuthorizationObserver = false
    private var pendingSnapshotExchangeId = 0L
    private val authorizationTracker = CompanionAuthorizationResponseTracker()
    private var authorizationClaim: RuntimeAuthorizationClaim? = null
    private data class PendingAction(val exchangeId: Long, val request: CompanionActionRequest)
    private data class RuntimeAuthorizationClaim(
        val operationId: Long,
        val companion: BleDiscoveredCompanion,
        val purpose: DeviceAuthorizationPurpose,
        val publicOperationToken: String,
        val observer: (DeviceAuthorizationClaimEvent) -> Unit,
        var started: Boolean = false,
        var terminal: Boolean = false,
        var promoted: Boolean = false,
    )
    private enum class DeferredRuntimeControl { START, STOP, DISCONNECT, CLOSE }

    /**
     * State callbacks are notification-only. Reentrant lifecycle/disconnect/close controls are
     * bounded and deferred (close wins); request, selection, and protocol mutations are rejected.
     */
    fun observe(observer: ((BleRuntimeState) -> Unit)?) {
        requireOwnerThread()
        if (deliveringObserver) return
        this.observer = observer
        deliverState(observer, state)
    }

    @Synchronized
    fun onLifecycleStart() {
        requireOwnerThread()
        if (deliveringObserver) {
            deferControl(DeferredRuntimeControl.START)
            return
        }
        startNow()
    }

    private fun startNow() {
        if (closed || lifecycleActive) return
        lifecycleActive = true
        val remembered = selected
        if (remembered == null) {
            publish(BleRuntimeState.Idle)
        } else {
            beginConnection(remembered, isReconnect = true)
        }
    }

    @Synchronized
    fun onLifecycleStop() {
        requireOwnerThread()
        if (deliveringObserver) {
            deferControl(DeferredRuntimeControl.STOP)
            return
        }
        stopNow()
    }

    private fun stopNow() {
        if (closed || !lifecycleActive) return
        lifecycleActive = false
        invalidateAndRelease()
        authorizationClaim = null
        publish(BleRuntimeState.Inactive)
    }

    @Synchronized
    fun requestScan() {
        requireOwnerThread()
        if (deliveringObserver) return
        if (!lifecycleActive || closed) return
        selected = null
        reconnectAttempt = 0
        invalidateAndRelease()
        authorizationClaim = null
        val preflight = facade.preflight()
        if (!preflight.isReady) {
            publish(BleRuntimeState.Blocked(preflight.blocker!!))
            return
        }
        val callbackGeneration = nextGeneration() ?: return
        val candidates = linkedMapOf<String, BleDiscoveredCompanion>()
        val lease = facade.createScan { event -> onScanEvent(callbackGeneration, candidates, event) }
        if (lease == null) {
            publish(BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED))
            return
        }
        scanLease = lease
        publish(BleRuntimeState.Scanning(emptyList()))
        if (!accepts(callbackGeneration) || scanLease !== lease || state !is BleRuntimeState.Scanning) return
        if (!lease.start()) {
            lease.close()
            if (scanLease === lease) scanLease = null
            if (generation == callbackGeneration) {
                publish(BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED))
            }
        }
    }

    @Synchronized
    fun beginAuthorization(endpointToken: String): BleDiscoveredCompanion? {
        requireOwnerThread()
        if (deliveringObserver || !lifecycleActive || closed) return null
        val scanning = state as? BleRuntimeState.Scanning ?: return null
        val candidate = scanning.candidates.singleOrNull { it.endpointToken == endpointToken } ?: return null
        selected = null
        reconnectAttempt = 0
        scanLease?.close()
        scanLease = null
        publish(BleRuntimeState.AwaitingAuthorization(candidate))
        return candidate.copy()
    }

    fun authorizationAccepted(endpointToken: String): Boolean {
        requireOwnerThread()
        if (deliveringObserver || !lifecycleActive || closed) return false
        val activeClaim = authorizationClaim
        if (
            activeClaim != null &&
            activeClaim.companion.endpointToken == endpointToken &&
            activeClaim.terminal &&
            activeClaim.promoted &&
            authorizationTracker.allowsNormalCompanionTraffic(authorizationTransportGeneration.toULong())
        ) return true
        val awaiting = state as? BleRuntimeState.AwaitingAuthorization ?: return false
        if (awaiting.companion.endpointToken != endpointToken) return false
        selected = awaiting.companion
        reconnectAttempt = 0
        beginConnection(awaiting.companion, isReconnect = false)
        return true
    }

    fun authorizationEnded() {
        requireOwnerThread()
        if (deliveringObserver || closed) return
        if (authorizationClaim != null) {
            selected = null
            invalidateAndRelease()
            authorizationClaim = null
            publish(if (lifecycleActive) BleRuntimeState.Idle else BleRuntimeState.Inactive)
            return
        }
        if (state !is BleRuntimeState.AwaitingAuthorization) return
        selected = null
        reconnectAttempt = 0
        invalidateAndRelease()
        publish(if (lifecycleActive) BleRuntimeState.Idle else BleRuntimeState.Inactive)
    }

    /**
     * Dormant OT-051 seam. MainActivity does not construct this client; tests or a later admitted
     * production composition may bind it only to this exact runtime owner thread.
     */
    fun createAuthorizationClaim(
        endpointToken: String,
        purpose: DeviceAuthorizationPurpose,
        observer: (DeviceAuthorizationClaimEvent) -> Unit,
    ): DeviceAuthorizationClaimLease? {
        requireOwnerThread()
        if (deliveringObserver || closed || !lifecycleActive || authorizationClaim != null) return null
        val awaiting = state as? BleRuntimeState.AwaitingAuthorization ?: return null
        if (awaiting.companion.endpointToken != endpointToken) return null
        if (authorizationOperationCounter == Long.MAX_VALUE) return null
        authorizationOperationCounter += 1
        val operationId = authorizationOperationCounter
        val claim = RuntimeAuthorizationClaim(
            operationId,
            awaiting.companion,
            purpose,
            "wire-claim-$operationId",
            observer,
        )
        authorizationClaim = claim
        return object : DeviceAuthorizationClaimLease {
            private var closed = false

            override fun start(): Boolean {
                requireOwnerThread()
                if (closed) return false
                return startAuthorizationClaim(operationId)
            }

            override fun close() {
                requireOwnerThread()
                if (closed) return
                closed = true
                closeAuthorizationClaim(operationId)
            }
        }
    }

    private fun startAuthorizationClaim(operationId: Long): Boolean {
        val claim = authorizationClaim
        if (
            claim == null || claim.operationId != operationId || claim.started || claim.terminal ||
            state !is BleRuntimeState.AwaitingAuthorization
        ) return false
        claim.started = true
        selected = claim.companion
        reconnectAttempt = 0
        beginConnection(claim.companion, isReconnect = false)
        return authorizationClaim?.operationId == operationId &&
            (state is BleRuntimeState.Connecting || state is BleRuntimeState.Negotiating)
    }

    private fun closeAuthorizationClaim(operationId: Long) {
        val claim = authorizationClaim ?: return
        if (claim.operationId != operationId || claim.terminal) return
        selected = null
        invalidateAndRelease()
        authorizationClaim = null
        publish(if (lifecycleActive) BleRuntimeState.Idle else BleRuntimeState.Inactive)
    }

    @Synchronized
    fun disconnect() {
        requireOwnerThread()
        if (deliveringObserver) {
            deferControl(DeferredRuntimeControl.DISCONNECT)
            return
        }
        disconnectNow()
    }

    private fun disconnectNow() {
        if (closed) return
        selected = null
        reconnectAttempt = 0
        invalidateAndRelease()
        authorizationClaim = null
        publish(if (lifecycleActive) BleRuntimeState.Idle else BleRuntimeState.Inactive)
    }

    @Synchronized
    fun submitAction(request: CompanionActionRequest): Boolean {
        requireOwnerThread()
        if (deliveringObserver) return false
        val ready = state as? BleRuntimeState.Ready ?: return false
        val lease = gattLease ?: return false
        if (pendingAction != null) return false
        val requestId = nextRequestId
        if (requestId !in 1..0xffff_ffffL) {
            handleConnectionFailure(
                ready.session.companion,
                BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED,
                transient = true,
            )
            return false
        }
        val payload = CompanionSemanticCodec.encodeActionRequest(request).value ?: return false
        val encoded = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.ACTION_REQUEST,
                sessionNonce = ready.session.sessionNonce,
                exchangeId = requestId,
                payload = payload,
            ),
        ).value ?: return false

        // Consume an ID before handing bytes to the platform because a false return can still be uncertain.
        nextRequestId = requestId + 1
        pendingAction = PendingAction(requestId, request)
        if (!lease.writeCommandWithResponse(encoded)) {
            pendingAction = null
            failAndRelease(BleRuntimeFailure.ACTION_WRITE_FAILED)
            return false
        }
        if (pendingAction?.exchangeId == requestId && state is BleRuntimeState.Ready) {
            armActionTimeout(generation, requestId)
        }
        return true
    }

    @Synchronized
    override fun close() {
        requireOwnerThread()
        if (deliveringObserver) {
            deferControl(DeferredRuntimeControl.CLOSE)
            return
        }
        closeNow()
    }

    private fun closeNow() {
        if (closed) return
        closed = true
        lifecycleActive = false
        selected = null
        invalidateAndRelease()
        authorizationClaim = null
        observer = null
        state = BleRuntimeState.Closed
    }

    @Synchronized
    private fun onScanEvent(
        callbackGeneration: Long,
        candidates: LinkedHashMap<String, BleDiscoveredCompanion>,
        event: BleScanEvent,
    ) {
        requireOwnerThread()
        if (deliveringObserver) return
        if (!accepts(callbackGeneration) || state !is BleRuntimeState.Scanning) return
        when (event) {
            is BleScanEvent.Candidate -> {
                val candidate = event.companion
                if (
                    candidate.endpointToken.isBlank() ||
                    candidate.endpointToken.length > MAX_ENDPOINT_TOKEN_CHARS ||
                    candidate.publicLabel.isBlank() ||
                    candidate.publicLabel.length > MAX_PUBLIC_LABEL_CHARS ||
                    (!candidates.containsKey(candidate.endpointToken) && candidates.size >= MAX_DISCOVERED_COMPANIONS)
                ) return
                candidates[candidate.endpointToken] = candidate.copy()
                publish(BleRuntimeState.Scanning(candidates.values.toList()))
            }
            BleScanEvent.Complete -> {
                scanLease?.close()
                scanLease = null
                publish(BleRuntimeState.Scanning(candidates.values.toList()))
            }
            is BleScanEvent.Failed -> {
                scanLease?.close()
                scanLease = null
                publish(BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED))
            }
        }
    }

    private fun beginConnection(companion: BleDiscoveredCompanion, isReconnect: Boolean) {
        if (!lifecycleActive || closed) return
        invalidateAndRelease()
        val preflight = facade.preflight()
        if (!preflight.isReady) {
            publish(BleRuntimeState.Blocked(preflight.blocker!!))
            return
        }
        val callbackGeneration = nextGeneration() ?: return
        clearSessionState()
        val lease = facade.createConnection(companion.endpointToken) { event ->
            onGattEvent(callbackGeneration, companion, event)
        }
        if (lease == null) {
            handleConnectionFailure(companion, BleRuntimeFailure.CONNECTION_START_FAILED, transient = isReconnect)
            return
        }
        gattLease = lease
        publish(
            if (isReconnect) {
                BleRuntimeState.Reconnecting(companion, reconnectAttempt.coerceAtLeast(1), maximumReconnectAttempts)
            } else {
                BleRuntimeState.Connecting(companion)
            },
        )
        if (!accepts(callbackGeneration) || gattLease !== lease) return
        if (!lease.start()) {
            if (gattLease === lease) {
                lease.close()
                gattLease = null
            }
            if (generation == callbackGeneration) {
                handleConnectionFailure(companion, BleRuntimeFailure.CONNECTION_START_FAILED, transient = true)
            }
        } else if (
            generation == callbackGeneration &&
            gattLease === lease &&
            (state is BleRuntimeState.Connecting || state is BleRuntimeState.Reconnecting)
        ) {
            armNegotiationTimeout(callbackGeneration, null)
        }
    }

    @Synchronized
    private fun onGattEvent(
        callbackGeneration: Long,
        companion: BleDiscoveredCompanion,
        event: BleGattEvent,
    ) {
        requireOwnerThread()
        if (!accepts(callbackGeneration) || gattLease == null) return
        if (deliveringAuthorizationObserver) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        if (deliveringObserver) return
        when (event) {
            is BleGattEvent.SecurityEstablished -> onSecurity(companion, event.evidence)
            is BleGattEvent.MtuChanged -> onMtu(companion, event.mtu)
            is BleGattEvent.ProtocolInfoRead -> onProtocolInfo(companion, event.value)
            BleGattEvent.StreamIndicationsSubscribed -> onStreamSubscribed(companion)
            is BleGattEvent.StreamIndication -> onStreamValue(companion, event.value)
            is BleGattEvent.Failed -> handleConnectionFailure(
                companion,
                if (authorizationClaim != null) {
                    BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST
                } else when (event.failure) {
                    BleGattFailure.SECURITY_REJECTED -> BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED
                    BleGattFailure.PERMISSION_REVOKED -> BleRuntimeFailure.CONNECTION_START_FAILED
                    BleGattFailure.PLATFORM_FAILURE -> BleRuntimeFailure.CONNECTION_START_FAILED
                    BleGattFailure.TRANSIENT_LINK -> BleRuntimeFailure.CONNECTION_START_FAILED
                },
                transient = authorizationClaim == null && event.failure == BleGattFailure.TRANSIENT_LINK,
            )
            BleGattEvent.Disconnected -> handleConnectionFailure(
                companion,
                if (authorizationClaim != null) {
                    BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST
                } else {
                    BleRuntimeFailure.CONNECTION_START_FAILED
                },
                transient = authorizationClaim == null,
            )
        }
    }

    private fun onSecurity(companion: BleDiscoveredCompanion, evidence: BleSecurityEvidence) {
        if (state !is BleRuntimeState.Connecting && state !is BleRuntimeState.Reconnecting) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        if (!evidence.encrypted || !evidence.authenticatedBond) {
            failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED)
            return
        }
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        if (authorizationClaim != null) {
            authorizationSecurityEvidence = evidence
            publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.PROTOCOL_INFO))
            if (!hasActiveGattPhase(BleNegotiationPhase.PROTOCOL_INFO)) return
            if (gattLease?.readProtocolInfo() != true) {
                failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            } else if (isPhase(BleNegotiationPhase.PROTOCOL_INFO)) {
                armNegotiationTimeout(generation, BleNegotiationPhase.PROTOCOL_INFO)
            }
            return
        }
        if (!evidence.applicationAuthorized) {
            failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED)
            return
        }
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.ATT_MTU))
        if (!hasActiveGattPhase(BleNegotiationPhase.ATT_MTU)) return
        if (gattLease?.requestMtu(COMPANION_MINIMUM_ATT_MTU) != true) {
            failAndRelease(BleRuntimeFailure.MTU_NEGOTIATION_FAILED)
        } else if (isPhase(BleNegotiationPhase.ATT_MTU)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.ATT_MTU)
        }
    }

    private fun onMtu(companion: BleDiscoveredCompanion, mtu: Int) {
        val minimumMtu = authorizationProtocolInfo?.minimumNormalAttMtu ?: COMPANION_MINIMUM_ATT_MTU
        if (!isPhase(BleNegotiationPhase.ATT_MTU) || mtu < minimumMtu) {
            failAndRelease(BleRuntimeFailure.MTU_NEGOTIATION_FAILED)
            return
        }
        negotiatedMtu = mtu
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        if (authorizationClaim != null) {
            publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.STREAM_SUBSCRIPTION))
            if (!hasActiveGattPhase(BleNegotiationPhase.STREAM_SUBSCRIPTION)) return
            if (gattLease?.subscribeStreamIndications() != true) {
                failAndRelease(BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED)
            } else if (isPhase(BleNegotiationPhase.STREAM_SUBSCRIPTION)) {
                armNegotiationTimeout(generation, BleNegotiationPhase.STREAM_SUBSCRIPTION)
            }
            return
        }
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.PROTOCOL_INFO))
        if (!hasActiveGattPhase(BleNegotiationPhase.PROTOCOL_INFO)) return
        if (gattLease?.readProtocolInfo() != true) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
        } else if (isPhase(BleNegotiationPhase.PROTOCOL_INFO)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.PROTOCOL_INFO)
        }
    }

    private fun onProtocolInfo(companion: BleDiscoveredCompanion, value: ByteArray) {
        if (!isPhase(BleNegotiationPhase.PROTOCOL_INFO)) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        if (authorizationClaim != null) {
            acceptAuthorizationProtocolInfo(companion, value)
            return
        }
        val decoded = CompanionProtocolCodec.decodeProtocolInfo(value).value
        if (
            decoded == null ||
            (decoded.capabilities and REQUIRED_ACTION_CAPABILITIES) != REQUIRED_ACTION_CAPABILITIES ||
            decoded.maxFragmentPayloadBytes < COMPANION_STATUS_SNAPSHOT_BYTES ||
            negotiatedMtu < decoded.minimumAttMtu
        ) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            return
        }
        protocolInfo = decoded
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.STREAM_SUBSCRIPTION))
        if (!hasActiveGattPhase(BleNegotiationPhase.STREAM_SUBSCRIPTION)) return
        if (gattLease?.subscribeStreamIndications() != true) {
            failAndRelease(BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED)
        } else if (isPhase(BleNegotiationPhase.STREAM_SUBSCRIPTION)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.STREAM_SUBSCRIPTION)
        }
    }

    private fun acceptAuthorizationProtocolInfo(companion: BleDiscoveredCompanion, value: ByteArray) {
        val decoded = CompanionAuthorizationProtocolInfoCodec.decode(value).value
        val evidence = authorizationSecurityEvidence
        if (decoded == null || evidence == null || decoded.minimumNormalAttMtu < COMPANION_MINIMUM_ATT_MTU) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            return
        }
        val trackerError = authorizationTracker.openProvisionalSession(
            CompanionAuthorizationProvisionalEvidence(
                transportGeneration = generation.toULong(),
                linkEncrypted = evidence.encrypted,
                authenticatedBond = evidence.authenticatedBond,
                claimWireSupported = true,
            ),
            decoded.provisionalSessionNonce,
        )
        if (trackerError != CompanionAuthorizationWireError.NONE) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            return
        }
        authorizationTransportGeneration = generation
        authorizationProtocolInfo = decoded
        protocolInfo = CompanionProtocolInfo(
            capabilities = decoded.capabilities and 0x0f,
            maxFragmentPayloadBytes = decoded.maxFragmentPayloadBytes,
            minimumAttMtu = decoded.minimumNormalAttMtu,
            maxFragmentCount = decoded.maxFragmentCount,
            maxActiveControllers = decoded.maxActiveControllers,
        )
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.ATT_MTU))
        if (!hasActiveGattPhase(BleNegotiationPhase.ATT_MTU)) return
        if (gattLease?.requestMtu(decoded.minimumNormalAttMtu) != true) {
            failAndRelease(BleRuntimeFailure.MTU_NEGOTIATION_FAILED)
        } else if (isPhase(BleNegotiationPhase.ATT_MTU)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.ATT_MTU)
        }
    }

    private fun onStreamSubscribed(companion: BleDiscoveredCompanion) {
        if (!isPhase(BleNegotiationPhase.STREAM_SUBSCRIPTION)) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        if (authorizationClaim != null) {
            beginAuthorizationWireClaim(companion)
            return
        }
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.INITIAL_SNAPSHOT))
        if (hasActiveGattPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.INITIAL_SNAPSHOT)
        }
    }

    private fun beginAuthorizationWireClaim(companion: BleDiscoveredCompanion) {
        val claim = authorizationClaim ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        val info = authorizationProtocolInfo ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        val exchangeId = firstRequestId
        val purpose = when (claim.purpose) {
            DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE -> CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
            DeviceAuthorizationPurpose.REPLACE_LOST_PHONE -> CompanionAuthorizationPurpose.REPLACE_CONTROLLER
        }
        val fragment = CompanionFragment(
            kind = CompanionFrameKind.AUTHORIZATION_CLAIM_START,
            sessionNonce = info.provisionalSessionNonce,
            exchangeId = exchangeId,
            payload = CompanionAuthorizationWireCodec.encodeClaimStart(CompanionAuthorizationClaimStart(purpose)),
        )
        if (authorizationTracker.begin(generation.toULong(), fragment) != CompanionAuthorizationWireError.NONE) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        val encoded = CompanionProtocolCodec.encodeFragment(fragment).value
            ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.AUTHORIZATION_CLAIM))
        if (!hasActiveGattPhase(BleNegotiationPhase.AUTHORIZATION_CLAIM)) return
        if (gattLease?.writeCommandWithResponse(encoded) != true) {
            failAndRelease(BleRuntimeFailure.ACTION_WRITE_FAILED)
        } else if (isPhase(BleNegotiationPhase.AUTHORIZATION_CLAIM)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.AUTHORIZATION_CLAIM)
        }
    }

    private fun onStreamValue(companion: BleDiscoveredCompanion, value: ByteArray) {
        val fragment = CompanionProtocolCodec.decodeFragment(value).value
        if (authorizationClaim != null && isPhase(BleNegotiationPhase.AUTHORIZATION_CLAIM)) {
            acceptAuthorizationStreamValue(companion, fragment)
            return
        }
        if (
            fragment == null ||
            fragment.payload.size > (protocolInfo?.maxFragmentPayloadBytes ?: 0) ||
            CompanionSemanticCodec.validateSemanticFragment(fragment) != CompanionSemanticCodecError.NONE
        ) {
            failAndRelease(
                if (isPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) {
                    BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED
                } else BleRuntimeFailure.PROTOCOL_VIOLATION,
            )
            return
        }
        if (isPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) {
            acceptInitialSnapshot(companion, fragment)
            return
        }
        val ready = state as? BleRuntimeState.Ready
        if (ready == null || fragment.sessionNonce != activeSessionNonce) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        when (fragment.kind) {
            CompanionFrameKind.SNAPSHOT -> acceptSnapshotUpdate(ready, fragment)
            CompanionFrameKind.ACTION_RESULT -> acceptActionResult(ready, fragment)
            else -> failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        }
    }

    private fun acceptAuthorizationStreamValue(companion: BleDiscoveredCompanion, fragment: CompanionFragment?) {
        val claim = authorizationClaim
        val info = authorizationProtocolInfo
        if (claim == null || info == null || fragment == null || fragment.payload.size > info.maxFragmentPayloadBytes) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        val observation = authorizationTracker.observe(generation.toULong(), fragment)
        if (!observation.accepted) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        when (observation.phase) {
            CompanionAuthorizationResponsePhase.PENDING -> emitAuthorizationEvent(
                claim,
                DeviceAuthorizationClaimEvent.Pending(claim.publicOperationToken),
            ).also {
                if (
                    authorizationClaim?.operationId == claim.operationId &&
                    isPhase(BleNegotiationPhase.AUTHORIZATION_CLAIM)
                ) {
                    armNegotiationTimeout(
                        generation,
                        BleNegotiationPhase.AUTHORIZATION_CLAIM,
                        AUTHORIZATION_RESULT_TIMEOUT_MILLIS,
                    )
                }
            }
            CompanionAuthorizationResponsePhase.TERMINAL -> {
                operationTimeoutLease?.close()
                operationTimeoutLease = null
                claim.terminal = true
                when (observation.outcome) {
                    CompanionAuthorizationClaimOutcome.ACCEPTED -> {
                        claim.promoted = true
                        emitAuthorizationEvent(
                            claim,
                            DeviceAuthorizationClaimEvent.Accepted(claim.publicOperationToken),
                        )
                        continueAfterAuthorizationPromotion(companion, claim)
                    }
                    CompanionAuthorizationClaimOutcome.REPLACED -> {
                        claim.promoted = true
                        emitAuthorizationEvent(
                            claim,
                            DeviceAuthorizationClaimEvent.Replaced(claim.publicOperationToken),
                        )
                        continueAfterAuthorizationPromotion(companion, claim)
                    }
                    CompanionAuthorizationClaimOutcome.DENIED -> {
                        emitAuthorizationEvent(claim, DeviceAuthorizationClaimEvent.Denied(claim.publicOperationToken))
                        if (authorizationClaim?.operationId == claim.operationId) authorizationEnded()
                    }
                }
            }
            else -> failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        }
    }

    private fun emitAuthorizationEvent(claim: RuntimeAuthorizationClaim, event: DeviceAuthorizationClaimEvent) {
        if (authorizationClaim?.operationId != claim.operationId) return
        if (deliveringAuthorizationObserver) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        deliveringAuthorizationObserver = true
        try {
            claim.observer(event)
        } catch (_: Exception) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        } finally {
            deliveringAuthorizationObserver = false
        }
    }

    private fun continueAfterAuthorizationPromotion(
        companion: BleDiscoveredCompanion,
        claim: RuntimeAuthorizationClaim,
    ) {
        val info = authorizationProtocolInfo
        if (
            authorizationClaim?.operationId != claim.operationId || !claim.promoted || info == null ||
            !authorizationTracker.allowsNormalCompanionTraffic(generation.toULong()) || gattLease == null
        ) return
        activeSessionNonce = info.provisionalSessionNonce
        val exchangeId = firstRequestId
        if (exchangeId == 0xffff_ffffL) {
            failAndRelease(BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED)
            return
        }
        val payload = CompanionSemanticCodec.encodeSnapshotRequest()
        val encoded = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.SNAPSHOT_REQUEST,
                sessionNonce = activeSessionNonce,
                exchangeId = exchangeId,
                payload = payload,
            ),
        ).value ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        pendingSnapshotExchangeId = exchangeId
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.INITIAL_SNAPSHOT))
        if (!hasActiveGattPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) return
        if (gattLease?.writeCommandWithResponse(encoded) != true) {
            failAndRelease(BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED)
        } else if (isPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.INITIAL_SNAPSHOT)
        }
    }

    private fun acceptInitialSnapshot(companion: BleDiscoveredCompanion, fragment: CompanionFragment) {
        val info = protocolInfo
        val snapshot = if (fragment.kind == CompanionFrameKind.SNAPSHOT) {
            CompanionSemanticCodec.decodeStatusSnapshot(fragment.payload).value
        } else null
        if (
            info == null || snapshot == null ||
            (
                pendingSnapshotExchangeId != 0L &&
                    (fragment.exchangeId != pendingSnapshotExchangeId || fragment.sessionNonce != activeSessionNonce)
                )
        ) {
            failAndRelease(BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED)
            return
        }
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        if (pendingSnapshotExchangeId == 0L) activeSessionNonce = fragment.sessionNonce
        lastDeviceEventId = fragment.exchangeId
        nextRequestId = if (pendingSnapshotExchangeId == 0L) {
            firstRequestId
        } else {
            pendingSnapshotExchangeId + 1
        }
        pendingAction = null
        pendingSnapshotExchangeId = 0
        authorizationClaim = null
        reconnectAttempt = 0
        publish(BleRuntimeState.Ready(BleActiveSession(companion, fragment.sessionNonce, snapshot, info)))
    }

    private fun acceptSnapshotUpdate(ready: BleRuntimeState.Ready, fragment: CompanionFragment) {
        if (fragment.exchangeId <= lastDeviceEventId) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        val snapshot = CompanionSemanticCodec.decodeStatusSnapshot(fragment.payload).value
        if (snapshot == null || snapshot.revision < ready.session.snapshot.revision) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        lastDeviceEventId = fragment.exchangeId
        publish(BleRuntimeState.Ready(ready.session.copy(snapshot = snapshot)))
    }

    private fun acceptActionResult(ready: BleRuntimeState.Ready, fragment: CompanionFragment) {
        val pending = pendingAction
        val result = CompanionSemanticCodec.decodeActionResult(fragment.payload).value
        if (
            pending == null ||
            fragment.exchangeId != pending.exchangeId ||
            result == null ||
            result.kind != pending.request.kind ||
            result.quickStatus != pending.request.quickStatus ||
            result.criticalAlertId != pending.request.criticalAlertId
        ) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        pendingAction = null
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        publish(BleRuntimeState.Ready(ready.session.copy(lastActionResult = result)))
    }

    private fun handleConnectionFailure(
        companion: BleDiscoveredCompanion,
        reason: BleRuntimeFailure,
        transient: Boolean,
    ) {
        val authorizationWasActive = authorizationClaim != null
        if (authorizationWasActive) {
            selected = null
            invalidateAndRelease()
            authorizationClaim = null
            publish(BleRuntimeState.Failed(reason))
            return
        }
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        gattLease?.close()
        gattLease = null
        clearSessionState()
        if (!transient || !lifecycleActive || selected?.endpointToken != companion.endpointToken) {
            selected = null
            publish(BleRuntimeState.Failed(reason))
            return
        }
        if (reconnectAttempt >= maximumReconnectAttempts) {
            selected = null
            publish(BleRuntimeState.Failed(BleRuntimeFailure.RECONNECT_EXHAUSTED))
            return
        }
        reconnectAttempt += 1
        val attempt = reconnectAttempt
        val scheduleGeneration = nextGeneration() ?: return
        publish(BleRuntimeState.Reconnecting(companion, attempt, maximumReconnectAttempts))
        if (
            !accepts(scheduleGeneration) ||
            selected?.endpointToken != companion.endpointToken ||
            state !is BleRuntimeState.Reconnecting
        ) return
        reconnectLease = scheduler.schedule(reconnectDelayMillis(attempt)) {
            onReconnectTimer(scheduleGeneration, companion, attempt)
        }
    }

    @Synchronized
    private fun onReconnectTimer(
        scheduleGeneration: Long,
        companion: BleDiscoveredCompanion,
        attempt: Int,
    ) {
        requireOwnerThread()
        if (deliveringObserver) return
        if (
            !accepts(scheduleGeneration) ||
            !lifecycleActive ||
            reconnectAttempt != attempt ||
            selected?.endpointToken != companion.endpointToken
        ) return
        reconnectLease = null
        beginConnection(companion, isReconnect = true)
    }

    private fun reconnectDelayMillis(attempt: Int): Long = 1_000L shl (attempt - 1).coerceAtMost(3)

    private fun armNegotiationTimeout(
        callbackGeneration: Long,
        expectedPhase: BleNegotiationPhase?,
        delayMillis: Long = NEGOTIATION_STEP_TIMEOUT_MILLIS,
    ) {
        operationTimeoutLease?.close()
        operationTimeoutLease = scheduler.schedule(delayMillis) {
            onNegotiationTimeout(callbackGeneration, expectedPhase)
        }
    }

    @Synchronized
    private fun onNegotiationTimeout(callbackGeneration: Long, expectedPhase: BleNegotiationPhase?) {
        requireOwnerThread()
        if (deliveringObserver) return
        if (!accepts(callbackGeneration)) return
        val stillWaiting = if (expectedPhase == null) {
            state is BleRuntimeState.Connecting || state is BleRuntimeState.Reconnecting
        } else {
            isPhase(expectedPhase)
        }
        if (stillWaiting) failAndRelease(BleRuntimeFailure.NEGOTIATION_TIMEOUT)
    }

    private fun armActionTimeout(callbackGeneration: Long, exchangeId: Long) {
        operationTimeoutLease?.close()
        operationTimeoutLease = scheduler.schedule(ACTION_RESULT_TIMEOUT_MILLIS) {
            onActionTimeout(callbackGeneration, exchangeId)
        }
    }

    @Synchronized
    private fun onActionTimeout(callbackGeneration: Long, exchangeId: Long) {
        requireOwnerThread()
        if (deliveringObserver) return
        if (accepts(callbackGeneration) && pendingAction?.exchangeId == exchangeId) {
            val companion = (state as? BleRuntimeState.Ready)?.session?.companion
            if (companion == null) {
                failAndRelease(BleRuntimeFailure.ACTION_RESULT_TIMEOUT)
            } else {
                handleConnectionFailure(companion, BleRuntimeFailure.ACTION_RESULT_TIMEOUT, transient = true)
            }
        }
    }

    private fun isPhase(phase: BleNegotiationPhase): Boolean =
        (state as? BleRuntimeState.Negotiating)?.phase == phase

    private fun hasActiveGattPhase(phase: BleNegotiationPhase): Boolean =
        !closed && lifecycleActive && gattLease != null && isPhase(phase)

    private fun failAndRelease(reason: BleRuntimeFailure) {
        selected = null
        invalidateAndRelease()
        authorizationClaim = null
        publish(BleRuntimeState.Failed(reason))
    }

    private fun nextGeneration(): Long? {
        if (generation == Long.MAX_VALUE) {
            selected = null
            invalidateAndRelease(incrementGeneration = false)
            publish(BleRuntimeState.Failed(BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED))
            return null
        }
        generation += 1
        return generation
    }

    private fun accepts(callbackGeneration: Long): Boolean =
        !closed && lifecycleActive && callbackGeneration == generation

    private fun invalidateAndRelease(incrementGeneration: Boolean = true) {
        closeAuthorizationTransport()
        if (incrementGeneration && generation < Long.MAX_VALUE) generation += 1
        reconnectLease?.close()
        reconnectLease = null
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        scanLease?.close()
        scanLease = null
        gattLease?.close()
        gattLease = null
        clearSessionState()
    }

    private fun clearSessionState() {
        negotiatedMtu = 0
        protocolInfo = null
        activeSessionNonce = 0
        lastDeviceEventId = 0
        nextRequestId = firstRequestId
        pendingAction = null
        pendingSnapshotExchangeId = 0
        authorizationProtocolInfo = null
        authorizationSecurityEvidence = null
    }

    private fun closeAuthorizationTransport() {
        val ownedGeneration = authorizationTransportGeneration
        authorizationTransportGeneration = 0
        if (ownedGeneration != 0L) {
            authorizationTracker.closeTransportGeneration(ownedGeneration.toULong())
        }
    }

    private fun publish(next: BleRuntimeState) {
        state = next
        deliverState(observer, next)
    }

    private fun deliverState(callback: ((BleRuntimeState) -> Unit)?, next: BleRuntimeState) {
        if (callback == null || deliveringObserver) return
        deliveringObserver = true
        try {
            callback(next)
        } catch (_: Exception) {
            // Presentation failure cannot interrupt or acquire transport authority.
            if (observer === callback) observer = null
        } finally {
            deliveringObserver = false
            drainDeferredControls()
        }
    }

    private fun deferControl(control: DeferredRuntimeControl) {
        if (closed) return
        if (control == DeferredRuntimeControl.CLOSE || deferredControlActions.size >= 8) {
            deferredControlActions.clear()
            deferredControlActions.addLast(DeferredRuntimeControl.CLOSE)
        } else {
            deferredControlActions.addLast(control)
        }
    }

    private fun drainDeferredControls() {
        if (deliveringObserver || closed || deferredControlActions.isEmpty()) return
        if (deferredControlActions.contains(DeferredRuntimeControl.CLOSE)) {
            deferredControlActions.clear()
            closeNow()
            return
        }
        while (!deliveringObserver && !closed && deferredControlActions.isNotEmpty()) {
            when (deferredControlActions.removeFirst()) {
                DeferredRuntimeControl.START -> startNow()
                DeferredRuntimeControl.STOP -> stopNow()
                DeferredRuntimeControl.DISCONNECT -> disconnectNow()
                DeferredRuntimeControl.CLOSE -> {
                    closeNow()
                    return
                }
            }
        }
    }

    private fun requireOwnerThread() {
        check(threadVerifier.isOwnerThread()) { "BLE runtime access must stay on its owner thread." }
    }
}
