package io.github.nbjelanovic.otprotocol

const val COMPANION_AUTHORIZATION_CORRELATION_BYTES = 16
const val COMPANION_AUTHORIZATION_CLAIM_START_BYTES = 8
const val COMPANION_AUTHORIZATION_CLAIM_STATUS_BYTES = 24
const val COMPANION_AUTHORIZATION_CLAIM_RESULT_BYTES = 28

enum class CompanionAuthorizationPurpose(val wireValue: Int) {
    AUTHORIZE_CONTROLLER(1),
    REPLACE_CONTROLLER(2);

    companion object {
        fun fromWire(value: Int): CompanionAuthorizationPurpose? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class CompanionAuthorizationClaimState(val wireValue: Int) {
    PENDING(1);

    companion object {
        fun fromWire(value: Int): CompanionAuthorizationClaimState? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class CompanionAuthorizationClaimOutcome(val wireValue: Int) {
    ACCEPTED(1),
    DENIED(2),
    REPLACED(3);

    companion object {
        fun fromWire(value: Int): CompanionAuthorizationClaimOutcome? = entries.firstOrNull { it.wireValue == value }
    }
}

/** UNKNOWN is an explicit device denial with unavailable detail; it is never a local timeout/unknown state. */
enum class CompanionAuthorizationDenyReason(val wireValue: Int) {
    NONE(0),
    UNKNOWN(1),
    UNSUPPORTED(2),
    PHYSICAL_PRESENCE_REQUIRED(3),
    PHYSICAL_PRESENCE_EXPIRED(4),
    OWNER_STATE_CONFLICT(5),
    POLICY_DENIED(6),
    PERSISTENCE_UNAVAILABLE(7),
    INTERNAL_FAILURE(8);

    companion object {
        fun fromWire(value: Int): CompanionAuthorizationDenyReason? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class CompanionAuthorizationWireError {
    NONE,
    INVALID_ARGUMENT,
    OUTPUT_TOO_SMALL,
    MALFORMED,
    UNSUPPORTED_VERSION,
    UNKNOWN_PURPOSE,
    UNKNOWN_STATE,
    UNKNOWN_OUTCOME,
    UNKNOWN_REASON,
    INVALID_CORRELATION,
    INCOHERENT_RESULT,
    RESERVED_BITS_SET,
    UNSUPPORTED_FRAME_KIND,
    WRONG_DIRECTION,
    WRONG_SESSION,
    WRONG_EXCHANGE,
    PURPOSE_MISMATCH,
    CORRELATION_MISMATCH,
    CLAIM_IN_PROGRESS,
    NO_CLAIM_IN_PROGRESS,
    RESPONSE_OUT_OF_ORDER,
    DUPLICATE_RESPONSE,
    STALE_START,
    INVALID_TRANSPORT_GENERATION,
    LINK_NOT_ENCRYPTED,
    BOND_NOT_AUTHENTICATED,
    CLAIM_CAPABILITY_NOT_NEGOTIATED,
    NO_PROVISIONAL_SESSION,
    WRONG_TRANSPORT_GENERATION,
}

data class CompanionAuthorizationCodecResult<T>(
    val value: T? = null,
    val error: CompanionAuthorizationWireError = CompanionAuthorizationWireError.NONE,
) {
    val isSuccess: Boolean get() = error == CompanionAuthorizationWireError.NONE && value != null

    companion object {
        fun <T> success(value: T) = CompanionAuthorizationCodecResult(value)
        fun <T> failure(error: CompanionAuthorizationWireError) = CompanionAuthorizationCodecResult<T>(error = error)
    }
}

/**
 * Device-issued, boot-local opaque correlation. It is not identity, an address, a credential, or
 * a secret. The value is copied, redacted from string output, and must never be displayed/logged/persisted.
 */
class CompanionAuthorizationCorrelation(bytes: ByteArray) {
    private val value = if (bytes.size == COMPANION_AUTHORIZATION_CORRELATION_BYTES) {
        bytes.copyOf()
    } else {
        byteArrayOf()
    }

    internal fun isValid(): Boolean = value.size == COMPANION_AUTHORIZATION_CORRELATION_BYTES && value.any { it != 0.toByte() }
    internal fun copyBytes(): ByteArray = value.copyOf()

    override fun equals(other: Any?): Boolean =
        other is CompanionAuthorizationCorrelation && value.contentEquals(other.value)

    override fun hashCode(): Int = value.contentHashCode()
    override fun toString(): String = "CompanionAuthorizationCorrelation(redacted)"
}

data class CompanionAuthorizationClaimStart(val purpose: CompanionAuthorizationPurpose)

data class CompanionAuthorizationClaimStatus(
    val purpose: CompanionAuthorizationPurpose,
    val state: CompanionAuthorizationClaimState,
    val correlation: CompanionAuthorizationCorrelation,
)

data class CompanionAuthorizationClaimResult(
    val purpose: CompanionAuthorizationPurpose,
    val outcome: CompanionAuthorizationClaimOutcome,
    val reason: CompanionAuthorizationDenyReason,
    val correlation: CompanionAuthorizationCorrelation,
)

object CompanionAuthorizationWireCodec {
    private val startMagic = byteArrayOf(0x4f, 0x54, 0x4c, 0x30)
    private val statusMagic = byteArrayOf(0x4f, 0x54, 0x50, 0x30)
    private val resultMagic = byteArrayOf(0x4f, 0x54, 0x46, 0x30)

    fun encodeClaimStart(start: CompanionAuthorizationClaimStart): ByteArray =
        byteArrayOf(0x4f, 0x54, 0x4c, 0x30, 0, 0, start.purpose.wireValue.toByte(), 0)

    fun decodeClaimStart(encoded: ByteArray): CompanionAuthorizationCodecResult<CompanionAuthorizationClaimStart> {
        validatePrefix(encoded, startMagic, COMPANION_AUTHORIZATION_CLAIM_START_BYTES)?.let {
            return CompanionAuthorizationCodecResult.failure(it)
        }
        if (encoded.u8(7) != 0) return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.RESERVED_BITS_SET)
        val purpose = CompanionAuthorizationPurpose.fromWire(encoded.u8(6))
            ?: return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.UNKNOWN_PURPOSE)
        return CompanionAuthorizationCodecResult.success(CompanionAuthorizationClaimStart(purpose))
    }

    fun encodeClaimStatus(status: CompanionAuthorizationClaimStatus): CompanionAuthorizationCodecResult<ByteArray> {
        if (!status.correlation.isValid()) {
            return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.INVALID_CORRELATION)
        }
        val output = ByteArray(COMPANION_AUTHORIZATION_CLAIM_STATUS_BYTES)
        statusMagic.copyInto(output)
        output[6] = status.purpose.wireValue.toByte()
        output[7] = status.state.wireValue.toByte()
        status.correlation.copyBytes().copyInto(output, 8)
        return CompanionAuthorizationCodecResult.success(output)
    }

    fun decodeClaimStatus(encoded: ByteArray): CompanionAuthorizationCodecResult<CompanionAuthorizationClaimStatus> {
        validatePrefix(encoded, statusMagic, COMPANION_AUTHORIZATION_CLAIM_STATUS_BYTES)?.let {
            return CompanionAuthorizationCodecResult.failure(it)
        }
        val purpose = CompanionAuthorizationPurpose.fromWire(encoded.u8(6))
            ?: return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.UNKNOWN_PURPOSE)
        val state = CompanionAuthorizationClaimState.fromWire(encoded.u8(7))
            ?: return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.UNKNOWN_STATE)
        val correlation = CompanionAuthorizationCorrelation(encoded.copyOfRange(8, 24))
        if (!correlation.isValid()) {
            return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.INVALID_CORRELATION)
        }
        return CompanionAuthorizationCodecResult.success(CompanionAuthorizationClaimStatus(purpose, state, correlation))
    }

    fun encodeClaimResult(result: CompanionAuthorizationClaimResult): CompanionAuthorizationCodecResult<ByteArray> {
        validateResult(result)?.let { return CompanionAuthorizationCodecResult.failure(it) }
        val output = ByteArray(COMPANION_AUTHORIZATION_CLAIM_RESULT_BYTES)
        resultMagic.copyInto(output)
        output[6] = result.purpose.wireValue.toByte()
        output[7] = result.outcome.wireValue.toByte()
        output[8] = result.reason.wireValue.toByte()
        result.correlation.copyBytes().copyInto(output, 12)
        return CompanionAuthorizationCodecResult.success(output)
    }

    fun decodeClaimResult(encoded: ByteArray): CompanionAuthorizationCodecResult<CompanionAuthorizationClaimResult> {
        validatePrefix(encoded, resultMagic, COMPANION_AUTHORIZATION_CLAIM_RESULT_BYTES)?.let {
            return CompanionAuthorizationCodecResult.failure(it)
        }
        if ((9..11).any { encoded.u8(it) != 0 }) {
            return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.RESERVED_BITS_SET)
        }
        val purpose = CompanionAuthorizationPurpose.fromWire(encoded.u8(6))
            ?: return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.UNKNOWN_PURPOSE)
        val outcome = CompanionAuthorizationClaimOutcome.fromWire(encoded.u8(7))
            ?: return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.UNKNOWN_OUTCOME)
        val reason = CompanionAuthorizationDenyReason.fromWire(encoded.u8(8))
            ?: return CompanionAuthorizationCodecResult.failure(CompanionAuthorizationWireError.UNKNOWN_REASON)
        val result = CompanionAuthorizationClaimResult(
            purpose,
            outcome,
            reason,
            CompanionAuthorizationCorrelation(encoded.copyOfRange(12, 28)),
        )
        validateResult(result)?.let { return CompanionAuthorizationCodecResult.failure(it) }
        return CompanionAuthorizationCodecResult.success(result)
    }

    fun validateAuthorizationFragment(fragment: CompanionFragment): CompanionAuthorizationWireError {
        if (fragment.fragmentIndex != 0 || fragment.fragmentCount != 1 || fragment.payload.size > COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) {
            return CompanionAuthorizationWireError.MALFORMED
        }
        return when (fragment.kind) {
            CompanionFrameKind.AUTHORIZATION_CLAIM_START -> decodeClaimStart(fragment.payload).error
            CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS -> decodeClaimStatus(fragment.payload).error
            CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT -> decodeClaimResult(fragment.payload).error
            else -> CompanionAuthorizationWireError.UNSUPPORTED_FRAME_KIND
        }
    }

    private fun validatePrefix(
        encoded: ByteArray,
        magic: ByteArray,
        expectedBytes: Int,
    ): CompanionAuthorizationWireError? {
        if (encoded.size != expectedBytes || !encoded.hasAuthorizationMagic(magic)) {
            return CompanionAuthorizationWireError.MALFORMED
        }
        if (encoded.u8(4) != 0 || encoded.u8(5) != 0) return CompanionAuthorizationWireError.UNSUPPORTED_VERSION
        return null
    }

    private fun validateResult(result: CompanionAuthorizationClaimResult): CompanionAuthorizationWireError? {
        if (!result.correlation.isValid()) return CompanionAuthorizationWireError.INVALID_CORRELATION
        if (result.outcome == CompanionAuthorizationClaimOutcome.DENIED) {
            return if (result.reason == CompanionAuthorizationDenyReason.NONE) {
                CompanionAuthorizationWireError.INCOHERENT_RESULT
            } else null
        }
        if (result.reason != CompanionAuthorizationDenyReason.NONE) return CompanionAuthorizationWireError.INCOHERENT_RESULT
        if (
            result.outcome == CompanionAuthorizationClaimOutcome.ACCEPTED &&
            result.purpose != CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
        ) return CompanionAuthorizationWireError.INCOHERENT_RESULT
        if (
            result.outcome == CompanionAuthorizationClaimOutcome.REPLACED &&
            result.purpose != CompanionAuthorizationPurpose.REPLACE_CONTROLLER
        ) return CompanionAuthorizationWireError.INCOHERENT_RESULT
        return null
    }
}

enum class CompanionAuthorizationResponsePhase { IDLE, AWAITING_PENDING, PENDING, TERMINAL }

data class CompanionAuthorizationResponseObservation(
    val error: CompanionAuthorizationWireError,
    val phase: CompanionAuthorizationResponsePhase,
    val outcome: CompanionAuthorizationClaimOutcome = CompanionAuthorizationClaimOutcome.DENIED,
    val reason: CompanionAuthorizationDenyReason = CompanionAuthorizationDenyReason.UNKNOWN,
) {
    val accepted: Boolean get() = error == CompanionAuthorizationWireError.NONE
    val terminal: Boolean get() = accepted && phase == CompanionAuthorizationResponsePhase.TERMINAL
}

data class CompanionAuthorizationResponseStatus(
    val phase: CompanionAuthorizationResponsePhase,
    val purpose: CompanionAuthorizationPurpose,
    val correlationPresent: Boolean,
    val terminalReceived: Boolean,
    val provisionalSessionOpen: Boolean,
    val applicationAuthorized: Boolean,
)

data class CompanionAuthorizationProvisionalEvidence(
    val transportGeneration: ULong,
    val linkEncrypted: Boolean,
    val authenticatedBond: Boolean,
    val claimWireSupported: Boolean,
)

/**
 * Pure, fixed-state response tracker. It performs no transport I/O and grants no device authority.
 * One external owner must serialize every call; this mutable class is intentionally not thread-safe.
 */
class CompanionAuthorizationResponseTracker {
    private var phase = CompanionAuthorizationResponsePhase.IDLE
    private var purpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
    private var sessionNonce = 0L
    private var exchangeId = 0L
    private var lastExchangeId = 0L
    private var transportGeneration = 0uL
    private var lastTransportGeneration = 0uL
    private var correlation: CompanionAuthorizationCorrelation? = null
    private var provisionalSessionOpen = false
    private var applicationAuthorized = false

    fun openProvisionalSession(
        evidence: CompanionAuthorizationProvisionalEvidence,
        deviceGeneratedSessionNonce: Long,
    ): CompanionAuthorizationWireError {
        if (evidence.transportGeneration == 0uL || deviceGeneratedSessionNonce !in 1..0xffff_ffffL) {
            return CompanionAuthorizationWireError.INVALID_TRANSPORT_GENERATION
        }
        if (!evidence.linkEncrypted) return CompanionAuthorizationWireError.LINK_NOT_ENCRYPTED
        if (!evidence.authenticatedBond) return CompanionAuthorizationWireError.BOND_NOT_AUTHENTICATED
        if (!evidence.claimWireSupported) return CompanionAuthorizationWireError.CLAIM_CAPABILITY_NOT_NEGOTIATED
        if (transportGeneration != 0uL || provisionalSessionOpen || applicationAuthorized) {
            return CompanionAuthorizationWireError.CLAIM_IN_PROGRESS
        }
        if (evidence.transportGeneration <= lastTransportGeneration) return CompanionAuthorizationWireError.STALE_START
        phase = CompanionAuthorizationResponsePhase.IDLE
        purpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
        sessionNonce = deviceGeneratedSessionNonce
        exchangeId = 0
        lastExchangeId = 0
        transportGeneration = evidence.transportGeneration
        lastTransportGeneration = evidence.transportGeneration
        correlation = null
        provisionalSessionOpen = true
        applicationAuthorized = false
        return CompanionAuthorizationWireError.NONE
    }

    fun begin(generation: ULong, startFragment: CompanionFragment): CompanionAuthorizationWireError {
        if (!provisionalSessionOpen) return CompanionAuthorizationWireError.NO_PROVISIONAL_SESSION
        if (generation == 0uL || generation != transportGeneration) {
            return CompanionAuthorizationWireError.WRONG_TRANSPORT_GENERATION
        }
        if (startFragment.kind != CompanionFrameKind.AUTHORIZATION_CLAIM_START) {
            return CompanionAuthorizationWireError.WRONG_DIRECTION
        }
        CompanionAuthorizationWireCodec.validateAuthorizationFragment(startFragment).let {
            if (it != CompanionAuthorizationWireError.NONE) return it
        }
        if (phase == CompanionAuthorizationResponsePhase.AWAITING_PENDING || phase == CompanionAuthorizationResponsePhase.PENDING) {
            return CompanionAuthorizationWireError.CLAIM_IN_PROGRESS
        }
        if (startFragment.sessionNonce != sessionNonce) return CompanionAuthorizationWireError.WRONG_SESSION
        if (startFragment.exchangeId !in 1..0xffff_ffffL) return CompanionAuthorizationWireError.WRONG_EXCHANGE
        if (startFragment.exchangeId <= lastExchangeId) return CompanionAuthorizationWireError.STALE_START
        val decoded = CompanionAuthorizationWireCodec.decodeClaimStart(startFragment.payload).value!!
        purpose = decoded.purpose
        exchangeId = startFragment.exchangeId
        lastExchangeId = exchangeId
        correlation = null
        phase = CompanionAuthorizationResponsePhase.AWAITING_PENDING
        return CompanionAuthorizationWireError.NONE
    }

    fun observe(generation: ULong, responseFragment: CompanionFragment): CompanionAuthorizationResponseObservation {
        if (generation == 0uL || generation != transportGeneration) {
            return reject(CompanionAuthorizationWireError.WRONG_TRANSPORT_GENERATION)
        }
        if (phase == CompanionAuthorizationResponsePhase.TERMINAL) {
            return reject(CompanionAuthorizationWireError.DUPLICATE_RESPONSE)
        }
        if (phase == CompanionAuthorizationResponsePhase.IDLE) {
            return reject(CompanionAuthorizationWireError.NO_CLAIM_IN_PROGRESS)
        }
        if (
            responseFragment.kind != CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS &&
            responseFragment.kind != CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT
        ) return reject(CompanionAuthorizationWireError.WRONG_DIRECTION)
        CompanionAuthorizationWireCodec.validateAuthorizationFragment(responseFragment).let {
            if (it != CompanionAuthorizationWireError.NONE) return reject(it)
        }
        if (responseFragment.sessionNonce != sessionNonce) return reject(CompanionAuthorizationWireError.WRONG_SESSION)
        if (responseFragment.exchangeId != exchangeId) return reject(CompanionAuthorizationWireError.WRONG_EXCHANGE)
        if (responseFragment.kind == CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS) {
            if (phase != CompanionAuthorizationResponsePhase.AWAITING_PENDING) {
                return reject(CompanionAuthorizationWireError.DUPLICATE_RESPONSE)
            }
            val decoded = CompanionAuthorizationWireCodec.decodeClaimStatus(responseFragment.payload).value!!
            if (decoded.purpose != purpose) return reject(CompanionAuthorizationWireError.PURPOSE_MISMATCH)
            correlation = decoded.correlation
            phase = CompanionAuthorizationResponsePhase.PENDING
            return CompanionAuthorizationResponseObservation(CompanionAuthorizationWireError.NONE, phase)
        }
        if (phase != CompanionAuthorizationResponsePhase.PENDING) {
            return reject(CompanionAuthorizationWireError.RESPONSE_OUT_OF_ORDER)
        }
        val decoded = CompanionAuthorizationWireCodec.decodeClaimResult(responseFragment.payload).value!!
        if (decoded.purpose != purpose) return reject(CompanionAuthorizationWireError.PURPOSE_MISMATCH)
        if (decoded.correlation != correlation) return reject(CompanionAuthorizationWireError.CORRELATION_MISMATCH)
        phase = CompanionAuthorizationResponsePhase.TERMINAL
        applicationAuthorized = decoded.outcome == CompanionAuthorizationClaimOutcome.ACCEPTED ||
            decoded.outcome == CompanionAuthorizationClaimOutcome.REPLACED
        provisionalSessionOpen = false
        return CompanionAuthorizationResponseObservation(
            CompanionAuthorizationWireError.NONE,
            phase,
            decoded.outcome,
            decoded.reason,
        )
    }

    fun cancel(generation: ULong): CompanionAuthorizationWireError {
        if (generation == 0uL || generation != transportGeneration) {
            return CompanionAuthorizationWireError.WRONG_TRANSPORT_GENERATION
        }
        if (phase == CompanionAuthorizationResponsePhase.TERMINAL) return CompanionAuthorizationWireError.DUPLICATE_RESPONSE
        phase = CompanionAuthorizationResponsePhase.IDLE
        purpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
        exchangeId = 0
        correlation = null
        provisionalSessionOpen = false
        applicationAuthorized = false
        return CompanionAuthorizationWireError.NONE
    }

    fun closeTransportGeneration(generation: ULong): CompanionAuthorizationWireError {
        if (generation == 0uL || generation != transportGeneration) {
            return CompanionAuthorizationWireError.WRONG_TRANSPORT_GENERATION
        }
        phase = CompanionAuthorizationResponsePhase.IDLE
        purpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER
        sessionNonce = 0
        exchangeId = 0
        lastExchangeId = 0
        transportGeneration = 0uL
        correlation = null
        provisionalSessionOpen = false
        applicationAuthorized = false
        return CompanionAuthorizationWireError.NONE
    }

    fun allowsNormalCompanionTraffic(generation: ULong): Boolean =
        generation != 0uL && generation == transportGeneration && applicationAuthorized

    fun status(): CompanionAuthorizationResponseStatus = CompanionAuthorizationResponseStatus(
        phase,
        purpose,
        correlation?.isValid() == true,
        phase == CompanionAuthorizationResponsePhase.TERMINAL,
        provisionalSessionOpen,
        applicationAuthorized,
    )

    private fun reject(error: CompanionAuthorizationWireError) =
        CompanionAuthorizationResponseObservation(error, phase)
}

private fun ByteArray.hasAuthorizationMagic(magic: ByteArray): Boolean =
    size >= magic.size && magic.indices.all { this[it] == magic[it] }

private fun ByteArray.u8(offset: Int): Int = this[offset].toInt() and 0xff
