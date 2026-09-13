package io.github.nbjelanovic.otclient

/** One service-owned protected BLE session. Never reconstruct this reference from a name or PIN. */
class V1GroupConfirmationAuthority internal constructor() {
    override fun toString() = "V1GroupConfirmationAuthority(redacted)"
}

enum class V1GroupConfirmationRole { INVITER, JOINER }
enum class V1GroupConfirmationDecision { CONFIRM, CANCEL }
enum class V1GroupConfirmationDeviceOutcome { LOCAL_CONFIRMED, CANCELLED, REFUSED }
class V1GroupConfirmationResult internal constructor(
    val offer: V1GroupConfirmationOffer,
    val decision: V1GroupConfirmationDecision,
    val outcome: V1GroupConfirmationDeviceOutcome,
) {
    override fun toString() = "V1GroupConfirmationResult(outcome=$outcome, fields=redacted)"
}
enum class V1GroupConfirmationCloseReason {
    SESSION_ENDED, EXPIRED, CLOCK_INVALID, INVALID_OFFER, DUPLICATE_OFFER,
    STALE_HANDLE, SOURCE_UNAVAILABLE, SUBMISSION_FAILED, INVALID_RESULT, REENTRANT, CLOSED,
}

/**
 * Immutable service-issued presentation and exact one-use handle. Only a future verified device
 * adapter may construct this after its authenticated offer/transcript admission. These bounded
 * strings specify presentation, not a crypto suite or wire encoding. In particular, the adapter
 * must supply the full authoritative fingerprint, never a display name or shortened alias.
 * Deadline uses the controller's monotonic clock domain and never extends the device deadline.
 */
class V1GroupConfirmationOffer internal constructor(
    val authority: V1GroupConfirmationAuthority,
    val attempt: Long,
    val role: V1GroupConfirmationRole,
    val group: String,
    val epoch: UInt,
    val peerFingerprint: String,
    val confirmation: String,
    val deadlineMillis: Long,
) {
    init {
        require(attempt > 0 && epoch != 0u && deadlineMillis in 1 until Long.MAX_VALUE)
        require(canonicalReference(group) && canonicalReference(peerFingerprint))
        require(confirmation.length in 1..128 && confirmation.all { it in ' '..'~' } &&
            confirmation.any { it != ' ' })
    }

    override fun toString() = "V1GroupConfirmationOffer(redacted)"

    private fun canonicalReference(value: String): Boolean = value.length in 2..256 &&
        value.length % 2 == 0 && value.all { it in '0'..'9' || it in 'a'..'f' } &&
        value.any { it != '0' }
}

sealed interface V1GroupConfirmationState {
    data object Unsupported : V1GroupConfirmationState
    data object Idle : V1GroupConfirmationState
    class Review(val offer: V1GroupConfirmationOffer) : V1GroupConfirmationState {
        override fun toString() = "V1GroupConfirmationState.Review(redacted)"
    }
    data class Submitted(val decision: V1GroupConfirmationDecision) : V1GroupConfirmationState
    data class DeviceResult(val decision: V1GroupConfirmationDecision,
        val outcome: V1GroupConfirmationDeviceOutcome) : V1GroupConfirmationState
    data class Closed(val reason: V1GroupConfirmationCloseReason) : V1GroupConfirmationState
}

/**
 * Serialized service adapter, absent in current production. pollOffer transfers each offer once;
 * a repeated offer is a protocol failure, not an idempotent snapshot. submit reports only request
 * submission, never device acceptance, membership, durable consumption or traffic authority.
 * The future device adapter owns authenticated admission and durable attempt consumption.
 * close releases the local lease; it must not confirm an offer or start another attempt.
 */
interface V1GroupConfirmationSource : AutoCloseable {
    fun pollOffer(): V1GroupConfirmationOffer?
    fun submit(offer: V1GroupConfirmationOffer, decision: V1GroupConfirmationDecision): Boolean
    fun pollResult(): V1GroupConfirmationResult? = null
    /** Owner callbacks may arrive synchronously; the controller defers them across its operations. */
    fun setChangedObserver(observer: (() -> Unit)?) = Unit
    override fun close() = Unit
}

fun interface V1GroupConfirmationAdapter {
    fun open(authority: V1GroupConfirmationAuthority): V1GroupConfirmationSource?
}

object DisabledV1GroupConfirmationAdapter : V1GroupConfirmationAdapter {
    override fun open(authority: V1GroupConfirmationAuthority): V1GroupConfirmationSource? = null
}

/**
 * One coordinator per protected BLE session, one offer attempt, and no activation API. Local
 * invalidation is sticky. The owner must keep this coordinator for that session even after a
 * terminal result; recreating it within the same session is forbidden. A fresh connection needs
 * a fresh authority reference and cannot reuse an old offer. This is local presentation/request
 * admission only, not proof of device cryptography or persistence across process restart.
 */
class V1GroupConfirmationCoordinator(
    private val authority: V1GroupConfirmationAuthority,
    private val source: V1GroupConfirmationSource,
    private val currentAuthority: () -> V1GroupConfirmationAuthority?,
    private val clockMillis: () -> Long,
) : AutoCloseable {
    var state: V1GroupConfirmationState = V1GroupConfirmationState.Idle
        private set
    private var pending: V1GroupConfirmationOffer? = null
    private var submittedOffer: V1GroupConfirmationOffer? = null
    private var submittedDecision: V1GroupConfirmationDecision? = null
    private var consumed = false
    private var lastClock: Long? = null
    private var terminal = false
    private var busy = false
    private var sourceClosed = false

    val deadlineMillis: Long? get() = (pending ?: submittedOffer)?.deadlineMillis

    fun checkFreshness(): Boolean = operation { checkCurrent(pending ?: submittedOffer) }

    fun remainingMillis(): Long? {
        if (!checkFreshness()) return null
        val deadline = deadlineMillis ?: return null
        return deadline - checkNotNull(lastClock)
    }

    fun refresh(): Boolean = operation {
        if (!checkCurrent(pending ?: submittedOffer)) return@operation false
        if (consumed) {
            val result = try { source.pollResult() } catch (_: Exception) {
                refuse(V1GroupConfirmationCloseReason.SOURCE_UNAVAILABLE)
                return@operation false
            }
            if (terminal || !checkCurrent(submittedOffer)) return@operation false
            if (result == null) return@operation true
            if (result.offer !== submittedOffer || result.decision != submittedDecision ||
                result.outcome == V1GroupConfirmationDeviceOutcome.LOCAL_CONFIRMED && result.decision != V1GroupConfirmationDecision.CONFIRM ||
                result.outcome == V1GroupConfirmationDeviceOutcome.CANCELLED && result.decision != V1GroupConfirmationDecision.CANCEL) {
                refuse(V1GroupConfirmationCloseReason.INVALID_RESULT)
                return@operation false
            }
            state = V1GroupConfirmationState.DeviceResult(result.decision, result.outcome)
            submittedOffer = null
            terminal = true
            return@operation true
        }
        val next = try { source.pollOffer() } catch (_: Exception) {
            refuse(V1GroupConfirmationCloseReason.SOURCE_UNAVAILABLE)
            return@operation false
        }
        if (terminal || !checkCurrent(pending)) return@operation false
        if (next == null) return@operation true
        if (next.authority !== authority) {
            refuse(V1GroupConfirmationCloseReason.INVALID_OFFER)
            return@operation false
        }
        if (pending != null) {
            refuse(V1GroupConfirmationCloseReason.DUPLICATE_OFFER)
            return@operation false
        }
        if (!checkCurrent(next)) return@operation false
        pending = next
        state = V1GroupConfirmationState.Review(next)
        true
    }

    fun confirm(offer: V1GroupConfirmationOffer): Boolean = decide(offer, V1GroupConfirmationDecision.CONFIRM)
    fun cancel(offer: V1GroupConfirmationOffer): Boolean = decide(offer, V1GroupConfirmationDecision.CANCEL)

    private fun decide(offer: V1GroupConfirmationOffer, decision: V1GroupConfirmationDecision): Boolean = operation {
        if (consumed) return@operation false
        if (offer !== pending) {
            refuse(V1GroupConfirmationCloseReason.STALE_HANDLE)
            return@operation false
        }
        if (!checkCurrent(offer)) return@operation false
        // Consume locally before calling the adapter; a throw or uncertain result cannot retry.
        pending = null
        consumed = true
        submittedOffer = offer
        submittedDecision = decision
        val submitted = try { source.submit(offer, decision) } catch (_: Exception) { false }
        if (terminal || !checkCurrent(offer)) return@operation false
        if (!submitted) {
            refuse(V1GroupConfirmationCloseReason.SUBMISSION_FAILED)
            return@operation false
        }
        state = V1GroupConfirmationState.Submitted(decision)
        true
    }

    private fun operation(action: () -> Boolean): Boolean {
        if (busy) {
            refuse(V1GroupConfirmationCloseReason.REENTRANT)
            return false
        }
        if (terminal) return false
        busy = true
        return try { action() } finally { busy = false }
    }

    private fun checkCurrent(offer: V1GroupConfirmationOffer?): Boolean {
        val current = try { currentAuthority() } catch (_: Exception) { null }
        if (terminal) return false
        if (current !== authority) {
            refuse(V1GroupConfirmationCloseReason.SESSION_ENDED)
            return false
        }
        val now = try { clockMillis() } catch (_: Exception) {
            refuse(V1GroupConfirmationCloseReason.CLOCK_INVALID)
            return false
        }
        if (terminal) return false
        if (now < 0 || now == Long.MAX_VALUE || lastClock?.let { now < it } == true) {
            refuse(V1GroupConfirmationCloseReason.CLOCK_INVALID)
            return false
        }
        lastClock = now
        if (offer != null && now >= offer.deadlineMillis) {
            refuse(V1GroupConfirmationCloseReason.EXPIRED)
            return false
        }
        return true
    }

    private fun refuse(reason: V1GroupConfirmationCloseReason) {
        pending = null
        submittedOffer = null
        terminal = true
        state = V1GroupConfirmationState.Closed(reason)
    }

    override fun close() {
        refuse(V1GroupConfirmationCloseReason.CLOSED)
        if (!sourceClosed) {
            sourceClosed = true
            try { source.close() } catch (_: Exception) { /* Closure never restores authority. */ }
        }
    }
}
