package io.github.nbjelanovic.otclient

/**
 * Typed diagnostic stages for the protected BLE saved-owner flow.
 *
 * Every stage is emitted from an exact `BleRuntimeState` / `DeviceAuthorizationUiState` transition
 * that the runtime itself publishes. No stage is inferred from a general presentation label:
 * connected is not authorized, and authorized is not ready.
 *
 * Some listed stages share one runtime transition because the runtime publishes them together.
 * Those emissions carry the same elapsed value inside one transaction and are disclosed in the
 * export header; they are one observation reported at its full precision, not two observations.
 */
enum class V1TestTraceStage {
    CONNECTION_ATTEMPT_STARTED,
    RECONNECT_ATTEMPT_STARTED,
    RETURNING_OWNER_DISCOVERY_STARTED,
    GATT_LINK_ESTABLISHED,
    PROTECTED_PROTOCOL_INFO_REQUESTED,
    PROTECTED_PROTOCOL_INFO_ACCEPTED,
    PROTECTED_PROTOCOL_INFO_REJECTED,
    ATT_MTU_NEGOTIATED,
    STREAM_SUBSCRIPTION_ESTABLISHED,
    AUTHORIZATION_STARTED,
    AUTHORIZATION_PENDING,
    AUTHORIZATION_ACCEPTED,
    AUTHORIZATION_DENIED,
    AUTHORIZATION_UNAVAILABLE,
    AUTHORIZATION_UNCERTAIN,
    SNAPSHOT_REQUESTED,
    SNAPSHOT_ACCEPTED,
    SNAPSHOT_REJECTED,
    READY_REACHED,
    DISCONNECT_OBSERVED,
    TRANSACTION_FAILED,
}

/**
 * Categorical, non-secret failure reasons. Every value mirrors an existing typed runtime enum or a
 * local recorder classification. No free text, payload, address, token, key, or PIN is admitted.
 */
enum class V1TestTraceReason {
    NONE,

    // BleRuntimeFailure
    SCAN_START_FAILED,
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

    // BleRuntimeBlock
    ADAPTER_IMPLEMENTATION_MISSING,
    PLATFORM_UNSUPPORTED,
    BLUETOOTH_UNAVAILABLE,
    BLUETOOTH_DISABLED,
    SCAN_PERMISSION_MISSING,
    CONNECT_PERMISSION_MISSING,

    // BleConnectionDiagnostic
    LEASE_UNAVAILABLE,
    START_REJECTED,
    DISCONNECTED_BEFORE_PROFILE,
    GATT_TRANSIENT_LINK,
    GATT_PERMISSION_REVOKED,
    GATT_SECURITY_REJECTED,
    GATT_BOND_REQUIRED,
    GATT_AUTHORIZATION_REJECTED,
    GATT_PLATFORM_FAILURE,

    // DeviceAuthorizationUiState terminals
    AUTHORIZATION_DENIED,
    AUTHORIZATION_EXPIRED,
    AUTHORIZATION_INVALID_RESULT,
    AUTHORIZATION_AUTHORITY_UNKNOWN,

    // Local recorder classifications
    LINK_CLOSED,
    SERVICE_GENERATION_CHANGED,
    SERVICE_BINDING_RELEASED,
}

/** Reasons the recorder stops trusting its own input. Every value latches the machine closed. */
enum class V1TestTraceFailure { ELAPSED_ROLLBACK, ORDINAL_EXHAUSTED, TRANSACTION_EXHAUSTED }

/**
 * One recorded milestone.
 *
 * [transaction] is a session-local correlation number; `0` means the milestone was observed outside
 * an open connection transaction. [ordinal] is a session-local monotonic operation number and is
 * never a device, wire, or security identifier. [generation] is the bound connected-device service
 * generation already exposed by [ConnectedDeviceSessionPort]; it is not the private BLE transport
 * generation. [sinceMillis] is the elapsed distance from the previous milestone of the same
 * transaction, and `0` for the first. [attempt] is the reconnect attempt number, or `0`.
 */
data class V1TestTraceEmission(
    val stage: V1TestTraceStage,
    val transaction: Long,
    val ordinal: Long,
    val generation: Long,
    val sinceMillis: Long,
    val attempt: Int,
    val reason: V1TestTraceReason,
    /** Exact published lower-level fact; null means unavailable, including older log records. */
    val connectionDiagnostic: BleConnectionDiagnostic? = null,
) {
    init {
        require(transaction >= 0 && ordinal >= 1 && generation >= 0)
        require(sinceMillis >= 0 && attempt >= 0)
    }
}

/**
 * Pure, single-owner derivation of diagnostic milestones from the typed states the connected-device
 * service already publishes. It performs no I/O, holds no Android reference, and cannot start,
 * stop, or steal any production observer.
 *
 * The caller must serialize calls and supply session-relative monotonic elapsed milliseconds.
 */
class V1TestConnectionTraceMachine {
    /** Latched input-trust failure. Once set the machine emits nothing until [reset]. */
    var failure: V1TestTraceFailure? = null
        private set

    /** Observations refused because they carried an older service generation. */
    var staleObservations: Long = 0L
        private set

    private var generation = -1L
    private var lastElapsed = 0L
    private var ordinal = 0L
    private var transaction = 0L
    private var openTransaction = 0L
    private var lastStageElapsed = 0L
    private val emitted = mutableSetOf<V1TestTraceStage>()
    private var previous: Observed? = null
    private var previousAuthorization: DeviceAuthorizationUiState? = null

    /** Observation of one published connected-device state. */
    fun observe(
        generation: Long,
        elapsedMillis: Long,
        state: TrailAppUiState.BluetoothDevice,
    ): List<V1TestTraceEmission> {
        if (failure != null || generation < 0) return emptyList()
        if (generation < this.generation) {
            staleObservations += 1
            return emptyList()
        }
        if (elapsedMillis < 0 || elapsedMillis < lastElapsed) return fail(V1TestTraceFailure.ELAPSED_ROLLBACK)
        lastElapsed = elapsedMillis
        val emissions = mutableListOf<V1TestTraceEmission>()
        if (generation > this.generation) {
            if (this.generation >= 0) {
                closeOpenTransaction(elapsedMillis, V1TestTraceReason.SERVICE_GENERATION_CHANGED, emissions)
            }
            this.generation = generation
            previous = null
            previousAuthorization = null
        }
        val authorization = state.authorizationState
        if (authorization != previousAuthorization) {
            previousAuthorization = authorization
            authorizationStage(authorization)?.let { (stage, reason) ->
                emit(stage, elapsedMillis, reason, 0, emissions)
            }
        }
        val observed = classify(state.runtimeState)
        if (observed != previous) {
            val before = previous
            previous = observed
            applyRuntimeTransition(before, observed, elapsedMillis, emissions)
        }
        return if (failure != null) emptyList() else emissions
    }

    /** Only the exact observed owner may terminate its transaction. */
    fun observeServiceDisconnected(generation: Long, elapsedMillis: Long): List<V1TestTraceEmission> {
        if (generation != this.generation) {
            if (generation < this.generation) staleObservations += 1
            return emptyList()
        }
        return observeServiceDisconnected(elapsedMillis)
    }

    /** Historical binding termination; the service recorder uses the exact-generation overload. */
    fun observeServiceDisconnected(elapsedMillis: Long): List<V1TestTraceEmission> {
        if (failure != null) return emptyList()
        if (elapsedMillis < 0 || elapsedMillis < lastElapsed) return fail(V1TestTraceFailure.ELAPSED_ROLLBACK)
        lastElapsed = elapsedMillis
        val emissions = mutableListOf<V1TestTraceEmission>()
        closeOpenTransaction(elapsedMillis, V1TestTraceReason.SERVICE_BINDING_RELEASED, emissions)
        previous = null
        previousAuthorization = null
        return if (failure != null) emptyList() else emissions
    }

    fun reset() {
        failure = null
        staleObservations = 0
        generation = -1
        lastElapsed = 0
        ordinal = 0
        transaction = 0
        openTransaction = 0
        lastStageElapsed = 0
        emitted.clear()
        previous = null
        previousAuthorization = null
    }

    private fun applyRuntimeTransition(
        before: Observed?,
        observed: Observed,
        elapsed: Long,
        emissions: MutableList<V1TestTraceEmission>,
    ) {
        when (observed) {
            is Observed.Connecting -> {
                closeOpenTransaction(elapsed, V1TestTraceReason.LINK_CLOSED, emissions)
                if (!openTransaction(elapsed)) return
                emit(V1TestTraceStage.CONNECTION_ATTEMPT_STARTED, elapsed, V1TestTraceReason.NONE, 0, emissions)
            }
            is Observed.Reconnecting -> {
                closeOpenTransaction(elapsed, observed.reason, emissions, observed.connectionDiagnostic)
                if (!openTransaction(elapsed)) return
                emit(
                    V1TestTraceStage.RECONNECT_ATTEMPT_STARTED,
                    elapsed,
                    V1TestTraceReason.NONE,
                    observed.attempt,
                    emissions,
                )
            }
            is Observed.Discovery -> {
                closeOpenTransaction(elapsed, V1TestTraceReason.LINK_CLOSED, emissions)
                emit(
                    V1TestTraceStage.RETURNING_OWNER_DISCOVERY_STARTED,
                    elapsed,
                    V1TestTraceReason.NONE,
                    0,
                    emissions,
                )
            }
            is Observed.Negotiating -> negotiating(before, observed, elapsed, emissions)
            is Observed.Ready -> {
                if (openTransaction == 0L) return
                emit(V1TestTraceStage.SNAPSHOT_ACCEPTED, elapsed, V1TestTraceReason.NONE, 0, emissions)
                emit(V1TestTraceStage.READY_REACHED, elapsed, V1TestTraceReason.NONE, 0, emissions)
            }
            is Observed.Terminated -> {
                emit(terminalStage(before), elapsed, observed.reason, 0, emissions, observed.connectionDiagnostic)
                closeTransaction()
            }
            is Observed.Quiescent -> closeOpenTransaction(elapsed, V1TestTraceReason.LINK_CLOSED, emissions)
            is Observed.Unrelated -> Unit
        }
    }

    private fun negotiating(
        before: Observed?,
        observed: Observed.Negotiating,
        elapsed: Long,
        emissions: MutableList<V1TestTraceEmission>,
    ) {
        if (openTransaction == 0L) return
        val from = (before as? Observed.Negotiating)?.phase
        when (observed.phase) {
            BleNegotiationPhase.LINK_SECURITY -> Unit
            BleNegotiationPhase.PROTOCOL_INFO -> {
                // Publishing this phase proves the link opened and the exact GATT profile was
                // accepted; the protected read is issued in the same runtime step.
                emit(V1TestTraceStage.GATT_LINK_ESTABLISHED, elapsed, V1TestTraceReason.NONE, 0, emissions)
                emit(
                    V1TestTraceStage.PROTECTED_PROTOCOL_INFO_REQUESTED,
                    elapsed,
                    V1TestTraceReason.NONE,
                    0,
                    emissions,
                )
            }
            BleNegotiationPhase.ATT_MTU ->
                if (from == BleNegotiationPhase.PROTOCOL_INFO) {
                    emit(
                        V1TestTraceStage.PROTECTED_PROTOCOL_INFO_ACCEPTED,
                        elapsed,
                        V1TestTraceReason.NONE,
                        0,
                        emissions,
                    )
                }
            BleNegotiationPhase.STREAM_SUBSCRIPTION -> {
                if (from == BleNegotiationPhase.PROTOCOL_INFO) {
                    emit(
                        V1TestTraceStage.PROTECTED_PROTOCOL_INFO_ACCEPTED,
                        elapsed,
                        V1TestTraceReason.NONE,
                        0,
                        emissions,
                    )
                }
                if (from == BleNegotiationPhase.ATT_MTU) {
                    emit(V1TestTraceStage.ATT_MTU_NEGOTIATED, elapsed, V1TestTraceReason.NONE, 0, emissions)
                }
            }
            BleNegotiationPhase.AUTHORIZATION_CLAIM -> {
                if (from == BleNegotiationPhase.STREAM_SUBSCRIPTION) {
                    emit(
                        V1TestTraceStage.STREAM_SUBSCRIPTION_ESTABLISHED,
                        elapsed,
                        V1TestTraceReason.NONE,
                        0,
                        emissions,
                    )
                }
                emit(V1TestTraceStage.AUTHORIZATION_STARTED, elapsed, V1TestTraceReason.NONE, 0, emissions)
            }
            BleNegotiationPhase.INITIAL_SNAPSHOT -> {
                if (from == BleNegotiationPhase.STREAM_SUBSCRIPTION) {
                    emit(
                        V1TestTraceStage.STREAM_SUBSCRIPTION_ESTABLISHED,
                        elapsed,
                        V1TestTraceReason.NONE,
                        0,
                        emissions,
                    )
                }
                if (from == BleNegotiationPhase.AUTHORIZATION_CLAIM) {
                    // The runtime reaches this phase only after the exact terminal Accepted
                    // indication is confirmed and the claim is promoted.
                    emit(V1TestTraceStage.AUTHORIZATION_ACCEPTED, elapsed, V1TestTraceReason.NONE, 0, emissions)
                }
                emit(V1TestTraceStage.SNAPSHOT_REQUESTED, elapsed, V1TestTraceReason.NONE, 0, emissions)
            }
        }
    }

    private fun terminalStage(before: Observed?): V1TestTraceStage {
        val phase = (before as? Observed.Negotiating)?.phase ?: return V1TestTraceStage.TRANSACTION_FAILED
        return when (phase) {
            BleNegotiationPhase.PROTOCOL_INFO -> V1TestTraceStage.PROTECTED_PROTOCOL_INFO_REJECTED
            BleNegotiationPhase.AUTHORIZATION_CLAIM -> V1TestTraceStage.AUTHORIZATION_UNCERTAIN
            BleNegotiationPhase.INITIAL_SNAPSHOT -> V1TestTraceStage.SNAPSHOT_REJECTED
            else -> V1TestTraceStage.TRANSACTION_FAILED
        }
    }

    private fun authorizationStage(
        state: DeviceAuthorizationUiState,
    ): Pair<V1TestTraceStage, V1TestTraceReason>? = when (state) {
        is DeviceAuthorizationUiState.None -> null
        is DeviceAuthorizationUiState.Starting ->
            V1TestTraceStage.AUTHORIZATION_STARTED to V1TestTraceReason.NONE
        is DeviceAuthorizationUiState.Pending ->
            V1TestTraceStage.AUTHORIZATION_PENDING to V1TestTraceReason.NONE
        is DeviceAuthorizationUiState.Accepted ->
            V1TestTraceStage.AUTHORIZATION_ACCEPTED to V1TestTraceReason.NONE
        is DeviceAuthorizationUiState.Denied ->
            V1TestTraceStage.AUTHORIZATION_DENIED to V1TestTraceReason.AUTHORIZATION_DENIED
        is DeviceAuthorizationUiState.Unavailable ->
            V1TestTraceStage.AUTHORIZATION_UNAVAILABLE to V1TestTraceReason.AUTHORIZATION_UNAVAILABLE
        is DeviceAuthorizationUiState.Unsupported ->
            V1TestTraceStage.AUTHORIZATION_UNAVAILABLE to V1TestTraceReason.AUTHORIZATION_UNSUPPORTED
        is DeviceAuthorizationUiState.Expired ->
            V1TestTraceStage.AUTHORIZATION_UNCERTAIN to V1TestTraceReason.AUTHORIZATION_EXPIRED
        is DeviceAuthorizationUiState.InvalidResult ->
            V1TestTraceStage.AUTHORIZATION_UNCERTAIN to V1TestTraceReason.AUTHORIZATION_INVALID_RESULT
        is DeviceAuthorizationUiState.AuthorityUnknown ->
            V1TestTraceStage.AUTHORIZATION_UNCERTAIN to V1TestTraceReason.AUTHORIZATION_AUTHORITY_UNKNOWN
    }

    private fun openTransaction(elapsed: Long): Boolean {
        if (transaction == Long.MAX_VALUE) {
            fail(V1TestTraceFailure.TRANSACTION_EXHAUSTED)
            return false
        }
        transaction += 1
        openTransaction = transaction
        lastStageElapsed = elapsed
        emitted.clear()
        return true
    }

    private fun closeOpenTransaction(
        elapsed: Long,
        reason: V1TestTraceReason,
        emissions: MutableList<V1TestTraceEmission>,
        connectionDiagnostic: BleConnectionDiagnostic? = null,
    ) {
        if (openTransaction == 0L) return
        emit(V1TestTraceStage.DISCONNECT_OBSERVED, elapsed, reason, 0, emissions, connectionDiagnostic)
        closeTransaction()
    }

    private fun closeTransaction() {
        openTransaction = 0
        emitted.clear()
    }

    private fun emit(
        stage: V1TestTraceStage,
        elapsed: Long,
        reason: V1TestTraceReason,
        attempt: Int,
        emissions: MutableList<V1TestTraceEmission>,
        connectionDiagnostic: BleConnectionDiagnostic? = null,
    ) {
        if (failure != null) return
        if (openTransaction != 0L && !emitted.add(stage)) return
        if (ordinal == Long.MAX_VALUE) {
            fail(V1TestTraceFailure.ORDINAL_EXHAUSTED)
            return
        }
        ordinal += 1
        val since = if (openTransaction == 0L) 0L else (elapsed - lastStageElapsed).coerceAtLeast(0)
        emissions += V1TestTraceEmission(
            stage = stage,
            transaction = openTransaction,
            ordinal = ordinal,
            generation = generation.coerceAtLeast(0),
            sinceMillis = since,
            attempt = attempt,
            reason = reason,
            connectionDiagnostic = connectionDiagnostic,
        )
        if (openTransaction != 0L) lastStageElapsed = elapsed
    }

    private fun fail(value: V1TestTraceFailure): List<V1TestTraceEmission> {
        failure = value
        openTransaction = 0
        emitted.clear()
        previous = null
        previousAuthorization = null
        return emptyList()
    }

    private fun classify(state: BleRuntimeState): Observed = when (state) {
        is BleRuntimeState.Connecting -> Observed.Connecting
        is BleRuntimeState.Reconnecting -> Observed.Reconnecting(
            state.attempt,
            state.periodic,
            state.connectionDiagnostic?.traceReason() ?: V1TestTraceReason.LINK_CLOSED,
            state.connectionDiagnostic,
        )
        is BleRuntimeState.FindingReturningOwner -> Observed.Discovery
        is BleRuntimeState.Negotiating -> Observed.Negotiating(state.phase)
        is BleRuntimeState.Ready -> Observed.Ready
        is BleRuntimeState.Failed -> Observed.Terminated(state.reason.traceReason(), state.connectionDiagnostic)
        is BleRuntimeState.Blocked -> Observed.Terminated(state.reason.traceReason())
        // Factory-reset states keep the live session; they are not a disconnect and this increment
        // records no reset diagnostics.
        is BleRuntimeState.FactoryResetRequesting,
        is BleRuntimeState.FactoryResetErasing,
        is BleRuntimeState.FactoryResetVerifying,
        is BleRuntimeState.FactoryResetNotVerified,
        is BleRuntimeState.FactoryResetComplete,
        -> Observed.Unrelated
        else -> Observed.Quiescent
    }

    private sealed interface Observed {
        data object Connecting : Observed
        data class Reconnecting(
            val attempt: Int,
            val periodic: Boolean,
            val reason: V1TestTraceReason,
            val connectionDiagnostic: BleConnectionDiagnostic?,
        ) : Observed
        data object Discovery : Observed
        data class Negotiating(val phase: BleNegotiationPhase) : Observed
        data object Ready : Observed
        data class Terminated(
            val reason: V1TestTraceReason,
            val connectionDiagnostic: BleConnectionDiagnostic? = null,
        ) : Observed
        data object Quiescent : Observed
        data object Unrelated : Observed
    }
}

private fun BleRuntimeFailure.traceReason(): V1TestTraceReason = when (this) {
    BleRuntimeFailure.SCAN_START_FAILED -> V1TestTraceReason.SCAN_START_FAILED
    BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS -> V1TestTraceReason.RETURNING_OWNER_AMBIGUOUS
    BleRuntimeFailure.CONNECTION_START_FAILED -> V1TestTraceReason.CONNECTION_START_FAILED
    BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED -> V1TestTraceReason.SECURITY_REQUIREMENT_FAILED
    BleRuntimeFailure.MTU_NEGOTIATION_FAILED -> V1TestTraceReason.MTU_NEGOTIATION_FAILED
    BleRuntimeFailure.PROTOCOL_INFO_FAILED -> V1TestTraceReason.PROTOCOL_INFO_FAILED
    BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED -> V1TestTraceReason.STREAM_SUBSCRIPTION_FAILED
    BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED -> V1TestTraceReason.INITIAL_SNAPSHOT_FAILED
    BleRuntimeFailure.NEGOTIATION_TIMEOUT -> V1TestTraceReason.NEGOTIATION_TIMEOUT
    BleRuntimeFailure.PROTOCOL_VIOLATION -> V1TestTraceReason.PROTOCOL_VIOLATION
    BleRuntimeFailure.RECONNECT_EXHAUSTED -> V1TestTraceReason.RECONNECT_EXHAUSTED
    BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED -> V1TestTraceReason.SESSION_COUNTER_EXHAUSTED
    BleRuntimeFailure.ACTION_WRITE_FAILED -> V1TestTraceReason.ACTION_WRITE_FAILED
    BleRuntimeFailure.ACTION_RESULT_TIMEOUT -> V1TestTraceReason.ACTION_RESULT_TIMEOUT
    BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST -> V1TestTraceReason.AUTHORIZATION_CONNECTION_LOST
    BleRuntimeFailure.AUTHORIZATION_UNSUPPORTED -> V1TestTraceReason.AUTHORIZATION_UNSUPPORTED
    BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE -> V1TestTraceReason.AUTHORIZATION_UNAVAILABLE
}

private fun BleRuntimeBlock.traceReason(): V1TestTraceReason = when (this) {
    BleRuntimeBlock.ADAPTER_IMPLEMENTATION_MISSING -> V1TestTraceReason.ADAPTER_IMPLEMENTATION_MISSING
    BleRuntimeBlock.PLATFORM_UNSUPPORTED -> V1TestTraceReason.PLATFORM_UNSUPPORTED
    BleRuntimeBlock.BLUETOOTH_UNAVAILABLE -> V1TestTraceReason.BLUETOOTH_UNAVAILABLE
    BleRuntimeBlock.BLUETOOTH_DISABLED -> V1TestTraceReason.BLUETOOTH_DISABLED
    BleRuntimeBlock.SCAN_PERMISSION_MISSING -> V1TestTraceReason.SCAN_PERMISSION_MISSING
    BleRuntimeBlock.CONNECT_PERMISSION_MISSING -> V1TestTraceReason.CONNECT_PERMISSION_MISSING
}

private fun BleConnectionDiagnostic.traceReason(): V1TestTraceReason = when (this) {
    BleConnectionDiagnostic.LEASE_UNAVAILABLE -> V1TestTraceReason.LEASE_UNAVAILABLE
    BleConnectionDiagnostic.START_REJECTED -> V1TestTraceReason.START_REJECTED
    BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE -> V1TestTraceReason.DISCONNECTED_BEFORE_PROFILE
    BleConnectionDiagnostic.GATT_TRANSIENT_LINK -> V1TestTraceReason.GATT_TRANSIENT_LINK
    BleConnectionDiagnostic.GATT_PERMISSION_REVOKED -> V1TestTraceReason.GATT_PERMISSION_REVOKED
    BleConnectionDiagnostic.GATT_SECURITY_REJECTED -> V1TestTraceReason.GATT_SECURITY_REJECTED
    BleConnectionDiagnostic.GATT_BOND_REQUIRED -> V1TestTraceReason.GATT_BOND_REQUIRED
    BleConnectionDiagnostic.GATT_AUTHORIZATION_REJECTED -> V1TestTraceReason.GATT_AUTHORIZATION_REJECTED
    BleConnectionDiagnostic.GATT_PLATFORM_FAILURE -> V1TestTraceReason.GATT_PLATFORM_FAILURE
}
