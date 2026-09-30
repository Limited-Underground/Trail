package io.github.nbjelanovic.otclient

/** Optional variant-owned observation. Production has no recorder or storage implementation. */
interface ProtectedProtocolInfoDiagnosticObserver {
    fun observeProtectedProtocolInfo(diagnostic: ProtectedProtocolInfoDiagnostic)
}

enum class ProtectedProtocolInfoDiagnosticOrigin {
    READ_INITIATION, READ_CALLBACK_VALUE, READ_CALLBACK_LEGACY,
    CONNECTION_STATE_CALLBACK, SERVICE_CHANGED_CALLBACK, SERVICES_DISCOVERED_CALLBACK,
    MTU_CALLBACK, DESCRIPTOR_WRITE_CALLBACK, CHARACTERISTIC_WRITE_CALLBACK,
    STREAM_CALLBACK, LEASE_CLOSE_REQUESTED, LIMIT,
}

enum class ProtectedProtocolInfoDiagnosticOutcome {
    REQUESTED, STARTED, PRECONDITION_REJECTED, GATT_UNAVAILABLE, ORDER_REJECTED,
    TARGET_UNAVAILABLE, START_REJECTED, SECURITY_EXCEPTION, ARGUMENT_EXCEPTION,
    IGNORED_STALE, CALLBACK_OBSERVED, IGNORE, ACCEPT, PERMISSION_REVOKED,
    BOND_REQUIRED, SECURITY_REQUIRED, AUTHORIZATION_REJECTED, PLATFORM_FAILURE,
    CLOSE_REQUESTED, LIMIT_REACHED,
}

internal const val PROTECTED_READ_DIAGNOSTIC_MAX_EVENTS = 16
internal const val PROTECTED_READ_DIAGNOSTIC_MAX_MILLIS = 60_000L
internal const val PROTECTED_READ_DIAGNOSTIC_MAX_STATUS = 65_535
internal const val PROTECTED_READ_DIAGNOSTIC_MAX_BYTES = 512

/** Only bounded numbers, closed enums and ownership flags; never an identity or a value buffer. */
data class ProtectedProtocolInfoDiagnostic(
    val connection: Long,
    val request: Long,
    val origin: ProtectedProtocolInfoDiagnosticOrigin,
    val outcome: ProtectedProtocolInfoDiagnosticOutcome,
    val sinceMillis: Long?,
    val dispatchMillis: Long?,
    val handlingMillis: Long?,
    val status: Int?,
    val statusOutOfRange: Boolean,
    val valueBytes: Int?,
    val valueBytesOutOfRange: Boolean,
    val newState: Int?,
    val currentGatt: Boolean,
    val exactCharacteristic: Boolean?,
    val pendingRead: Boolean,
) {
    init {
        require(connection > 0 && request > 0)
        require(listOf(sinceMillis, dispatchMillis, handlingMillis).all {
            it == null || it in 0..PROTECTED_READ_DIAGNOSTIC_MAX_MILLIS
        })
        require(status == null || status in 0..PROTECTED_READ_DIAGNOSTIC_MAX_STATUS)
        require(valueBytes == null || valueBytes in 0..PROTECTED_READ_DIAGNOSTIC_MAX_BYTES)
        require(newState == null || newState in 0..3)
        require(!statusOutOfRange || status == null)
        require(!valueBytesOutOfRange || valueBytes == null)
    }
}

/** Main-thread observation only. It cannot complete, replace or authorize a GATT operation. */
internal class ProtectedProtocolInfoDiagnostics private constructor(
    private val connection: Long,
    private val observer: ProtectedProtocolInfoDiagnosticObserver?,
    private val nowMillis: () -> Long,
) {
    companion object {
        private var lastConnection = 0L

        /** Process-local counter also separates a recreated service from delayed old callbacks. */
        fun create(
            observer: ProtectedProtocolInfoDiagnosticObserver?,
            nowMillis: () -> Long,
        ): ProtectedProtocolInfoDiagnostics {
            if (observer == null) return ProtectedProtocolInfoDiagnostics(0, null, nowMillis)
            val connection = synchronized(this) {
                if (lastConnection == Long.MAX_VALUE) null else ++lastConnection
            }
            return ProtectedProtocolInfoDiagnostics(connection ?: 0, observer.takeIf { connection != null }, nowMillis)
        }
    }

    private var request = 0L
    private var started: Long? = null
    private var emitted = 0
    private var pending = false

    fun begin(currentGatt: Boolean, pendingRead: Boolean) {
        if (observer == null || request == Long.MAX_VALUE) return
        // A refused concurrent request must not replace the outstanding read's correlation.
        if (!pending) {
            request++
            started = nowMillis()
            emitted = 0
            pending = true
        }
        record(ProtectedProtocolInfoDiagnosticOrigin.READ_INITIATION,
            ProtectedProtocolInfoDiagnosticOutcome.REQUESTED, currentGatt, pendingRead)
    }

    fun finish() { pending = false }

    fun record(
        origin: ProtectedProtocolInfoDiagnosticOrigin,
        outcome: ProtectedProtocolInfoDiagnosticOutcome,
        currentGatt: Boolean,
        pendingRead: Boolean,
        status: Int? = null,
        valueBytes: Int? = null,
        newState: Int? = null,
        exactCharacteristic: Boolean? = null,
        callbackAt: Long? = null,
        handlingAt: Long? = null,
    ) {
        val sink = observer ?: return
        val start = started ?: return
        // Keep bounded late-read observations useful after close; unrelated callbacks stay out.
        if (!pending && origin != ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE &&
            origin != ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_LEGACY) return
        if (emitted >= PROTECTED_READ_DIAGNOSTIC_MAX_EVENTS) return
        val limited = emitted == PROTECTED_READ_DIAGNOSTIC_MAX_EVENTS - 1
        emitted++
        val now = nowMillis()
        val diagnostic = ProtectedProtocolInfoDiagnostic(
            connection, request,
            if (limited) ProtectedProtocolInfoDiagnosticOrigin.LIMIT else origin,
            if (limited) ProtectedProtocolInfoDiagnosticOutcome.LIMIT_REACHED else outcome,
            elapsed(start, now),
            callbackAt?.let { elapsed(it, handlingAt ?: now) },
            handlingAt?.let { elapsed(it, now) },
            status?.takeIf { it in 0..PROTECTED_READ_DIAGNOSTIC_MAX_STATUS },
            status != null && status !in 0..PROTECTED_READ_DIAGNOSTIC_MAX_STATUS,
            valueBytes?.takeIf { it in 0..PROTECTED_READ_DIAGNOSTIC_MAX_BYTES },
            valueBytes != null && valueBytes !in 0..PROTECTED_READ_DIAGNOSTIC_MAX_BYTES,
            newState?.takeIf { it in 0..3 }, currentGatt, exactCharacteristic, pendingRead,
        )
        // Diagnostics cannot interfere with cleanup, security admission or runtime delivery.
        runCatching { sink.observeProtectedProtocolInfo(diagnostic) }
    }

    private fun elapsed(from: Long, to: Long): Long? =
        if (from >= 0 && to >= from && to - from <= PROTECTED_READ_DIAGNOSTIC_MAX_MILLIS) to - from else null
}

internal fun AndroidProtectedReadAdmission.diagnosticOutcome(): ProtectedProtocolInfoDiagnosticOutcome = when (this) {
    AndroidProtectedReadAdmission.IGNORE -> ProtectedProtocolInfoDiagnosticOutcome.IGNORE
    AndroidProtectedReadAdmission.ACCEPT -> ProtectedProtocolInfoDiagnosticOutcome.ACCEPT
    AndroidProtectedReadAdmission.PERMISSION_REVOKED -> ProtectedProtocolInfoDiagnosticOutcome.PERMISSION_REVOKED
    AndroidProtectedReadAdmission.BOND_REQUIRED -> ProtectedProtocolInfoDiagnosticOutcome.BOND_REQUIRED
    AndroidProtectedReadAdmission.SECURITY_REQUIRED -> ProtectedProtocolInfoDiagnosticOutcome.SECURITY_REQUIRED
    AndroidProtectedReadAdmission.AUTHORIZATION_REJECTED -> ProtectedProtocolInfoDiagnosticOutcome.AUTHORIZATION_REJECTED
    AndroidProtectedReadAdmission.PLATFORM_FAILURE -> ProtectedProtocolInfoDiagnosticOutcome.PLATFORM_FAILURE
}
