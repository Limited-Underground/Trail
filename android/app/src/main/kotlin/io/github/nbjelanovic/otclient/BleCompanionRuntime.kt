package io.github.nbjelanovic.otclient
import io.github.nbjelanovic.otprotocol.CompanionRadioEvent
import io.github.nbjelanovic.otprotocol.CompanionTextCarrierCodec
import io.github.nbjelanovic.otprotocol.CompanionTextRecord
import io.github.nbjelanovic.otprotocol.CompanionTextReference
import io.github.nbjelanovic.otprotocol.CompanionTextSend
import io.github.nbjelanovic.otprotocol.CompanionTextResult
import io.github.nbjelanovic.otprotocol.CompanionTextReceived
import io.github.nbjelanovic.otprotocol.CompanionTextDelivery
import io.github.nbjelanovic.otprotocol.CompanionRadioEventCodec
import io.github.nbjelanovic.otprotocol.CompanionRadioEventReceiver
import io.github.nbjelanovic.otprotocol.CompanionConfigurationCodec
import io.github.nbjelanovic.otprotocol.CompanionConfigurationInfo
import io.github.nbjelanovic.otprotocol.CompanionConfigurationFrame
import io.github.nbjelanovic.otprotocol.CompanionPublicProfileCodec
import io.github.nbjelanovic.otprotocol.CompanionConfirmationCodec
import io.github.nbjelanovic.otprotocol.COMPANION_MINIMUM_ATT_MTU
import io.github.nbjelanovic.otprotocol.COMPANION_FACTORY_RESET_CAPABILITY
import io.github.nbjelanovic.otprotocol.COMPANION_KNOWN_CAPABILITY_MASK
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
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionFragment
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodecError
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot

private val configurationScopeCounter = java.util.concurrent.atomic.AtomicLong(0)
private fun nextConfigurationScope(): Long? {
    val value=configurationScopeCounter.updateAndGet { if(it==Long.MAX_VALUE) it else it+1 }
    return value.takeIf { it<Long.MAX_VALUE }
}

internal const val REQUIRED_ACTION_CAPABILITIES = 0x07 or COMPANION_FACTORY_RESET_CAPABILITY
internal const val MAX_DISCOVERED_COMPANIONS = 16
internal const val MAX_PUBLIC_LABEL_CHARS = 64
internal const val MAX_ENDPOINT_TOKEN_CHARS = 256
internal const val DEFAULT_MAX_RECONNECT_ATTEMPTS = 3
internal const val PERIODIC_RECONNECT_DELAY_MILLIS = 15_000L
internal const val INITIAL_GATT_PROFILE_TIMEOUT_MILLIS = 45_000L
internal const val NEGOTIATION_STEP_TIMEOUT_MILLIS = 15_000L
internal const val ACTION_RESULT_TIMEOUT_MILLIS = 10_000L
internal const val FACTORY_RESET_RESTART_TIMEOUT_MILLIS = 20_000L
internal const val AUTHORIZATION_RESULT_TIMEOUT_MILLIS = 30_000L
private val RECOVERED_FACTORY_RESET_COMPANION =
    BleDiscoveredCompanion("pending-reset-verification", "Trail device")

object CompanionGattV0Contract {
    const val SERVICE_UUID = "5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d0"
    /** Advertising-only marker for the active unowned pairing window; it is not a GATT service. */
    const val PAIRABLE_ADVERTISING_UUID = "5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d1"
    const val PROTOCOL_INFO_UUID = "5e0f2a01-7c6b-4ea3-a210-0c4f1f43b7d0"
    const val COMMAND_UUID = "5e0f2a02-7c6b-4ea3-a210-0c4f1f43b7d0"
    const val STREAM_UUID = "5e0f2a03-7c6b-4ea3-a210-0c4f1f43b7d0"
    const val PUBLIC_LINK_INFO_UUID = "5e0f2a04-7c6b-4ea3-a210-0c4f1f43b7d0"

    private val publicLinkInfoV0 = byteArrayOf(
        0x4f, 0x54, 0x42, 0x30,
        0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x18, 0x00,
        0x01, 0x01, 0x00, 0x00,
    )

    fun acceptsPublicLinkInfo(value: ByteArray): Boolean = value.contentEquals(publicLinkInfoV0)
}

enum class BleRuntimeBlock {
    ADAPTER_IMPLEMENTATION_MISSING,
    PLATFORM_UNSUPPORTED,
    BLUETOOTH_UNAVAILABLE,
    BLUETOOTH_DISABLED,
    BLUETOOTH_STATE_READ_DENIED,
    SCAN_PERMISSION_MISSING,
    CONNECT_PERMISSION_MISSING,
}

enum class BleRuntimeFailure {
    SCAN_START_FAILED,
    SETUP_LABEL_AMBIGUOUS,
    SETUP_LABEL_CHANGED,
    RETURNING_OWNER_AMBIGUOUS,
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
    AUTHORIZATION_UNSUPPORTED,
    AUTHORIZATION_UNAVAILABLE,
}

/**
 * Identity-free support detail for the local Android connection boundary.
 *
 * Values must never contain a Bluetooth address, endpoint token, public label, passkey, bond key,
 * exception text, or vendor status payload.
 */
enum class BleConnectionDiagnostic {
    LEASE_UNAVAILABLE,
    START_REJECTED,
    DISCONNECTED_BEFORE_PROFILE,
    GATT_TRANSIENT_LINK,
    GATT_PERMISSION_REVOKED,
    GATT_SECURITY_REJECTED,
    GATT_BOND_REQUIRED,
    GATT_AUTHORIZATION_REJECTED,
    GATT_PLATFORM_FAILURE,
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
    BOND_REQUIRED,
    AUTHORIZATION_REJECTED,
    PLATFORM_FAILURE,
}

private fun BleGattFailure.connectionDiagnostic(): BleConnectionDiagnostic = when (this) {
    BleGattFailure.TRANSIENT_LINK -> BleConnectionDiagnostic.GATT_TRANSIENT_LINK
    BleGattFailure.PERMISSION_REVOKED -> BleConnectionDiagnostic.GATT_PERMISSION_REVOKED
    BleGattFailure.SECURITY_REJECTED -> BleConnectionDiagnostic.GATT_SECURITY_REJECTED
    BleGattFailure.BOND_REQUIRED -> BleConnectionDiagnostic.GATT_BOND_REQUIRED
    BleGattFailure.AUTHORIZATION_REJECTED -> BleConnectionDiagnostic.GATT_AUTHORIZATION_REJECTED
    BleGattFailure.PLATFORM_FAILURE -> BleConnectionDiagnostic.GATT_PLATFORM_FAILURE
}

enum class BleSetupLabelIssue { AMBIGUOUS, CHANGED }

sealed interface BleScanEvent {
    data class Candidate(val companion: BleDiscoveredCompanion) : BleScanEvent
    /** Reset-only correlation proof; never a device identity or authorization credential. */
    data class FactoryResetReceiptObserved(val receipt: ULong) : BleScanEvent
    data object Complete : BleScanEvent
    data class Failed(val failure: BleGattFailure, val setupLabelIssue: BleSetupLabelIssue? = null) : BleScanEvent
}

sealed interface BleGattEvent {
    /** Exact lease now owns a non-null GATT transport; this event carries no security proof. */
    data object GattOpened : BleGattEvent
    /** Exact service and characteristic profile discovered; this event carries no security proof. */
    data object ProfileReady : BleGattEvent
    data class SecurityEstablished(val evidence: BleSecurityEvidence) : BleGattEvent
    data class MtuChanged(val mtu: Int) : BleGattEvent
    data class ProtocolInfoRead(val value: ByteArray) : BleGattEvent
    /** Successful exact device-protected read; Android does not independently measure encryption/MITM. */
    data class ProtectedProtocolInfoRead(val value: ByteArray) : BleGattEvent
    data object StreamIndicationsSubscribed : BleGattEvent
    data class StreamIndication(val value: ByteArray) : BleGattEvent
    data class Failed(val failure: BleGattFailure) : BleGattEvent
    data object Disconnected : BleGattEvent
}

enum class BleConnectionPurpose {
    INITIAL_AUTHORIZATION,
    EXISTING_OWNER,
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
 * invoke callbacks serially on the runtime owner thread, and must not infer link encryption or MITM
 * authentication from Android bond state. A concrete Android implementation enters the restricted
 * v0.1 claim path only through an exact device-protected ProtocolInfo read. Test facades may emit
 * [BleGattEvent.SecurityEstablished] as injected evidence. Add Device scans expose only the exact
 * [CompanionGattV0Contract.PAIRABLE_ADVERTISING_UUID]. The selected scan result's in-memory device
 * binding is retained for reconnects within this facade session; GATT discovery still requires only
 * [CompanionGattV0Contract.SERVICE_UUID] and its three fixed characteristics.
 */
interface AndroidBluetoothFacade {
    fun displayTimeSample(): Pair<UInt,Int> = java.time.LocalTime.now().toSecondOfDay().toUInt() to 2
    fun watchDisplayTimeChanges(callback: () -> Unit): BleReconnectLease? = null
    fun preflight(): BlePreflight
    fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease?
    /** Optional process-restart route; candidates must already be bonded and advertise owned D0 only. */
    fun createReturningOwnerScan(observer: (BleScanEvent) -> Unit): BleScanLease? = null
    /** Short saved-owner check used after fast reconnect attempts have exhausted. */
    fun createPeriodicReturningOwnerScan(observer: (BleScanEvent) -> Unit): BleScanLease? =
        createReturningOwnerScan(observer)
    /** Durably stages one nonzero random receipt before destructive bytes are handed to Android. */
    fun stageFactoryResetReceipt(): ULong? = null
    /** A verified local reset requires explicit fresh setup until a new protected session succeeds. */
    fun requiresFreshSetupAfterVerifiedReset(): Boolean = false
    fun authenticatedSessionReady() = Unit
    /** Loads an unexpired app-private pending receipt after runtime/service process recovery. */
    fun loadPendingFactoryResetReceipt(): ULong? = null
    /** Clears only the exact pending receipt after verified completion, explicit rejection, or expiry. */
    fun clearPendingFactoryResetReceipt(receipt: ULong): Boolean = false
    /**
     * Scans D1 advertisements for an exact receipt match without persisting or matching a MAC,
     * endpoint token, IRK-derived identity, or Android BluetoothDevice object.
     */
    fun createFactoryResetVerificationScan(
        receipt: ULong,
        observer: (BleScanEvent) -> Unit,
    ): BleScanLease? = null
    /**
     * Clears the exact pending receipt and app-owned volatile binding only after a matching D1
     * receipt observation. This never invokes hidden Android bond-removal APIs.
     */
    fun completeFactoryResetVerification(receipt: ULong): FactoryResetLocalCleanupResult =
        FactoryResetLocalCleanupResult.FAILED
    fun createConnection(
        endpointToken: String,
        purpose: BleConnectionPurpose,
        observer: (BleGattEvent) -> Unit,
    ): BleGattLease?
}

/** Production default: no Android Bluetooth implementation and therefore no scan/connect authority. */
class DisabledAndroidBluetoothFacade : AndroidBluetoothFacade {
    override fun preflight() = BlePreflight(BleRuntimeBlock.ADAPTER_IMPLEMENTATION_MISSING)
    override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease? = null
    override fun createConnection(
        endpointToken: String,
        purpose: BleConnectionPurpose,
        observer: (BleGattEvent) -> Unit,
    ): BleGattLease? = null
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

/** Host-only trusted input; a received event never supplies any of these authorities. */
data class BlePacketAuthorityQuery(val lease: BleGattLease, val callbackGeneration: Long,
    val nonce: UInt, val peer: String) {
    override fun toString() = "BlePacketAuthorityQuery(fields=redacted)"
}
data class BlePacketAuthority(val lease: BleGattLease, val callbackGeneration: Long,
    val nonce: UInt, val peer: String, val group: ULong, val admittedSession: ULong,
    val deviceGeneration: ULong, val expiresAtMillis: Long, val admitted: Boolean = true,
    val radioArmed: Boolean = true,
    val wireBinding: io.github.nbjelanovic.otprotocol.CompanionAuthorityResponse? = null) {
    override fun toString() = "BlePacketAuthority(fields=redacted)"
}
fun interface BlePacketAuthorityProvider { fun current(query: BlePacketAuthorityQuery): BlePacketAuthority? }

/** Copied bounded host observation, never radio authority or durable delivery history. */
data class BlePacketRadioState(val lastEvent: CompanionRadioEvent? = null,
    val terminalEvents: List<CompanionRadioEvent> = emptyList(), val eventId: UInt = 0u,
    val outcomeUnknown: Boolean = false, val lastActionResult: CompanionActionResult? = null) {
    override fun toString() = "BlePacketRadioState(terminals=${terminalEvents.size}, outcomeUnknown=$outcomeUnknown)"
}

/** Opaque, volatile correlation only; this is never a durable message identifier. */
class BlePacketRequestReference internal constructor(
    private val runtimeScope: Any, private val callbackGeneration: Long,
    private val nonce: UInt, private val exchange: UInt, private val deviceGeneration: ULong,
) {
    override fun toString() = "BlePacketRequestReference(redacted)"
}

enum class BlePacketDeliveryPhase {
    AWAITING_ACCEPTANCE, QUEUE_ACCEPTED, TRANSMISSION_COMPLETED, PROTECTED_PEER_ACK,
    AUTHORITATIVE_REJECTION, FAILED, OUTCOME_UNKNOWN, NOT_SENT,
}

data class BlePacketRequestObservation(
    val reference: BlePacketRequestReference, val status: Int,
    val phase: BlePacketDeliveryPhase, val currentAttachment: Boolean = true,
) {
    override fun toString() = "BlePacketRequestObservation(phase=$phase, currentAttachment=$currentAttachment)"
}

data class BlePacketReceivedObservation(val status: Int, val currentAttachment: Boolean = true) {
    override fun toString() = "BlePacketReceivedObservation(currentAttachment=$currentAttachment)"
}

/** A copied projection of the existing bounded receiver, not another transport owner or history. */
data class BlePacketDeliveryObservation(
    val available: Boolean = false, val currentRequest: BlePacketRequestObservation? = null,
    val receivedStatus: BlePacketReceivedObservation? = null, val observationGap: Boolean = false,
) {
    override fun toString() = "BlePacketDeliveryObservation(available=$available, request=${currentRequest?.phase}, gap=$observationGap)"
}

data class BleActiveSession(
    val companion: BleDiscoveredCompanion,
    val sessionNonce: Long,
    val snapshot: CompanionStatusSnapshot,
    val protocolInfo: CompanionProtocolInfo,
    val groupLocation: GroupLocationSnapshot,
    val lastActionResult: CompanionActionResult? = null,
    val configuration: BleConfigurationState = BleConfigurationState(),
)

enum class FactoryResetLocalCleanupResult {
    CLEARED,
    SYSTEM_BOND_REMAINS,
    FAILED,
}

enum class FactoryResetNotVerifiedReason {
    REQUEST_WRITE_UNCERTAIN,
    REQUEST_REJECTED,
    RESPONSE_TIMEOUT,
    CONNECTION_LOST_BEFORE_ACCEPTANCE,
    MALFORMED_RESPONSE,
    VERIFICATION_UNAVAILABLE,
    VERIFICATION_TIMEOUT,
    WRONG_DEVICE_OBSERVED,
    LOCAL_RECORD_CLEAR_FAILED,
}

sealed interface BleRuntimeState {
    data object Inactive : BleRuntimeState
    data object Idle : BleRuntimeState
    data class Blocked(val reason: BleRuntimeBlock) : BleRuntimeState
    data class Scanning(val candidates: List<BleDiscoveredCompanion>) : BleRuntimeState
    data class ScanComplete(val candidates: List<BleDiscoveredCompanion>) : BleRuntimeState
    data object FindingReturningOwner : BleRuntimeState
    data class AwaitingAuthorization(val companion: BleDiscoveredCompanion) : BleRuntimeState
    data class Connecting(val companion: BleDiscoveredCompanion) : BleRuntimeState
    data class Negotiating(
        val companion: BleDiscoveredCompanion,
        val phase: BleNegotiationPhase,
    ) : BleRuntimeState
    data class Ready(val session: BleActiveSession) : BleRuntimeState
    data class FactoryResetRequesting(val session: BleActiveSession) : BleRuntimeState
    data class FactoryResetErasing(val companion: BleDiscoveredCompanion) : BleRuntimeState
    data class FactoryResetVerifying(val companion: BleDiscoveredCompanion) : BleRuntimeState
    data class FactoryResetNotVerified(
        val companion: BleDiscoveredCompanion,
        val reason: FactoryResetNotVerifiedReason,
        val canRetryVerification: Boolean,
    ) : BleRuntimeState
    data class FactoryResetComplete(val systemBondRemovalRequired: Boolean) : BleRuntimeState
    data class Reconnecting(
        val companion: BleDiscoveredCompanion,
        val attempt: Int,
        val maximumAttempts: Int,
        val connectionDiagnostic: BleConnectionDiagnostic? = null,
        val periodic: Boolean = false,
    ) : BleRuntimeState
    data class Failed(
        val reason: BleRuntimeFailure,
        val connectionDiagnostic: BleConnectionDiagnostic? = null,
    ) : BleRuntimeState
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
    initialGeneration: Long = 0,
    private val evaluationConfirmationEnabled: Boolean = false,
    private val confirmationClockMillis: () -> Long = { System.nanoTime() / 1_000_000L },
    private val packetV1Enabled: Boolean = false,
    private val packetAuthority: BlePacketAuthorityProvider = BlePacketAuthorityProvider { null },
) : AutoCloseable {
    init {
        require(maximumReconnectAttempts in 0..16)
        require(firstRequestId in 1..0xffff_ffffL)
        require(initialGeneration in 0..Long.MAX_VALUE)
    }

    @Volatile
    var state: BleRuntimeState = BleRuntimeState.Inactive
        private set

    private var observer: ((BleRuntimeState) -> Unit)? = null
    private var deliveringObserver = false
    private val deferredControlActions = ArrayDeque<DeferredRuntimeControl>()
    private var lifecycleActive = false
    private var closed = false
    private var generation = initialGeneration
    private var scanLease: BleScanLease? = null
    private var gattLease: BleGattLease? = null
    private var gattOpenedObserved = false
    private var reconnectLease: BleReconnectLease? = null
    private var operationTimeoutLease: BleReconnectLease? = null
    private var selected: BleDiscoveredCompanion? = null
    private var reconnectAttempt = 0
    private var periodicReconnect = false
    private var lifecycleAuthorizationFailure: BleRuntimeFailure? = null
    private var negotiatedMtu = 0
    private var protocolInfo: CompanionProtocolInfo? = null
    private var configurationInfo: CompanionConfigurationInfo? = null
    private var configurationSession: BleConfigurationSession? = null
    private var groupConfirmationSession: BleGroupConfirmationSession? = null
    private var groupConfirmationTimeout: BleReconnectLease? = null
    private var matchedSetupLabel: V1SetupLabel? = null
    private var configurationTimeout: BleReconnectLease? = null
    private var displayTimeChanges: BleReconnectLease? = null
    private var automaticNameReadPending = false
    private var automaticTimeSyncPending = false
    private var pumpingAutomaticConfiguration = false
    private var normalProfileConfirmedAfterPromotion = false
    private var activeSessionNonce = 0L
    private var lastDeviceEventId = 0L
    private var nextRequestId = firstRequestId
    private var pendingAction: PendingAction? = null
    private val packetReceiver = CompanionRadioEventReceiver()
    private val packetProjectionScope = Any()
    var packetDelivery = BlePacketDeliveryObservation()
        private set
    private var quickStatusRecipient: V1QuickStatusRecipient? = null
    private var packetBinding: BlePacketAuthority? = null
    private val textConversation = V1TextConversation()
    private var textOperation = 0uL
    private var textHandedOff = false
    private var textTimeout: BleReconnectLease? = null
    private var textExpiry: BleReconnectLease? = null
    private var packetEventCursor = 0u
    private var packetReceiptFloor = 0uL
    private var packetReceiptGeneration = 0uL
    private var queryingPacketAuthority = false
    private data class PacketOrigin(val exchange: UInt, val detail: Int,
        var handedOff: Boolean = false, var result: CompanionActionResult? = null,
        var terminal: Boolean = false, var observed: Boolean = false)
    private var packetOrigin: PacketOrigin? = null
    private var packetTimeout: BleReconnectLease? = null
    var packetRadioState = BlePacketRadioState()
        private set
    private var factoryResetVerificationTarget: FactoryResetVerificationTarget? = null
    private var authorizationProtocolInfo: CompanionAuthorizationProtocolInfo? = null
    private var authorizationSecurityEvidence: BleSecurityEvidence? = null
    private var authorizationTransportGeneration = 0L
    private var authorizationOperationCounter = 0L
    private var deliveringAuthorizationObserver = false
    private enum class DiscoveryRoute { ADD_DEVICE, RETURNING_OWNER }
    private var retryDiscoveryRoute = DiscoveryRoute.RETURNING_OWNER
    private var pendingSnapshotExchangeId = 0L
    private val authorizationTracker = CompanionAuthorizationResponseTracker()
    private var authorizationClaim: RuntimeAuthorizationClaim? = null
    private data class PendingAction(val exchangeId: Long, val request: CompanionActionRequest)
    private data class FactoryResetVerificationTarget(
        val receipt: ULong,
        val companion: BleDiscoveredCompanion?,
    )
    private data class RuntimeAuthorizationClaim(
        val operationId: Long,
        val companion: BleDiscoveredCompanion,
        val purpose: DeviceAuthorizationPurpose,
        val publicOperationToken: String,
        val observer: (DeviceAuthorizationClaimEvent) -> Unit,
        val automaticReturningOwner: Boolean = false,
        var started: Boolean = false,
        var terminal: Boolean = false,
        var promoted: Boolean = false,
        var pendingReported: Boolean = false,
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
        val resetTarget = factoryResetVerificationTarget ?: facade.loadPendingFactoryResetReceipt()?.let { receipt ->
            if (receipt == 0uL) null else FactoryResetVerificationTarget(receipt, companion = null)
        }
        resetTarget?.let {
            factoryResetVerificationTarget = it
            beginFactoryResetVerification(it)
            return
        }
        if (facade.requiresFreshSetupAfterVerifiedReset()) {
            selected = null
            publish(BleRuntimeState.Idle)
            return
        }
        lifecycleAuthorizationFailure?.let {
            publish(BleRuntimeState.Failed(it))
            return
        }
        val remembered = selected
        if (remembered == null) {
            if (!beginReturningOwnerDiscovery()) {
                val preflight = facade.preflight()
                publish(if (preflight.isReady) BleRuntimeState.Idle
                    else BleRuntimeState.Blocked(preflight.blocker!!))
            }
        } else if (periodicReconnect) {
            beginPeriodicDiscovery()
        } else {
            beginConnection(remembered, isReconnect = true, purpose = BleConnectionPurpose.EXISTING_OWNER)
        }
    }

    /**
     * Starts the separate process-restart route when the platform supports it. Absence of platform
     * support or failed preflight preserves the prior Idle startup behavior; an admitted lease owns
     * its own state and reports concrete startup/callback failures.
     */
    private fun beginReturningOwnerDiscovery(periodicCycle: Boolean = false): Boolean {
        retryDiscoveryRoute = DiscoveryRoute.RETURNING_OWNER
        if (!facade.preflight().isReady) return false
        var callbackGeneration = 0L
        val candidates = linkedMapOf<String, BleDiscoveredCompanion>()
        val scanObserver: (BleScanEvent) -> Unit = { event ->
            onReturningOwnerScanEvent(callbackGeneration, candidates, event, periodicCycle)
        }
        val lease = if (periodicCycle) facade.createPeriodicReturningOwnerScan(scanObserver)
            else facade.createReturningOwnerScan(scanObserver)
        if (lease == null) return false
        val nextGeneration = nextGeneration()
        if (nextGeneration == null) {
            lease.close()
            return true
        }
        callbackGeneration = nextGeneration
        scanLease = lease
        publish(BleRuntimeState.FindingReturningOwner)
        if (!accepts(callbackGeneration) || scanLease !== lease || state !== BleRuntimeState.FindingReturningOwner) {
            return true
        }
        if (!lease.start()) {
            lease.close()
            if (scanLease === lease) scanLease = null
            if (generation == callbackGeneration) {
                val preflight = facade.preflight()
                val remembered = selected
                if (!preflight.isReady) {
                    selected = null
                    publish(BleRuntimeState.Blocked(preflight.blocker!!))
                } else if (periodicCycle && remembered != null) {
                    handleConnectionFailure(remembered, BleRuntimeFailure.SCAN_START_FAILED, transient = true)
                } else {
                    publish(BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED))
                }
            }
        }
        return true
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
        if (authorizationClaim?.let { it.pendingReported || it.terminal } == true) {
            lifecycleAuthorizationFailure = BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST
            selected = null
        }
        lifecycleActive = false
        invalidateAndRelease()
        authorizationClaim = null
        publish(BleRuntimeState.Inactive)
    }

    /** Explicit retry on the existing owner, preserving the last discovery route. */
    @Synchronized
    fun retryDiscovery() {
        requireOwnerThread()
        if (deliveringObserver || closed || !lifecycleActive || state !is BleRuntimeState.Blocked ||
            authorizationClaim != null || factoryResetVerificationTarget != null ||
            facade.loadPendingFactoryResetReceipt() != null
        ) return
        if (retryDiscoveryRoute == DiscoveryRoute.ADD_DEVICE) {
            requestScan()
            return
        }
        val preflight = facade.preflight()
        if (!preflight.isReady) {
            publish(BleRuntimeState.Blocked(preflight.blocker!!))
            return
        }
        selected = null
        reconnectAttempt = 0
        invalidateAndRelease()
        if (!beginReturningOwnerDiscovery()) {
            val current = facade.preflight()
            publish(if (current.isReady) BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED)
                else BleRuntimeState.Blocked(current.blocker!!))
        }
    }

    @Synchronized
    fun requestScan() {
        requireOwnerThread()
        if (deliveringObserver) return
        if (!lifecycleActive || closed) return
        retryDiscoveryRoute = DiscoveryRoute.ADD_DEVICE
        lifecycleAuthorizationFailure = null
        val resetTarget = factoryResetVerificationTarget ?: facade.loadPendingFactoryResetReceipt()?.let { receipt ->
            if (receipt == 0uL) null else FactoryResetVerificationTarget(receipt, companion = null)
        }
        resetTarget?.let {
            factoryResetVerificationTarget = it
            beginFactoryResetVerification(it)
            return
        }
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
        val candidates = when (val current = state) {
            is BleRuntimeState.Scanning -> current.candidates
            is BleRuntimeState.ScanComplete -> current.candidates
            else -> return null
        }
        val candidate = candidates.singleOrNull { it.endpointToken == endpointToken } ?: return null
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
        beginConnection(
            awaiting.companion,
            isReconnect = false,
            purpose = BleConnectionPurpose.INITIAL_AUTHORIZATION,
        )
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
     * Runtime-backed claim seam used by the explicit Bluetooth mode. It remains fail-closed unless
     * the exact device-protected v0.1 ProtocolInfo path is admitted on this runtime owner thread.
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
        beginConnection(
            claim.companion,
            isReconnect = false,
            purpose = BleConnectionPurpose.INITIAL_AUTHORIZATION,
        )
        // Once connection startup is admitted, injected/platform callbacks may synchronously reach
        // Pending or a terminal local/device result before beginConnection returns. Those callbacks
        // retain authority, so start must report true even if the claim has already ended.
        return true
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
        if (
            state is BleRuntimeState.FactoryResetRequesting ||
            state is BleRuntimeState.FactoryResetErasing ||
            state is BleRuntimeState.FactoryResetVerifying
        ) return
        selected = null
        reconnectAttempt = 0
        invalidateAndRelease()
        authorizationClaim = null
        publish(if (lifecycleActive) BleRuntimeState.Idle else BleRuntimeState.Inactive)
    }

    @Synchronized
    fun submitAction(request: CompanionActionRequest): Boolean {
        requireOwnerThread()
        if (request.kind == CompanionActionKind.FACTORY_RESET) return false
        return submitActionInternal(request, factoryResetAuthorized = false)
    }

    /** Dedicated fixed-status lane: never falls back to a legacy/plaintext action. */
    @Synchronized
    fun submitQuickStatus(recipient: V1QuickStatusRecipient, status: CompanionQuickStatus): Boolean {
        requireOwnerThread()
        if (recipient !== quickStatusRecipient || !currentPacketAuthority()) return false
        // Reentrant provider calls cannot replace the selected recipient between these checks.
        if (recipient !== quickStatusRecipient) return false
        return submitActionInternal(CompanionActionRequest(CompanionActionKind.QUICK_STATUS, status), false)
    }

    @Synchronized
    fun quickStatusState(): V1QuickStatusState {
        requireOwnerThread()
        val recipient = quickStatusRecipient
        if (state !is BleRuntimeState.Ready || !packetDelivery.available || recipient == null)
            return V1QuickStatusState()
        return V1QuickStatusState(recipient,
            packetDelivery.receivedStatus?.takeIf { it.currentAttachment }?.let { CompanionQuickStatus.fromWire(it.status) },
            packetDelivery.currentRequest?.takeIf { it.currentAttachment }?.phase)
    }

    /** Text never supplies peer, identity, group, epoch or authority. */
    fun submitText(recipient: V1QuickStatusRecipient, text: String): Boolean {
        requireOwnerThread()
        if (deliveringObserver || queryingPacketAuthority || V1TextComposer.refusal(text) != null) return false
        val ready = state as? BleRuntimeState.Ready ?: return false
        val lease = gattLease ?: return false
        if (recipient !== quickStatusRecipient) return false
        val binding = currentTextBinding() ?: return false
        if (textConversation.busy || pendingAction != null || packetReceiver.pendingRequest || packetReceiver.activeEvent != null ||
            configurationSession?.busy == true || groupConfirmationSession?.busy == true) return false
        val id = nextRequestId
        if (id !in 1..0xffff_ffffL || textOperation == ULong.MAX_VALUE) {
            retirePacket(BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED, unknown = false); return false
        }
        val request = CompanionTextSend(CompanionTextReference(binding.nonce, binding.exchange, binding.sessionGeneration),
            id.toUInt(), binding.epoch, textOperation + 1uL, text)
        val encoded = CompanionTextCarrierCodec.encode(request, allowProfile06 = true) ?: return false
        if (currentTextBinding() !== binding || recipient !== quickStatusRecipient) return false
        if (!textConversation.begin(request)) return false
        textHandedOff = false; nextRequestId = id + 1; textOperation = request.operation
        publish(BleRuntimeState.Ready(ready.session))
        if (currentTextBinding() !== binding || recipient !== quickStatusRecipient || textConversation.origin !== request) {
            retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED, unknown = false); return false
        }
        textHandedOff = true
        val written = try { lease.writeCommandWithResponse(encoded) } catch (_: Exception) { false }
        if (currentTextBinding() !== binding || !written) {
            retirePacket(if (written) BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED else BleRuntimeFailure.ACTION_WRITE_FAILED,
                unknown = textConversation.busy)
            return false
        }
        if (textConversation.busy) {
            val local = generation
            val timer = scheduler.schedule(ACTION_RESULT_TIMEOUT_MILLIS) {
                requireOwnerThread()
                if (accepts(local) && textConversation.origin === request && textConversation.busy)
                    retirePacket(BleRuntimeFailure.ACTION_RESULT_TIMEOUT, unknown = true)
            }
            if (accepts(local) && textConversation.origin === request && textConversation.busy) textTimeout = timer else timer.close()
        }
        return true
    }

    /** Copied UI hints only; each command and incoming record rechecks the actual current provider. */
    fun textState(): V1TextState {
        requireOwnerThread()
        if (!deliveringObserver && !queryingPacketAuthority && packetBinding?.wireBinding != null) currentTextBinding()
        val recipient = quickStatusRecipient?.takeIf { state is BleRuntimeState.Ready && packetBinding?.wireBinding != null }
        return V1TextState(recipient, textConversation.received.takeIf { recipient != null }, textConversation.phase,
            textConversation.reason, textConversation.currentAttachment && recipient != null)
    }

    private fun currentTextBinding(): io.github.nbjelanovic.otprotocol.CompanionAuthorityResponse? {
        if (!currentPacketAuthority()) {
            if (packetBinding?.wireBinding != null) retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED, unknown = textConversation.busy)
            return null
        }
        val proof = packetBinding ?: return null
        val wire = proof.wireBinding ?: return null
        if (wire.isRefusal || wire.nonce != proof.nonce || wire.group != proof.group ||
            wire.deviceGeneration != proof.deviceGeneration || wire.sessionGeneration != proof.admittedSession) return null
        return wire
    }

    private fun armTextExpiry(proof: BlePacketAuthority, now: Long): Boolean {
        if (proof.wireBinding == null) return true
        if (now < 0 || now >= proof.expiresAtMillis) return false
        textExpiry?.close()
        val local = generation
        val timer = try { scheduler.schedule(proof.expiresAtMillis - now) {
            requireOwnerThread()
            if (accepts(local) && packetBinding === proof) {
                if (!currentPacketAuthority()) retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED, unknown = textConversation.busy)
                else if (!armTextExpiry(proof, confirmationClockMillis())) retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED, unknown = textConversation.busy)
            }
        } } catch (_: Exception) { return false }
        if (accepts(local) && packetBinding === proof) textExpiry = timer else timer.close()
        return accepts(local) && packetBinding === proof
    }

    private fun acceptTextRecord(value: ByteArray) {
        val record = CompanionTextCarrierCodec.decode(value, allowProfile06 = true) ?: return
        if (record is CompanionTextSend) return // Device input can never submit an Android command.
        val binding = currentTextBinding() ?: run {
            if (packetBinding != null) retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED, unknown = textConversation.busy)
            return
        }
        if (record.reference != CompanionTextReference(binding.nonce, binding.exchange, binding.sessionGeneration)) return
        val event = record is CompanionTextReceived || record is CompanionTextDelivery
        if (event) {
            if (record.exchange <= packetEventCursor) return
            if (packetEventCursor == UInt.MAX_VALUE || record.exchange != packetEventCursor + 1u) {
                retirePacket(BleRuntimeFailure.PROTOCOL_VIOLATION, unknown = true); return
            }
        }
        val receipt = when (record) {
            is CompanionTextResult -> record.receipt
            is CompanionTextDelivery -> record.receipt
            else -> 0uL
        }
        if (receipt != 0uL && receipt <= packetReceiptFloor && textConversation.receipt != receipt) return
        if (!textConversation.receive(record)) {
            if (textConversation.faulted) retirePacket(BleRuntimeFailure.PROTOCOL_VIOLATION, unknown = true)
            return
        }
        if (event) packetEventCursor = record.exchange
        if (receipt > packetReceiptFloor) packetReceiptFloor = receipt
        if (!textConversation.busy) { textTimeout?.close(); textTimeout = null }
        val ready = state as? BleRuntimeState.Ready ?: return
        publish(BleRuntimeState.Ready(ready.session))
    }

    /**
     * Dedicated destructive authority. Presentation must obtain explicit confirmation before calling
     * this method; the generic action API can never submit a factory reset.
     */
    @Synchronized
    fun submitFactoryReset(): Boolean {
        requireOwnerThread()
        return submitActionInternal(
            CompanionActionRequest(CompanionActionKind.FACTORY_RESET),
            factoryResetAuthorized = true,
        )
    }

    private fun submitActionInternal(
        request: CompanionActionRequest,
        factoryResetAuthorized: Boolean,
    ): Boolean {
        if (deliveringObserver) return false
        val ready = state as? BleRuntimeState.Ready ?: return false
        val lease = gattLease ?: return false
        if (pendingAction != null || textConversation.busy || configurationSession?.busy == true || groupConfirmationSession?.busy == true) return false
        if (
            request.kind == CompanionActionKind.FACTORY_RESET &&
            (
                !factoryResetAuthorized ||
                    (ready.session.protocolInfo.capabilities and COMPANION_FACTORY_RESET_CAPABILITY) == 0
                )
        ) return false
        val isPacket = configurationInfo?.minorVersion == 6
        if (isPacket && (request.kind != CompanionActionKind.QUICK_STATUS ||
                !currentPacketAuthority() || packetReceiver.pendingRequest || packetReceiver.activeEvent != null)) return false
        val requestId = nextRequestId
        if (requestId !in 1..0xffff_ffffL) {
            handleConnectionFailure(
                ready.session.companion,
                BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED,
                transient = true,
            )
            return false
        }
        val effectiveRequest = if (request.kind == CompanionActionKind.FACTORY_RESET) {
            val receipt = facade.stageFactoryResetReceipt() ?: return false
            request.copy(factoryResetReceipt = receipt)
        } else {
            request
        }
        val payload = CompanionSemanticCodec.encodeActionRequest(effectiveRequest).value ?: run {
            if (effectiveRequest.kind == CompanionActionKind.FACTORY_RESET) {
                facade.clearPendingFactoryResetReceipt(effectiveRequest.factoryResetReceipt)
            }
            return false
        }
        val encoded = encodeNormalFragment(
            CompanionFragment(
                kind = CompanionFrameKind.ACTION_REQUEST,
                sessionNonce = ready.session.sessionNonce,
                exchangeId = requestId,
                payload = payload,
            ),
        ) ?: run {
            if (effectiveRequest.kind == CompanionActionKind.FACTORY_RESET) {
                facade.clearPendingFactoryResetReceipt(effectiveRequest.factoryResetReceipt)
            }
            return false
        }

        if (effectiveRequest.kind == CompanionActionKind.FACTORY_RESET) {
            // Once bytes are handed to Android, a false/timeout/disconnect result is ambiguous: the
            // device may still have durably accepted and executed the reset. Retain only the exact
            // ephemeral target needed to verify its later D1 boot; never infer completion here.
            factoryResetVerificationTarget = FactoryResetVerificationTarget(
                effectiveRequest.factoryResetReceipt,
                ready.session.companion,
            )
        }
        if (isPacket) {
            if (!currentPacketAuthority()) return false
            val detail = checkNotNull(request.quickStatus).wireValue
            if (!packetReceiver.registerRequest(requestId.toUInt(), detail)) return false
            packetOrigin = PacketOrigin(requestId.toUInt(), detail)
            packetDelivery=packetDelivery.copy(currentRequest=BlePacketRequestObservation(
                BlePacketRequestReference(packetProjectionScope,generation,ready.session.sessionNonce.toUInt(),
                    requestId.toUInt(),checkNotNull(packetBinding).deviceGeneration),detail,
                BlePacketDeliveryPhase.AWAITING_ACCEPTANCE))
        }
        // Consume an ID before handing bytes to the platform because a false return can still be uncertain.
        nextRequestId = requestId + 1
        pendingAction = PendingAction(requestId, effectiveRequest)
        if(isPacket) publish(BleRuntimeState.Ready(ready.session.copy(lastActionResult=null)))
        if (isPacket && !currentPacketAuthority()) {
            retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED, unknown=false)
            return false
        }
        val submittedOrigin=if(isPacket) packetOrigin else null
        if (isPacket) submittedOrigin?.handedOff=true
        val written=if(isPacket) try { lease.writeCommandWithResponse(encoded) } catch(_:Exception) { false } else lease.writeCommandWithResponse(encoded)
        if (isPacket && (!currentPacketAuthority() || !written)) {
            // A synchronous accepted response/event cannot be undone by a false platform return.
            retirePacket(if(written) BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED else BleRuntimeFailure.ACTION_WRITE_FAILED,
                unknown=submittedOrigin?.terminal != true)
            return false
        }
        if (!written) {
            if (effectiveRequest.kind == CompanionActionKind.FACTORY_RESET) {
                // A synchronous callback may already have published the authoritative admitted or
                // rejected state. Only classify uncertainty while this exact request is still pending.
                if (pendingAction?.exchangeId == requestId && state is BleRuntimeState.Ready) {
                    factoryResetNotVerified(
                        ready.session.companion,
                        FactoryResetNotVerifiedReason.REQUEST_WRITE_UNCERTAIN,
                        canRetryVerification = true,
                    )
                }
            } else {
                pendingAction = null
                failAndRelease(BleRuntimeFailure.ACTION_WRITE_FAILED)
            }
            return false
        }
        if (
            effectiveRequest.kind == CompanionActionKind.FACTORY_RESET &&
            pendingAction?.exchangeId == requestId &&
            state is BleRuntimeState.Ready
        ) {
            publish(BleRuntimeState.FactoryResetRequesting(ready.session))
        }
        if (
            pendingAction?.exchangeId == requestId &&
            (state is BleRuntimeState.Ready || state is BleRuntimeState.FactoryResetRequesting)
        ) {
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
        factoryResetVerificationTarget = null
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
            is BleScanEvent.FactoryResetReceiptObserved -> Unit
            BleScanEvent.Complete -> {
                scanLease?.close()
                scanLease = null
                publish(BleRuntimeState.ScanComplete(candidates.values.toList()))
            }
            is BleScanEvent.Failed -> {
                scanLease?.close()
                scanLease = null
                candidates.clear()
                publish(BleRuntimeState.Failed(when (event.setupLabelIssue) {
                    BleSetupLabelIssue.AMBIGUOUS -> BleRuntimeFailure.SETUP_LABEL_AMBIGUOUS
                    BleSetupLabelIssue.CHANGED -> BleRuntimeFailure.SETUP_LABEL_CHANGED
                    null -> BleRuntimeFailure.SCAN_START_FAILED
                }))
            }
        }
    }

    @Synchronized
    private fun onReturningOwnerScanEvent(
        callbackGeneration: Long,
        candidates: LinkedHashMap<String, BleDiscoveredCompanion>,
        event: BleScanEvent,
        periodicCycle: Boolean,
    ) {
        requireOwnerThread()
        if (deliveringObserver) return
        if (!accepts(callbackGeneration) || state !== BleRuntimeState.FindingReturningOwner) return
        when (event) {
            is BleScanEvent.Candidate -> {
                val candidate = event.companion
                if (
                    candidate.endpointToken.isBlank() ||
                    candidate.endpointToken.length > MAX_ENDPOINT_TOKEN_CHARS ||
                    candidate.publicLabel.isBlank() ||
                    candidate.publicLabel.length > MAX_PUBLIC_LABEL_CHARS
                ) return
                candidates[candidate.endpointToken] = candidate.copy()
                if (candidates.size > 1) {
                    selected = null
                    scanLease?.close()
                    scanLease = null
                    publish(BleRuntimeState.Failed(BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS))
                }
            }
            is BleScanEvent.FactoryResetReceiptObserved -> Unit
            BleScanEvent.Complete -> {
                scanLease?.close()
                scanLease = null
                val candidate = candidates.values.singleOrNull()
                if (candidate == null) {
                    val remembered = selected
                    if (periodicCycle && remembered != null) {
                        handleConnectionFailure(remembered, BleRuntimeFailure.CONNECTION_START_FAILED, transient = true)
                    } else {
                        selected = null
                        publish(BleRuntimeState.Idle)
                    }
                } else {
                    beginReturningOwnerConnection(candidate, isReconnect = periodicCycle)
                }
            }
            is BleScanEvent.Failed -> {
                scanLease?.close()
                scanLease = null
                val remembered = selected
                if (periodicCycle && remembered != null && event.failure in
                    listOf(BleGattFailure.TRANSIENT_LINK, BleGattFailure.PLATFORM_FAILURE)) {
                    handleConnectionFailure(remembered, BleRuntimeFailure.SCAN_START_FAILED, transient = true)
                } else {
                    selected = null
                    publish(BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED))
                }
            }
        }
    }

    private fun beginConnection(
        companion: BleDiscoveredCompanion,
        isReconnect: Boolean,
        purpose: BleConnectionPurpose,
    ) {
        if (!lifecycleActive || closed) return
        val priorConnectionDiagnostic =
            (state as? BleRuntimeState.Reconnecting)?.connectionDiagnostic
        invalidateAndRelease()
        val preflight = facade.preflight()
        if (!preflight.isReady) {
            val claim = authorizationClaim
            if (claim != null && (!claim.automaticReturningOwner || claim.pendingReported || claim.terminal)) {
                finishAuthorizationTransportFailure(BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE)
            } else {
                // No wire claim started: retain the phone blocker so the existing owner retry can recover.
                if (claim?.automaticReturningOwner == true) {
                    authorizationClaim = null
                    selected = null
                }
                publish(BleRuntimeState.Blocked(preflight.blocker!!))
            }
            return
        }
        val callbackGeneration = nextGeneration() ?: return
        clearSessionState()
        // A first-use scan match belongs only to this exact connection attempt.
        matchedSetupLabel = if (purpose == BleConnectionPurpose.INITIAL_AUTHORIZATION)
            V1SetupLabel.create(companion.publicLabel) else null
        val lease = facade.createConnection(companion.endpointToken, purpose) { event ->
            onGattEvent(callbackGeneration, companion, purpose, event)
        }
        if (lease == null) {
            handleConnectionFailure(
                companion,
                BleRuntimeFailure.CONNECTION_START_FAILED,
                transient = isReconnect,
                connectionDiagnostic = BleConnectionDiagnostic.LEASE_UNAVAILABLE,
            )
            return
        }
        gattLease = lease
        publish(
            if (isReconnect) {
                BleRuntimeState.Reconnecting(
                    companion,
                    reconnectAttempt.coerceAtLeast(1),
                    maximumReconnectAttempts,
                    priorConnectionDiagnostic,
                    periodic = periodicReconnect,
                )
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
            if (
                generation == callbackGeneration &&
                (state is BleRuntimeState.Connecting || state is BleRuntimeState.Reconnecting)
            ) {
                handleConnectionFailure(
                    companion,
                    BleRuntimeFailure.CONNECTION_START_FAILED,
                    transient = true,
                    connectionDiagnostic = BleConnectionDiagnostic.START_REJECTED,
                )
            }
        }
    }

    /**
     * A saved bond removes the PIN/pairing step, but it does not replace the device-authoritative
     * application claim required for each fresh GATT session. Keep that claim internal so a
     * returning owner reconnects without UI interaction while using the same protected wire path
     * as first authorization.
     */
    private fun beginReturningOwnerConnection(
        companion: BleDiscoveredCompanion,
        isReconnect: Boolean,
    ) {
        if (authorizationClaim != null || authorizationOperationCounter == Long.MAX_VALUE) {
            selected = null
            publish(BleRuntimeState.Failed(BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED))
            return
        }
        retryDiscoveryRoute = DiscoveryRoute.RETURNING_OWNER
        authorizationOperationCounter += 1
        val operationId = authorizationOperationCounter
        authorizationClaim = RuntimeAuthorizationClaim(
            operationId = operationId,
            companion = companion,
            purpose = DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE,
            publicOperationToken = "returning-owner-$operationId",
            observer = {},
            automaticReturningOwner = true,
            started = true,
        )
        selected = companion
        if (!isReconnect) {
            reconnectAttempt = 0
            periodicReconnect = false
        }
        beginConnection(
            companion,
            isReconnect = isReconnect,
            purpose = BleConnectionPurpose.EXISTING_OWNER,
        )
    }

    @Synchronized
    private fun onGattEvent(
        callbackGeneration: Long,
        companion: BleDiscoveredCompanion,
        purpose: BleConnectionPurpose,
        event: BleGattEvent,
    ) {
        requireOwnerThread()
        if (!accepts(callbackGeneration) || gattLease == null) return
        if (deliveringAuthorizationObserver) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        if (deliveringObserver) return
        if (
            (event is BleGattEvent.Failed || event is BleGattEvent.Disconnected) &&
            handleFactoryResetTransportEnd(companion)
        ) return
        when (event) {
            BleGattEvent.GattOpened -> onGattOpened()
            BleGattEvent.ProfileReady -> onProfileReady(companion)
            is BleGattEvent.SecurityEstablished -> {
                if (purpose == BleConnectionPurpose.EXISTING_OWNER) {
                    failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED)
                } else {
                    onSecurity(companion, event.evidence)
                }
            }
            is BleGattEvent.MtuChanged -> onMtu(companion, event.mtu)
            is BleGattEvent.ProtocolInfoRead -> {
                if (purpose == BleConnectionPurpose.EXISTING_OWNER) {
                    failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED)
                } else {
                    onProtocolInfo(companion, event.value)
                }
            }
            is BleGattEvent.ProtectedProtocolInfoRead -> onProtectedProtocolInfo(companion, purpose, event.value)
            BleGattEvent.StreamIndicationsSubscribed -> onStreamSubscribed(companion)
            is BleGattEvent.StreamIndication -> onStreamValue(companion, event.value)
            is BleGattEvent.Failed -> {
                val claimOwnsFailure = authorizationClaimOwnsFailure()
                handleConnectionFailure(
                    companion,
                    if (claimOwnsFailure) {
                        BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST
                    } else when (event.failure) {
                        BleGattFailure.SECURITY_REJECTED -> BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED
                        BleGattFailure.BOND_REQUIRED -> BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED
                        BleGattFailure.AUTHORIZATION_REJECTED -> BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE
                        BleGattFailure.PERMISSION_REVOKED -> BleRuntimeFailure.CONNECTION_START_FAILED
                        BleGattFailure.PLATFORM_FAILURE -> BleRuntimeFailure.CONNECTION_START_FAILED
                        BleGattFailure.TRANSIENT_LINK -> BleRuntimeFailure.CONNECTION_START_FAILED
                    },
                    transient = !claimOwnsFailure && event.failure == BleGattFailure.TRANSIENT_LINK,
                    connectionDiagnostic = event.failure.connectionDiagnostic(),
                )
            }
            BleGattEvent.Disconnected -> {
                val claimOwnsFailure = authorizationClaimOwnsFailure()
                val beforeReturningOwnerProfile =
                    purpose == BleConnectionPurpose.EXISTING_OWNER &&
                        (state is BleRuntimeState.Connecting || state is BleRuntimeState.Reconnecting)
                handleConnectionFailure(
                    companion,
                    if (claimOwnsFailure) {
                        BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST
                    } else {
                        BleRuntimeFailure.CONNECTION_START_FAILED
                    },
                    transient = !claimOwnsFailure,
                    connectionDiagnostic = if (beforeReturningOwnerProfile) {
                        BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE
                    } else {
                        null
                    },
                )
            }
        }
        pumpAutomaticConfiguration()
    }

    private fun authorizationClaimOwnsFailure(): Boolean {
        val claim = authorizationClaim ?: return false
        return !claim.automaticReturningOwner || claim.pendingReported || claim.terminal
    }

    private fun onGattOpened() {
        if (
            gattOpenedObserved ||
            (state !is BleRuntimeState.Connecting && state !is BleRuntimeState.Reconnecting)
        ) return
        gattOpenedObserved = true
        armNegotiationTimeout(generation, null, INITIAL_GATT_PROFILE_TIMEOUT_MILLIS)
    }

    private fun onProfileReady(companion: BleDiscoveredCompanion) {
        if (state !is BleRuntimeState.Connecting && state !is BleRuntimeState.Reconnecting) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.PROTOCOL_INFO))
        if (!hasActiveGattPhase(BleNegotiationPhase.PROTOCOL_INFO)) return
        if (gattLease?.readProtocolInfo() != true) {
            failAndRelease(BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE)
        } else if (isPhase(BleNegotiationPhase.PROTOCOL_INFO)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.PROTOCOL_INFO)
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
        if (protocolInfo != null) {
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

    private fun decodeNormalInfo(value: ByteArray): CompanionProtocolInfo? {
        val packet=if(packetV1Enabled) CompanionConfigurationCodec.decodeInfo(value,6,allowProfile06=true) else null
        val configuration=packet ?: CompanionConfigurationCodec.decodeInfo(value) ?: CompanionConfigurationCodec.decodeInfo(value,3) ?:
            CompanionConfigurationCodec.decodeInfo(value,CompanionPublicProfileCodec.PROFILE) ?:
            if(evaluationConfirmationEnabled) CompanionConfigurationCodec.decodeInfo(value,CompanionConfirmationCodec.PROFILE) else null
        configurationInfo=configuration
        return if(configuration!=null) CompanionProtocolInfo(capabilities=configuration.capabilities,
            maxFragmentCount=1) else CompanionProtocolCodec.decodeProtocolInfo(value).value
    }

    private fun requiredNormalCapabilities(): Int =
        if (configurationInfo?.minorVersion == CompanionConfirmationCodec.PROFILE) 0x07 else REQUIRED_ACTION_CAPABILITIES

    /** Only an explicitly opted-in, protected Ready evaluation profile can expose this adapter. */
    fun createGroupConfirmationSource(authority: V1GroupConfirmationAuthority): V1GroupConfirmationSource? {
        requireOwnerThread()
        val ready = state as? BleRuntimeState.Ready ?: return null
        if (!evaluationConfirmationEnabled || configurationInfo?.minorVersion != CompanionConfirmationCodec.PROFILE ||
            negotiatedMtu < COMPANION_MINIMUM_ATT_MTU || groupConfirmationSession != null) return null
        val callbackGeneration = generation
        val nonce = ready.session.sessionNonce
        val session = BleGroupConfirmationSession(authority, nonce.toUInt(),
            isCurrent = { accepts(callbackGeneration) &&
                (state as? BleRuntimeState.Ready)?.session?.sessionNonce == nonce &&
                configurationInfo?.minorVersion == CompanionConfirmationCodec.PROFILE },
            allocate = {
                if (deliveringObserver || state !is BleRuntimeState.Ready || pendingAction != null ||
                    configurationSession?.busy == true || groupConfirmationSession?.busy == true ||
                    nextRequestId !in 1..0xffff_ffffL) null
                else nextRequestId.toUInt().also { nextRequestId++ }
            },
            send = { bytes ->
                val sent = gattLease?.writeCommandWithResponse(bytes) == true
                armGroupConfirmationTimeout()
                sent
            },
            clockMillis = confirmationClockMillis)
        groupConfirmationSession = session
        return session
    }

    private fun armGroupConfirmationTimeout() {
        groupConfirmationTimeout?.close()
        groupConfirmationTimeout = null
        val session = groupConfirmationSession ?: return
        val exchange = session.pendingExchange() ?: return
        val callbackGeneration = generation
        val lease = scheduler.schedule(6000) {
            requireOwnerThread()
            if (accepts(callbackGeneration) && groupConfirmationSession === session) session.lost(exchange)
        }
        if (accepts(callbackGeneration) && groupConfirmationSession === session && session.pendingExchange() == exchange)
            groupConfirmationTimeout = lease
        else lease.close()
    }

    private fun encodeNormalFragment(fragment: CompanionFragment): ByteArray? =
        if(configurationInfo!=null) CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(
            fragment.kind.wireValue,fragment.sessionNonce.toUInt(),fragment.exchangeId.toUInt(),fragment.payload,checkNotNull(configurationInfo).minorVersion),allowProfile06=packetV1Enabled)
        else CompanionProtocolCodec.encodeFragment(fragment).value

    private fun acceptPromotedNormalInfo(companion: BleDiscoveredCompanion,value: ByteArray) {
        val claim=authorizationClaim ?: return
        val decoded=decodeNormalInfo(value)
        if(decoded==null || (decoded.capabilities and requiredNormalCapabilities())!=requiredNormalCapabilities() ||
            negotiatedMtu<decoded.minimumAttMtu) return failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
        protocolInfo=decoded
        normalProfileConfirmedAfterPromotion=true
        operationTimeoutLease?.close();operationTimeoutLease=null
        continueAfterAuthorizationPromotion(companion,claim)
    }

    fun readPublicProfile(): Boolean { requireOwnerThread(); return configurationOperation { it.readPublicProfile() } }
    fun writePublicProfile(name: String, visible: Boolean): Boolean { requireOwnerThread(); return configurationOperation { it.writePublicProfile(name,visible) } }
    fun readRadioRegion(): Boolean { requireOwnerThread(); return configurationOperation { it.readRegion() } }
    fun writeRadioRegion(selectionId: Int): Boolean { requireOwnerThread(); return configurationOperation { it.writeRegion(selectionId) } }
    fun readDeviceName(): Boolean { requireOwnerThread(); return configurationOperation { it.readName() } }
    fun writeDeviceName(value: String): Boolean {
        requireOwnerThread()
        val name=V1DeviceName.create(value) ?: return false
        return configurationOperation { it.writeName(name) }
    }
    fun synchronizeDisplayTime(): Boolean { requireOwnerThread(); return configurationOperation { it.synchronizeTime() } }
    // Coalesce source-clock changes; queued work never steals an occupied action/configuration lane.
    private fun pumpAutomaticConfiguration() {
        if(pumpingAutomaticConfiguration || deliveringObserver || state !is BleRuntimeState.Ready || pendingAction!=null || groupConfirmationSession?.busy==true) return
        val session=configurationSession ?: return
        if(session.busy || (!automaticNameReadPending && !automaticTimeSyncPending)) return
        pumpingAutomaticConfiguration=true
        try {
            while(configurationSession===session && state is BleRuntimeState.Ready && pendingAction==null && !session.busy && groupConfirmationSession?.busy!=true) {
                when {
                    automaticNameReadPending -> { automaticNameReadPending=false;session.readName() }
                    automaticTimeSyncPending -> { automaticTimeSyncPending=false;session.synchronizeTime() }
                    else -> break
                }
                // Consume before sending, including an uncertain write: no automatic retry loop.
            }
            armConfigurationTimeout()
        } finally { pumpingAutomaticConfiguration=false }
    }
    private fun configurationOperation(operation: (BleConfigurationSession)->Boolean): Boolean {
        if(deliveringObserver || state !is BleRuntimeState.Ready || pendingAction!=null || groupConfirmationSession?.busy==true) return false
        val session=configurationSession ?: return false
        val result=operation(session)
        if(result) armConfigurationTimeout()
        pumpAutomaticConfiguration()
        return result
    }
    private fun initializeConfigurationSession() {
        val ready=state as? BleRuntimeState.Ready ?: return
        val info=configurationInfo ?: return
        if(!CompanionConfigurationCodec.transportCompatible(info,0x40,negotiatedMtu,148,148,true,allowProfile06=packetV1Enabled) ||
            !CompanionConfigurationCodec.transportCompatible(info,0x80,negotiatedMtu,148,148,true,allowProfile06=packetV1Enabled)) return
        val callbackGeneration=generation
        val nonce=ready.session.sessionNonce
        if(info.minorVersion==6 && !currentPacketAuthority(bind=true)) return
        val scope=nextConfigurationScope() ?: return
        val tokenBytes=ByteArray(16)
        repeat(8) { tokenBytes[it]=(scope shr (it*8)).toByte();tokenBytes[8+it]=(callbackGeneration shr (it*8)).toByte() }
        val token=V1SetupSessionToken.create(tokenBytes) ?: return
        val context=V1NameContext(scope,nextConfigurationScope() ?: return,nextConfigurationScope() ?: return,
            nextConfigurationScope() ?: return,callbackGeneration,nextConfigurationScope() ?: return,nonce.toUInt(),matchedSetupLabel,token)
        configurationSession=BleConfigurationSession(context,minorVersion=info.minorVersion,allowProfile06=packetV1Enabled,
            isCurrent={ accepts(callbackGeneration) && (state as? BleRuntimeState.Ready)?.session?.sessionNonce==nonce && (info.minorVersion!=6 || currentPacketAuthority()) },
            allocate={
                if(info.minorVersion==6 && !currentPacketAuthority() || nextRequestId !in 1..0xffff_ffffL) null else nextRequestId.toUInt().also { nextRequestId++ }
            },
            send={ bytes ->
                if(info.minorVersion==6 && !currentPacketAuthority()) false
                else {
                    val sent=if(info.minorVersion==6) try { gattLease?.writeCommandWithResponse(bytes)==true } catch(_:Exception) { false } else gattLease?.writeCommandWithResponse(bytes)==true
                    if(info.minorVersion==6 && (!sent || !currentPacketAuthority())) {
                        retirePacket(BleRuntimeFailure.ACTION_WRITE_FAILED,unknown=true);false
                    } else sent
                }
            },
            civilSample={ facade.displayTimeSample() },
            changed={ configuration ->
                val current=state as? BleRuntimeState.Ready
                if(accepts(callbackGeneration) && current?.session?.sessionNonce==nonce && (info.minorVersion!=6 || currentPacketAuthority()))
                    publish(BleRuntimeState.Ready(current.session.copy(configuration=configuration)))
            })
        val session=checkNotNull(configurationSession)
        automaticNameReadPending=true
        automaticTimeSyncPending=true
        publish(BleRuntimeState.Ready(ready.session.copy(configuration=session.state)))
        if(!accepts(callbackGeneration) || configurationSession!==session || state !is BleRuntimeState.Ready) return
        val watcher=facade.watchDisplayTimeChanges {
            requireOwnerThread()
            if(accepts(callbackGeneration) && configurationSession===session && state is BleRuntimeState.Ready) {
                automaticTimeSyncPending=true
                pumpAutomaticConfiguration()
            }
        }
        if(accepts(callbackGeneration) && configurationSession===session) displayTimeChanges=watcher else watcher?.close()
        pumpAutomaticConfiguration()
    }
    private fun armConfigurationTimeout() {
        configurationTimeout?.close();configurationTimeout=null
        val session=configurationSession ?: return
        val exchange=session.pendingExchange() ?: return
        val callbackGeneration=generation
        configurationTimeout=scheduler.schedule(6000) {
            requireOwnerThread()
            if(accepts(callbackGeneration) && configurationSession===session) {
                if(configurationInfo?.minorVersion==6 && !currentPacketAuthority()) {
                    retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=true);return@schedule
                }
                session.lost(exchange)
                pumpAutomaticConfiguration()
            }
        }
    }

    private fun onProtocolInfo(companion: BleDiscoveredCompanion, value: ByteArray) {
        if (!isPhase(BleNegotiationPhase.PROTOCOL_INFO)) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        if (authorizationClaim?.promoted == true) {
            failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED)
            return
        }
        if (authorizationClaim != null) {
            acceptAuthorizationProtocolInfo(companion, value)
            return
        }
        val decoded = decodeNormalInfo(value)
        if (
            decoded == null ||
            configurationInfo?.minorVersion == CompanionConfirmationCodec.PROFILE ||
            (decoded.capabilities and requiredNormalCapabilities()) != requiredNormalCapabilities() ||
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

    private fun onProtectedProtocolInfo(
        companion: BleDiscoveredCompanion,
        purpose: BleConnectionPurpose,
        value: ByteArray,
    ) {
        if (!isPhase(BleNegotiationPhase.PROTOCOL_INFO)) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        if(authorizationClaim?.promoted == true) {
            acceptPromotedNormalInfo(companion,value)
            return
        }
        if (purpose == BleConnectionPurpose.EXISTING_OWNER && authorizationClaim == null) {
            acceptReturningOwnerProtocolInfo(companion, value)
            return
        }
        if (
            authorizationClaim == null ||
            (purpose != BleConnectionPurpose.INITIAL_AUTHORIZATION && purpose != BleConnectionPurpose.EXISTING_OWNER)
        ) {
            failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED)
            return
        }
        val decoded = CompanionAuthorizationProtocolInfoCodec.decode(value).value
        if (decoded == null) {
            val legacyProtocol = CompanionProtocolCodec.decodeProtocolInfo(value).value
            failAndRelease(
                if (legacyProtocol != null) {
                    BleRuntimeFailure.AUTHORIZATION_UNSUPPORTED
                } else {
                    BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE
                },
            )
            return
        }
        acceptAuthorizationProtocolInfo(companion, value, deviceProtectedReadAccepted = true)
    }

    /** Legacy branch retained for configurations that expose the protected normal v0.0 record. */
    private fun acceptReturningOwnerProtocolInfo(companion: BleDiscoveredCompanion, value: ByteArray) {
        val decoded = decodeNormalInfo(value)
        if (
            decoded == null ||
            (decoded.capabilities and requiredNormalCapabilities()) != requiredNormalCapabilities() ||
            decoded.maxFragmentPayloadBytes < COMPANION_STATUS_SNAPSHOT_BYTES
        ) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            return
        }
        protocolInfo = decoded
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        if (negotiatedMtu > 0) {
            if (negotiatedMtu < decoded.minimumAttMtu) {
                failAndRelease(BleRuntimeFailure.MTU_NEGOTIATION_FAILED)
                return
            }
            publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.STREAM_SUBSCRIPTION))
            if (gattLease?.subscribeStreamIndications() != true) {
                failAndRelease(BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED)
            } else if (isPhase(BleNegotiationPhase.STREAM_SUBSCRIPTION)) {
                armNegotiationTimeout(generation, BleNegotiationPhase.STREAM_SUBSCRIPTION)
            }
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

    private fun acceptAuthorizationProtocolInfo(
        companion: BleDiscoveredCompanion,
        value: ByteArray,
        deviceProtectedReadAccepted: Boolean = false,
    ) {
        val decoded = CompanionAuthorizationProtocolInfoCodec.decode(value).value
        val evidence = authorizationSecurityEvidence
        if (
            decoded == null ||
            (!deviceProtectedReadAccepted && evidence == null) ||
            decoded.minimumNormalAttMtu < COMPANION_MINIMUM_ATT_MTU
        ) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            return
        }
        // This tracker admission names the device-enforced protected read directly. It does not
        // represent Android measuring encryption/MITM, and OS bond state alone cannot reach here.
        val provisionalEvidence = if (deviceProtectedReadAccepted) {
            CompanionAuthorizationProvisionalEvidence(
                transportGeneration = generation.toULong(),
                linkEncrypted = true,
                authenticatedBond = true,
                claimWireSupported = true,
            )
        } else {
            CompanionAuthorizationProvisionalEvidence(
                transportGeneration = generation.toULong(),
                linkEncrypted = evidence!!.encrypted,
                authenticatedBond = evidence.authenticatedBond,
                claimWireSupported = true,
            )
        }
        val trackerError = authorizationTracker.openProvisionalSession(
            provisionalEvidence,
            decoded.provisionalSessionNonce,
        )
        if (trackerError != CompanionAuthorizationWireError.NONE) {
            failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            return
        }
        authorizationTransportGeneration = generation
        authorizationProtocolInfo = decoded
        protocolInfo = CompanionProtocolInfo(
            capabilities = decoded.capabilities and COMPANION_KNOWN_CAPABILITY_MASK,
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
        if (authorizationClaim == null) return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        val info = authorizationProtocolInfo ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        val exchangeId = firstRequestId
        val purpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
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

    /** No production provider is connected; only a trusted caller may supply this context. */
    private fun currentPacketAuthority(bind: Boolean = false, preReady: BleDiscoveredCompanion? = null): Boolean {
        if(!packetV1Enabled || configurationInfo?.minorVersion!=6 || deliveringObserver || queryingPacketAuthority) return false
        val expectedState=state
        val ready=state as? BleRuntimeState.Ready
        val companion=ready?.session?.companion ?: preReady ?: return false
        if(ready==null) {
            val negotiating=expectedState as? BleRuntimeState.Negotiating ?: return false
            if(negotiating.companion!=companion || negotiating.phase !in listOf(BleNegotiationPhase.PROTOCOL_INFO,BleNegotiationPhase.INITIAL_SNAPSHOT) ||
                packetBinding==null && authorizationClaim?.promoted!=true) return false
        }
        val nonce=ready?.session?.sessionNonce ?: activeSessionNonce
        if(nonce !in 1..0xffff_ffffL) return false
        val lease=gattLease ?: return false
        val local=generation
        if(!accepts(local)) return false
        val query=BlePacketAuthorityQuery(lease,local,nonce.toUInt(),companion.endpointToken)
        queryingPacketAuthority=true
        val sampled=try { packetAuthority.current(query) to confirmationClockMillis() } catch(_:Exception) { null }
            finally { queryingPacketAuthority=false }
        val proof=sampled?.first ?: return false
        val now=sampled.second
        if(!accepts(local) || gattLease!==lease || state!==expectedState || activeSessionNonce!=query.nonce.toLong() ||
            proof.lease!==lease || proof.callbackGeneration!=local || proof.nonce!=query.nonce || proof.peer!=query.peer ||
            proof.group==0uL || proof.admittedSession==0uL || proof.deviceGeneration==0uL ||
            !proof.admitted || !proof.radioArmed || now < 0 || now>=proof.expiresAtMillis) return false
        val prior=packetBinding
        if(prior!=null) return prior==proof && packetReceiver.connected
        if(!bind) return false
        if(!packetReceiver.open(query.nonce,proof.deviceGeneration)) return false
        quickStatusRecipient=V1QuickStatusRecipient()
        packetBinding=proof
        if (packetReceiptGeneration != proof.deviceGeneration) { packetReceiptFloor = 0uL; packetReceiptGeneration = proof.deviceGeneration }
        packetEventCursor = 0u; textOperation = 0uL
        proof.wireBinding?.let { wire ->
            nextRequestId = maxOf(nextRequestId, wire.exchange.toLong() + 1L)
            textConversation.attach(wire)
        }
        if (!armTextExpiry(proof, now) || !accepts(local) || packetBinding !== proof || gattLease !== lease || state !== expectedState) return false
        packetDelivery=packetDelivery.copy(available=true)
        return true
    }

    private fun retirePacket(reason: BleRuntimeFailure, unknown: Boolean) {
        if(unknown) {
            packetRadioState=packetRadioState.copy(outcomeUnknown=true)
            if(packetOrigin?.terminal!=true) updatePacketPhase(BlePacketDeliveryPhase.OUTCOME_UNKNOWN)
        }
        if(closed) return
        // Retirement releases only Android BLE ownership; there is no radio-stop command.
        failAndRelease(reason)
    }

    fun cancelPacketOperation(): Boolean {
        requireOwnerThread()
        if(deliveringObserver || queryingPacketAuthority || configurationInfo?.minorVersion!=6 || packetOrigin==null || packetOrigin?.terminal==true && packetOrigin?.result!=null) return false
        retirePacket(BleRuntimeFailure.ACTION_RESULT_TIMEOUT,unknown=packetOrigin?.handedOff==true && packetOrigin?.terminal!=true)
        return true
    }

    private fun armPacketTimeout(origin: PacketOrigin) {
        packetTimeout?.close()
        val local=generation
        val timer=scheduler.schedule(ACTION_RESULT_TIMEOUT_MILLIS) {
            requireOwnerThread()
            if(accepts(local) && packetOrigin===origin && !origin.terminal)
                retirePacket(BleRuntimeFailure.ACTION_RESULT_TIMEOUT,unknown=true)
        }
        if(accepts(local) && packetOrigin===origin && !origin.terminal) packetTimeout=timer else timer.close()
    }

    private fun updatePacketPhase(phase: BlePacketDeliveryPhase) {
        val current=packetDelivery.currentRequest ?: return
        if(packetOrigin==null || !current.currentAttachment) return
        packetDelivery=packetDelivery.copy(currentRequest=current.copy(phase=phase))
    }

    private fun acceptPacketEvent(value: ByteArray) {
        // Decode first for structure only: no generation bootstrap or state allocation from input.
        val copied=value.copyOf()
        val record=CompanionRadioEventCodec.decodeRecord(copied,allowProfile06=true) ?: return
        if(!currentPacketAuthority()) {
            if(packetBinding!=null) retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=packetOrigin?.handedOff==true)
            return
        }
        if (record.eventId <= packetEventCursor) return
        if (packetBinding?.wireBinding != null && (packetEventCursor == UInt.MAX_VALUE || record.eventId != packetEventCursor + 1u)) {
            retirePacket(BleRuntimeFailure.PROTOCOL_VIOLATION, unknown = true); return
        }
        if (record.event.kind in 1..2 && record.event.receipt <= packetReceiptFloor && record.event.receipt != packetReceiver.activeEvent?.receipt) return
        val terminalsBefore=packetReceiver.terminalCount
        if(!packetReceiver.accept(copied)) return
        val event=record.event
        packetEventCursor = record.eventId
        if (event.kind in 1..2 && event.receipt > packetReceiptFloor) packetReceiptFloor = event.receipt
        val insertedTerminal=event.kind==2 || event.kind==1 && event.state in listOf(3,5,6)
        packetDelivery=packetDelivery.copy(observationGap=packetDelivery.observationGap || event.kind==4 ||
            insertedTerminal && terminalsBefore==16)
        if(event.kind==1) updatePacketPhase(when(event.state) {
            1 -> BlePacketDeliveryPhase.QUEUE_ACCEPTED
            2 -> BlePacketDeliveryPhase.TRANSMISSION_COMPLETED
            3 -> BlePacketDeliveryPhase.PROTECTED_PEER_ACK
            5 -> BlePacketDeliveryPhase.FAILED
            else -> BlePacketDeliveryPhase.OUTCOME_UNKNOWN
        })
        if(event.kind==2) packetDelivery=packetDelivery.copy(receivedStatus=BlePacketReceivedObservation(event.detail))
        if(event.kind==1) packetOrigin?.observed=true
        if(event.kind==1 && event.state in listOf(3,5,6)) {
            packetOrigin?.terminal=true;packetTimeout?.close();packetTimeout=null
        }
        packetRadioState=packetRadioState.copy(lastEvent=event,terminalEvents=packetReceiver.terminalEvents(),
            eventId=record.eventId,outcomeUnknown=packetRadioState.outcomeUnknown || packetReceiver.deliveryUnknown)
        if(event.kind in listOf(3,4)) {
            retirePacket(BleRuntimeFailure.PROTOCOL_VIOLATION,unknown=true)
            return
        }
        val ready=state as? BleRuntimeState.Ready ?: return
        publish(BleRuntimeState.Ready(ready.session))
    }

    private fun onStreamValue(companion: BleDiscoveredCompanion, value: ByteArray) {
        if (packetV1Enabled && configurationInfo?.minorVersion == 6 && value.size > 6 &&
            (value[6].toInt() and 255) in listOf(CompanionTextCarrierCodec.RESULT, CompanionTextCarrierCodec.RECEIVED, CompanionTextCarrierCodec.DELIVERY)) {
            acceptTextRecord(value); return
        }
        if(configurationInfo?.minorVersion==6 && value.size>6 && (value[6].toInt() and 255)==0x83) {
            acceptPacketEvent(value)
            return
        }
        if(configurationInfo?.minorVersion==6 && state is BleRuntimeState.Ready && !currentPacketAuthority()) {
            retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=packetOrigin?.handedOff==true)
            return
        }
        val configurationFrame = if(configurationInfo != null) CompanionConfigurationCodec.decodeFrame(value,checkNotNull(configurationInfo).minorVersion,allowProfile06=packetV1Enabled) else null
        if (configurationFrame?.kind == CompanionConfirmationCodec.RESPONSE) {
            if (groupConfirmationSession?.receive(configurationFrame) == true) {
                armGroupConfirmationTimeout()
                pumpAutomaticConfiguration()
            }
            return
        }
        if(configurationFrame?.kind in listOf(0x86,0x87,0x88,CompanionPublicProfileCodec.RESPONSE)) {
            if(configurationSession?.receive(checkNotNull(configurationFrame)) == true) {
                armConfigurationTimeout()
                return
            }
            // Unsolicited or stale operation results cannot mutate current work.
            return
        }
        val fragment = if(configurationInfo != null) configurationFrame?.let { frame ->
            CompanionFrameKind.fromWire(frame.kind)?.let { CompanionFragment(it,frame.sessionNonce.toLong(),frame.exchangeId.toLong(),payload=frame.payload) }
        } else CompanionProtocolCodec.decodeFragment(value).value
        if (authorizationClaim != null && isPhase(BleNegotiationPhase.AUTHORIZATION_CLAIM)) {
            acceptAuthorizationStreamValue(companion, fragment)
            return
        }
        if (
            fragment == null ||
            fragment.payload.size > (protocolInfo?.maxFragmentPayloadBytes ?: 0) ||
            CompanionSemanticCodec.validateSemanticFragment(fragment) != CompanionSemanticCodecError.NONE
        ) {
            if (state is BleRuntimeState.FactoryResetRequesting) {
                factoryResetNotVerified(
                    companion,
                    FactoryResetNotVerifiedReason.MALFORMED_RESPONSE,
                    canRetryVerification = true,
                )
                return
            }
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
        val activeSession = when (val current = state) {
            is BleRuntimeState.Ready -> current.session
            is BleRuntimeState.FactoryResetRequesting -> current.session
            else -> null
        }
        if (activeSession == null || fragment.sessionNonce != activeSessionNonce) {
            if (state is BleRuntimeState.FactoryResetRequesting) {
                factoryResetNotVerified(
                    companion,
                    FactoryResetNotVerifiedReason.MALFORMED_RESPONSE,
                    canRetryVerification = true,
                )
                return
            }
            failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            return
        }
        when (fragment.kind) {
            CompanionFrameKind.SNAPSHOT -> {
                val ready = state as? BleRuntimeState.Ready
                if (ready == null) {
                    factoryResetNotVerified(
                        companion,
                        FactoryResetNotVerifiedReason.MALFORMED_RESPONSE,
                        canRetryVerification = true,
                    )
                } else {
                    acceptSnapshotUpdate(ready, fragment)
                }
            }
            CompanionFrameKind.ACTION_RESULT -> acceptActionResult(activeSession, fragment)
            else -> {
                if (state is BleRuntimeState.FactoryResetRequesting) {
                    factoryResetNotVerified(
                        companion,
                        FactoryResetNotVerifiedReason.MALFORMED_RESPONSE,
                        canRetryVerification = true,
                    )
                } else {
                    failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
                }
            }
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
                claim.pendingReported = true
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
                when (observation.outcome) {
                    CompanionAuthorizationClaimOutcome.ACCEPTED -> {
                        operationTimeoutLease?.close()
                        operationTimeoutLease = null
                        claim.terminal = true
                        claim.promoted = true
                        emitAuthorizationEvent(
                            claim,
                            DeviceAuthorizationClaimEvent.Accepted(claim.publicOperationToken),
                        )
                        continueAfterAuthorizationPromotion(companion, claim)
                    }
                    CompanionAuthorizationClaimOutcome.REPLACED -> {
                        // The protocol codec retains this legacy value for compatibility, but V1
                        // exposes no replacement claim. It can never promote app authority.
                        failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
                    }
                    CompanionAuthorizationClaimOutcome.DENIED -> {
                        operationTimeoutLease?.close()
                        operationTimeoutLease = null
                        claim.terminal = true
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
        if(!normalProfileConfirmedAfterPromotion) {
            publish(BleRuntimeState.Negotiating(companion,BleNegotiationPhase.PROTOCOL_INFO))
            if(gattLease?.readProtocolInfo()!=true) failAndRelease(BleRuntimeFailure.PROTOCOL_INFO_FAILED)
            else if(isPhase(BleNegotiationPhase.PROTOCOL_INFO)) armNegotiationTimeout(generation,BleNegotiationPhase.PROTOCOL_INFO)
            return
        }
        activeSessionNonce = info.provisionalSessionNonce
        val packetProfile=configurationInfo?.minorVersion==6
        if(packetProfile && !currentPacketAuthority(bind=true,preReady=companion)) {
            failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED);return
        }
        val exchangeId = if (packetProfile && packetBinding?.wireBinding != null) nextRequestId else firstRequestId
        if (exchangeId !in 1..0xffff_fffeL) {
            failAndRelease(BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED)
            return
        }
        val payload = CompanionSemanticCodec.encodeSnapshotRequest()
        val encoded = encodeNormalFragment(
            CompanionFragment(
                kind = CompanionFrameKind.SNAPSHOT_REQUEST,
                sessionNonce = activeSessionNonce,
                exchangeId = exchangeId,
                payload = payload,
            ),
        ) ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        pendingSnapshotExchangeId = exchangeId
        publish(BleRuntimeState.Negotiating(companion, BleNegotiationPhase.INITIAL_SNAPSHOT))
        if (!hasActiveGattPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) return
        if(packetProfile && !currentPacketAuthority(preReady=companion)) {
            retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=false);return
        }
        val written=if(packetProfile) try { gattLease?.writeCommandWithResponse(encoded)==true } catch(_:Exception) { false } else gattLease?.writeCommandWithResponse(encoded)==true
        if(packetProfile && (!written || !currentPacketAuthority(preReady=companion))) {
            retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=false);return
        }
        if (!written) {
            failAndRelease(BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED)
        } else if (isPhase(BleNegotiationPhase.INITIAL_SNAPSHOT)) {
            armNegotiationTimeout(generation, BleNegotiationPhase.INITIAL_SNAPSHOT)
        }
    }

    private fun acceptInitialSnapshot(companion: BleDiscoveredCompanion, fragment: CompanionFragment) {
        val packetProfile=configurationInfo?.minorVersion==6
        if(packetProfile && !currentPacketAuthority(preReady=companion)) {
            failAndRelease(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED);return
        }
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
        nextRequestId = maxOf(nextRequestId, if (pendingSnapshotExchangeId == 0L) {
            firstRequestId
        } else {
            pendingSnapshotExchangeId + 1
        })
        pendingAction = null
        pendingSnapshotExchangeId = 0
        authorizationClaim = null
        reconnectAttempt = 0
        periodicReconnect = false
        val groupLocation = GroupLocationSnapshot.authoritativeUnavailable(
            snapshot.revision,
            snapshot.positionSharing,
        ) ?: return failAndRelease(BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED)
        facade.authenticatedSessionReady()
        if(packetProfile && !currentPacketAuthority(preReady=companion)) {
            retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=false);return
        }
        publish(
            BleRuntimeState.Ready(
                BleActiveSession(companion, fragment.sessionNonce, snapshot, info, groupLocation),
            ),
        )
        initializeConfigurationSession()
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
        val groupLocation = ready.session.groupLocation.withAuthoritativeStatus(
            snapshot.revision,
            snapshot.positionSharing,
        ) ?: return failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
        publish(BleRuntimeState.Ready(ready.session.copy(snapshot = snapshot, groupLocation = groupLocation)))
    }

    private fun acceptActionResult(session: BleActiveSession, fragment: CompanionFragment) {
        if(configurationInfo?.minorVersion==6) {
            val origin=packetOrigin ?: return
            val result=CompanionSemanticCodec.decodeActionResult(fragment.payload).value ?: return
            if(fragment.exchangeId.toUInt()!=origin.exchange || result.kind!=CompanionActionKind.QUICK_STATUS ||
                result.quickStatus?.wireValue!=origin.detail) return
            if(!currentPacketAuthority()) {
                retirePacket(BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,unknown=true);return
            }
            if(origin.result!=null) {
                if(origin.result!=result) {
                    packetRadioState=packetRadioState.copy(lastActionResult=result)
                    updatePacketPhase(BlePacketDeliveryPhase.OUTCOME_UNKNOWN)
                    retirePacket(BleRuntimeFailure.PROTOCOL_VIOLATION,unknown=true)
                }
                return
            }
            origin.result=result
            packetRadioState=packetRadioState.copy(lastActionResult=result)
            pendingAction=null;operationTimeoutLease?.close();operationTimeoutLease=null
            if(result.disposition==CompanionActionDisposition.REJECTED) {
                origin.terminal=true
                updatePacketPhase(if(origin.observed) BlePacketDeliveryPhase.OUTCOME_UNKNOWN else BlePacketDeliveryPhase.AUTHORITATIVE_REJECTION)
                retirePacket(BleRuntimeFailure.PROTOCOL_VIOLATION,
                    unknown=origin.observed)
                return
            }
            if(packetDelivery.currentRequest?.phase==BlePacketDeliveryPhase.AWAITING_ACCEPTANCE)
                updatePacketPhase(BlePacketDeliveryPhase.QUEUE_ACCEPTED)
            if(!origin.terminal) armPacketTimeout(origin)
            publish(BleRuntimeState.Ready(session.copy(lastActionResult=result)))
            return
        }
        val pending = pendingAction
        val result = CompanionSemanticCodec.decodeActionResult(fragment.payload).value
        if (
            pending == null ||
            fragment.exchangeId != pending.exchangeId ||
            result == null ||
            result.kind != pending.request.kind ||
            result.quickStatus != pending.request.quickStatus ||
            result.criticalAlertId != pending.request.criticalAlertId ||
            result.factoryResetReceipt != pending.request.factoryResetReceipt
        ) {
            if (pending?.request?.kind == CompanionActionKind.FACTORY_RESET) {
                factoryResetNotVerified(
                    session.companion,
                    FactoryResetNotVerifiedReason.MALFORMED_RESPONSE,
                    canRetryVerification = true,
                )
            } else {
                failAndRelease(BleRuntimeFailure.PROTOCOL_VIOLATION)
            }
            return
        }
        pendingAction = null
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        if (result.kind == CompanionActionKind.FACTORY_RESET) {
            if (result.disposition != CompanionActionDisposition.ADMITTED) {
                factoryResetNotVerified(
                    session.companion,
                    FactoryResetNotVerifiedReason.REQUEST_REJECTED,
                    canRetryVerification = false,
                )
                return
            }
            val target = factoryResetVerificationTarget
            if (target == null || target.receipt != result.factoryResetReceipt) {
                factoryResetNotVerified(
                    session.companion,
                    FactoryResetNotVerifiedReason.MALFORMED_RESPONSE,
                    canRetryVerification = true,
                )
                return
            }
            publish(BleRuntimeState.FactoryResetErasing(session.companion))
            armFactoryResetRestartTimeout(generation, target)
            return
        }
        publish(BleRuntimeState.Ready(session.copy(lastActionResult = result)))
    }

    private fun handleFactoryResetTransportEnd(companion: BleDiscoveredCompanion): Boolean {
        return when (val current = state) {
            is BleRuntimeState.FactoryResetRequesting -> {
                if (current.session.companion.endpointToken != companion.endpointToken) return false
                factoryResetNotVerified(
                    companion,
                    FactoryResetNotVerifiedReason.CONNECTION_LOST_BEFORE_ACCEPTANCE,
                    canRetryVerification = true,
                )
                true
            }
            is BleRuntimeState.FactoryResetErasing -> {
                if (current.companion.endpointToken != companion.endpointToken) return false
                val target = factoryResetVerificationTarget ?: return false
                beginFactoryResetVerification(target)
                true
            }
            else -> false
        }
    }

    @Synchronized
    fun retryFactoryResetVerification(): Boolean {
        requireOwnerThread()
        if (deliveringObserver || !lifecycleActive || closed) return false
        val current = state as? BleRuntimeState.FactoryResetNotVerified ?: return false
        val target = factoryResetVerificationTarget ?: return false
        if (!current.canRetryVerification) return false
        beginFactoryResetVerification(target)
        return state is BleRuntimeState.FactoryResetVerifying
    }

    private fun beginFactoryResetVerification(target: FactoryResetVerificationTarget) {
        val companion = target.companion ?: RECOVERED_FACTORY_RESET_COMPANION
        if (
            target.receipt == 0uL ||
            factoryResetVerificationTarget?.receipt != target.receipt ||
            facade.loadPendingFactoryResetReceipt() != target.receipt
        ) {
            factoryResetVerificationTarget = null
            invalidateAndRelease()
            publish(
                BleRuntimeState.FactoryResetNotVerified(
                    companion,
                    FactoryResetNotVerifiedReason.VERIFICATION_TIMEOUT,
                    canRetryVerification = false,
                ),
            )
            return
        }
        invalidateAndRelease()
        if (!lifecycleActive || closed || !facade.preflight().isReady) {
            factoryResetNotVerified(
                companion,
                FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE,
                canRetryVerification = true,
            )
            return
        }
        var callbackGeneration = 0L
        val lease = facade.createFactoryResetVerificationScan(target.receipt) { event ->
            onFactoryResetVerificationScanEvent(callbackGeneration, target, event)
        }
        if (lease == null) {
            factoryResetNotVerified(
                companion,
                FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE,
                canRetryVerification = true,
            )
            return
        }
        val nextGeneration = nextGeneration()
        if (nextGeneration == null) {
            lease.close()
            factoryResetNotVerified(
                companion,
                FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE,
                canRetryVerification = true,
            )
            return
        }
        callbackGeneration = nextGeneration
        scanLease = lease
        publish(BleRuntimeState.FactoryResetVerifying(companion))
        if (
            !accepts(callbackGeneration) ||
            scanLease !== lease ||
            state !is BleRuntimeState.FactoryResetVerifying
        ) return
        if (!lease.start()) {
            lease.close()
            if (scanLease === lease) scanLease = null
            factoryResetNotVerified(
                companion,
                FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE,
                canRetryVerification = true,
            )
        }
    }

    @Synchronized
    private fun onFactoryResetVerificationScanEvent(
        callbackGeneration: Long,
        target: FactoryResetVerificationTarget,
        event: BleScanEvent,
    ) {
        requireOwnerThread()
        if (deliveringObserver || !accepts(callbackGeneration)) return
        val companion = target.companion ?: RECOVERED_FACTORY_RESET_COMPANION
        if (state !is BleRuntimeState.FactoryResetVerifying) return
        if (factoryResetVerificationTarget?.receipt != target.receipt) return
        when (event) {
            is BleScanEvent.FactoryResetReceiptObserved -> {
                if (event.receipt != target.receipt) {
                    factoryResetNotVerified(
                        companion,
                        FactoryResetNotVerifiedReason.WRONG_DEVICE_OBSERVED,
                        canRetryVerification = true,
                    )
                    return
                }
                scanLease?.close()
                scanLease = null
                val cleanup = facade.completeFactoryResetVerification(target.receipt)
                when (cleanup) {
                    FactoryResetLocalCleanupResult.FAILED -> factoryResetNotVerified(
                        companion,
                        FactoryResetNotVerifiedReason.LOCAL_RECORD_CLEAR_FAILED,
                        canRetryVerification = true,
                    )
                    FactoryResetLocalCleanupResult.CLEARED,
                    FactoryResetLocalCleanupResult.SYSTEM_BOND_REMAINS,
                    -> {
                        val systemBondRemovalRequired =
                            cleanup == FactoryResetLocalCleanupResult.SYSTEM_BOND_REMAINS
                        factoryResetVerificationTarget = null
                        selected = null
                        invalidateAndRelease()
                        publish(BleRuntimeState.FactoryResetComplete(systemBondRemovalRequired))
                    }
                }
            }
            is BleScanEvent.Candidate -> Unit
            BleScanEvent.Complete -> factoryResetNotVerified(
                companion,
                FactoryResetNotVerifiedReason.VERIFICATION_TIMEOUT,
                canRetryVerification = true,
            )
            is BleScanEvent.Failed -> factoryResetNotVerified(
                companion,
                FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE,
                canRetryVerification = true,
            )
        }
    }

    private fun factoryResetNotVerified(
        companion: BleDiscoveredCompanion,
        reason: FactoryResetNotVerifiedReason,
        canRetryVerification: Boolean,
    ) {
        invalidateAndRelease()
        if (!canRetryVerification) {
            factoryResetVerificationTarget?.let { facade.clearPendingFactoryResetReceipt(it.receipt) }
            factoryResetVerificationTarget = null
        }
        publish(BleRuntimeState.FactoryResetNotVerified(companion, reason, canRetryVerification))
    }

    private fun handleConnectionFailure(
        companion: BleDiscoveredCompanion,
        reason: BleRuntimeFailure,
        transient: Boolean,
        connectionDiagnostic: BleConnectionDiagnostic? = null,
    ) {
        val activeClaim = authorizationClaim
        if (
            activeClaim != null &&
            (!activeClaim.automaticReturningOwner || activeClaim.pendingReported || activeClaim.terminal)
        ) {
            finishAuthorizationTransportFailure(reason, connectionDiagnostic)
            return
        }
        if (activeClaim?.automaticReturningOwner == true) authorizationClaim = null
        operationTimeoutLease?.close()
        operationTimeoutLease = null
        gattLease?.close()
        gattLease = null
        clearSessionState()
        if (!transient || !lifecycleActive || selected?.endpointToken != companion.endpointToken) {
            selected = null
            publish(BleRuntimeState.Failed(reason, connectionDiagnostic))
            return
        }
        if (maximumReconnectAttempts == 0) {
            selected = null
            publish(
                BleRuntimeState.Failed(
                    BleRuntimeFailure.RECONNECT_EXHAUSTED,
                    connectionDiagnostic,
                ),
            )
            return
        }
        // Saturate the fast-attempt counter; an extended absence cannot wrap it.
        periodicReconnect = reconnectAttempt >= maximumReconnectAttempts
        if (!periodicReconnect) reconnectAttempt += 1
        val attempt = reconnectAttempt
        val scheduleGeneration = nextGeneration() ?: return
        publish(
            BleRuntimeState.Reconnecting(
                companion,
                attempt,
                maximumReconnectAttempts,
                connectionDiagnostic,
                periodic = periodicReconnect,
            ),
        )
        if (
            !accepts(scheduleGeneration) ||
            selected?.endpointToken != companion.endpointToken ||
            state !is BleRuntimeState.Reconnecting
        ) return
        val delay = if (periodicReconnect) PERIODIC_RECONNECT_DELAY_MILLIS else reconnectDelayMillis(attempt)
        reconnectLease = scheduler.schedule(delay) {
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
        if (periodicReconnect) {
            beginPeriodicDiscovery()
        } else {
            beginReturningOwnerConnection(companion, isReconnect = true)
        }
    }

    private fun reconnectDelayMillis(attempt: Int): Long = 1_000L shl (attempt - 1).coerceAtMost(3)

    private fun beginPeriodicDiscovery() {
        val preflight = facade.preflight()
        if (!preflight.isReady) {
            selected = null
            publish(BleRuntimeState.Blocked(preflight.blocker!!))
        } else if (!beginReturningOwnerDiscovery(periodicCycle = true)) {
            selected = null
            publish(BleRuntimeState.Failed(BleRuntimeFailure.SCAN_START_FAILED))
        }
    }

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
        if (stillWaiting) {
            val reconnecting = state as? BleRuntimeState.Reconnecting
            if (expectedPhase == null && reconnecting != null) {
                handleConnectionFailure(
                    reconnecting.companion,
                    BleRuntimeFailure.NEGOTIATION_TIMEOUT,
                    transient = true,
                )
            } else {
                failAndRelease(BleRuntimeFailure.NEGOTIATION_TIMEOUT)
            }
        }
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
            val pending = pendingAction
            val companion = when (val current = state) {
                is BleRuntimeState.Ready -> current.session.companion
                is BleRuntimeState.FactoryResetRequesting -> current.session.companion
                else -> null
            }
            if (pending?.request?.kind == CompanionActionKind.FACTORY_RESET && companion != null) {
                factoryResetNotVerified(
                    companion,
                    FactoryResetNotVerifiedReason.RESPONSE_TIMEOUT,
                    canRetryVerification = true,
                )
                return
            }
            if (companion == null) {
                failAndRelease(BleRuntimeFailure.ACTION_RESULT_TIMEOUT)
            } else {
                handleConnectionFailure(companion, BleRuntimeFailure.ACTION_RESULT_TIMEOUT, transient = true)
            }
        }
    }

    private fun armFactoryResetRestartTimeout(
        callbackGeneration: Long,
        target: FactoryResetVerificationTarget,
    ) {
        operationTimeoutLease?.close()
        operationTimeoutLease = scheduler.schedule(FACTORY_RESET_RESTART_TIMEOUT_MILLIS) {
            onFactoryResetRestartTimeout(callbackGeneration, target)
        }
    }

    @Synchronized
    private fun onFactoryResetRestartTimeout(
        callbackGeneration: Long,
        target: FactoryResetVerificationTarget,
    ) {
        requireOwnerThread()
        if (
            deliveringObserver ||
            !accepts(callbackGeneration) ||
            factoryResetVerificationTarget?.receipt != target.receipt ||
            state !is BleRuntimeState.FactoryResetErasing
        ) return
        beginFactoryResetVerification(target)
    }

    private fun isPhase(phase: BleNegotiationPhase): Boolean =
        (state as? BleRuntimeState.Negotiating)?.phase == phase

    private fun hasActiveGattPhase(phase: BleNegotiationPhase): Boolean =
        !closed && lifecycleActive && gattLease != null && isPhase(phase)

    private fun failAndRelease(reason: BleRuntimeFailure) {
        if (authorizationClaim != null) {
            finishAuthorizationTransportFailure(reason)
            return
        }
        selected = null
        invalidateAndRelease()
        authorizationClaim = null
        publish(BleRuntimeState.Failed(reason))
    }

    private fun finishAuthorizationTransportFailure(
        reason: BleRuntimeFailure,
        connectionDiagnostic: BleConnectionDiagnostic? = null,
    ) {
        val claim = authorizationClaim ?: return
        if (claim.terminal) {
            selected = null
            invalidateAndRelease()
            authorizationClaim = null
            publish(BleRuntimeState.Failed(reason, connectionDiagnostic))
            return
        }
        val event = when {
            claim.pendingReported -> DeviceAuthorizationClaimEvent.AuthorityUnknown(claim.publicOperationToken)
            reason == BleRuntimeFailure.AUTHORIZATION_UNSUPPORTED ->
                DeviceAuthorizationClaimEvent.Unsupported(claim.publicOperationToken)
            else -> DeviceAuthorizationClaimEvent.Unavailable(claim.publicOperationToken)
        }
        claim.terminal = true
        selected = null
        invalidateAndRelease()
        authorizationClaim = null
        publish(BleRuntimeState.Failed(reason, connectionDiagnostic))
        emitDetachedAuthorizationEvent(claim, event)
    }

    private fun emitDetachedAuthorizationEvent(
        claim: RuntimeAuthorizationClaim,
        event: DeviceAuthorizationClaimEvent,
    ) {
        if (deliveringAuthorizationObserver) return
        deliveringAuthorizationObserver = true
        try {
            claim.observer(event)
        } catch (_: Exception) {
            // Transport authority is already released; presentation failure cannot reacquire it.
        } finally {
            deliveringAuthorizationObserver = false
        }
    }

    private fun nextGeneration(): Long? {
        if (generation == Long.MAX_VALUE) {
            if (authorizationClaim != null) {
                finishAuthorizationTransportFailure(BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED)
                return null
            }
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
        textConversation.retire(textHandedOff); textHandedOff = false
        textTimeout?.close(); textTimeout = null; textExpiry?.close(); textExpiry = null
        packetEventCursor = 0u; textOperation = 0uL
        val origin=packetOrigin
        if(origin!=null && !origin.terminal) updatePacketPhase(
            if(origin.handedOff) BlePacketDeliveryPhase.OUTCOME_UNKNOWN else BlePacketDeliveryPhase.NOT_SENT)
        packetDelivery=packetDelivery.copy(available=false,
            currentRequest=packetDelivery.currentRequest?.copy(currentAttachment=false),
            receivedStatus=packetDelivery.receivedStatus?.copy(currentAttachment=false))
        if(packetOrigin?.handedOff==true && packetOrigin?.terminal!=true)
            packetRadioState=packetRadioState.copy(outcomeUnknown=true)
        packetTimeout?.close();packetTimeout=null
        quickStatusRecipient=null
        packetBinding=null;packetReceiver.disconnect();packetOrigin=null
        groupConfirmationSession?.endSession()
        groupConfirmationSession = null
        groupConfirmationTimeout?.close()
        groupConfirmationTimeout = null
        matchedSetupLabel = null
        displayTimeChanges?.close()
        displayTimeChanges=null
        automaticNameReadPending=false
        automaticTimeSyncPending=false
        configurationSession?.close()
        configurationSession = null
        configurationTimeout?.close()
        configurationTimeout = null
        configurationInfo = null
        normalProfileConfirmedAfterPromotion = false
        gattOpenedObserved = false
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
