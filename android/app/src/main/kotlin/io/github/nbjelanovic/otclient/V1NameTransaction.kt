package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionNameKind
import io.github.nbjelanovic.otprotocol.CompanionNamePayload
import io.github.nbjelanovic.otprotocol.CompanionNamePayloadCodec

/** Opaque trusted references, never phone labels as device identity or wire authorization. */
data class V1NameContext(
    val device: Long,
    val runtime: Long,
    val owner: Long,
    val ownerGeneration: Long,
    val transportGeneration: Long,
    val controller: Long,
    val sessionNonce: UInt,
    val setupLabel: V1SetupLabel?,
    val sessionToken: V1SetupSessionToken,
) {
    internal fun validEpoch() = device > 0 && runtime > 0 && owner > 0 && ownerGeneration > 0
    internal fun validSession() = validEpoch() && transportGeneration > 0 && controller > 0 && sessionNonce != 0u
    internal fun sameEpoch(other: V1NameContext) = device == other.device && runtime == other.runtime &&
        owner == other.owner && ownerGeneration == other.ownerGeneration
    internal fun sameProtocolSession(other: V1NameContext) = sameEpoch(other) &&
        transportGeneration == other.transportGeneration && controller == other.controller && sessionNonce == other.sessionNonce
    override fun toString() = "V1NameContext(redacted)"
}

enum class V1NamePhase { UNAVAILABLE, DISCONNECTED, CONNECTED, READY, REVOKED }
data class V1NameAuthority(val phase: V1NamePhase, val context: V1NameContext?) {
    override fun toString() = "V1NameAuthority(phase=$phase, context=redacted)"
}

/**
 * Coherent trusted observations in one serialized, nonreentrant application domain.
 * The future adapter proves Ready/profile support and supplies ordered, nonreused
 * session/owner generations. This model neither authenticates nor negotiates wire.
 */
fun interface V1NameAuthoritySource { fun current(): V1NameAuthority }

class V1NameRequest internal constructor(
    val context: V1NameContext,
    val exchangeId: UInt,
    val payload: CompanionNamePayload,
) {
    fun encodedPayload(): ByteArray = checkNotNull(CompanionNamePayloadCodec.encode(payload))
    override fun toString() = "V1NameRequest(kind=${payload.kind}, identity=redacted, name=redacted)"
}
enum class V1NameResultCode { IGNORED, INVALID_RESPONSE, SNAPSHOT, APPLIED, REJECTED, UNCERTAIN }
data class V1NameResult(
    val code: V1NameResultCode,
    val receipt: V1NameReadbackReceipt? = null,
    val snapshot: CompanionNamePayload? = null,
) {
    override fun toString() = "V1NameResult(code=$code, receipt=redacted, snapshot=redacted)"
}
enum class V1NameLifecycle { DISCONNECTED, REVOKED, RESET }

/**
 * Model only: one pending request, one registered receipt, no transport calls.
 * Initial/new-session work requires an explicit READ before WRITE. READ confirms
 * current state and never attributes a historical write. firstExchangeId is a
 * trusted construction/test seam; never reconstruct within the same session.
 * Future transport must call lostResult on timeout/loss; no timer is invented here.
 */
class V1NameTransaction(
    private val source: V1NameAuthoritySource,
    firstExchangeId: UInt = 1u,
    private val sharedExchangeAllocator: (() -> UInt?)? = null,
) {
    private var current: V1NameAuthority? = null
    private var blockedSession: V1NameContext? = null
    private var blockedOwner: V1NameContext? = null
    private var sequenceContext: V1NameContext? = null
    private var nextExchange = firstExchangeId
    private var pending: V1NameRequest? = null
    private var issuedReceipt: V1NameReadbackReceipt? = null
    private var receiptRequest: V1NameRequest? = null
    private var knownRevision: ULong? = null
    var reconciliationRequired: Boolean = true
        private set

    private fun invalidate() {
        pending = null
        issuedReceipt = null
        receiptRequest = null
        knownRevision = null
        reconciliationRequired = true
    }

    fun observe(): Boolean {
        val next = try { source.current() } catch (_: Exception) {
            V1NameAuthority(V1NamePhase.UNAVAILABLE, null)
        }
        val old = current
        val context = next.context
        val ready = next.phase == V1NamePhase.READY && context?.validSession() == true
        if (old?.phase == V1NamePhase.READY && old.context?.validSession() == true &&
            (!ready || context?.let { !old.context.sameProtocolSession(it) } == true) && blockedOwner == null) {
            blockedSession = old.context
        }
        if (context?.validEpoch() == true && next.phase != V1NamePhase.UNAVAILABLE &&
            (next.phase != V1NamePhase.READY || ready)) {
            if (blockedOwner?.let { !context.sameEpoch(it) } == true) blockedOwner = null
            if (blockedSession?.let { !context.sameEpoch(it) } == true) blockedSession = null
            if (ready && blockedSession?.let { !context.sameProtocolSession(it) } == true) blockedSession = null
            if (next.phase == V1NamePhase.REVOKED) blockedOwner = context
        }
        current = next
        val allowed = ready && blockedOwner?.let { context!!.sameEpoch(it) } != true &&
            blockedSession?.let { context!!.sameProtocolSession(it) } != true
        if (!allowed || old?.context != context) invalidate()
        return allowed
    }

    fun lifecycle(expected: V1NameContext, event: V1NameLifecycle): Boolean {
        observe()
        if (current?.context != expected || !expected.validSession()) return false
        if (event == V1NameLifecycle.DISCONNECTED) {
            if (blockedOwner == null) blockedSession = expected
        } else blockedOwner = expected
        invalidate()
        return true
    }

    fun lostResult(expected: V1NameContext, exchangeId: UInt): Boolean {
        observe()
        val request = pending ?: receiptRequest ?: return false
        if (request.context != expected || request.exchangeId != exchangeId || current?.context != expected) return false
        invalidate()
        return true
    }

    fun beginRead(): V1NameRequest? = begin(CompanionNamePayload(CompanionNameKind.READ))

    fun beginWrite(name: V1DeviceName, expectedRevision: ULong): V1NameRequest? {
        return begin(CompanionNamePayload(CompanionNameKind.WRITE, expectedRevision, name.value))
    }

    private fun begin(payload: CompanionNamePayload): V1NameRequest? {
        if (!observe() || pending != null) return null
        if (payload.kind == CompanionNameKind.WRITE &&
            (reconciliationRequired || knownRevision != payload.revision || payload.revision == ULong.MAX_VALUE)) return null
        val context = current?.context ?: return null
        // Setup metadata binds receipts but cannot restart a wire-session exchange sequence.
        if (sequenceContext?.sameProtocolSession(context) == false) nextExchange = 1u
        if (nextExchange == 0u || CompanionNamePayloadCodec.encode(payload) == null) return null
        sequenceContext = context
        val id = sharedExchangeAllocator?.invoke() ?: if (sharedExchangeAllocator == null) nextExchange else return null
        if (id == 0u || id < nextExchange) return null
        nextExchange = if (id == UInt.MAX_VALUE) 0u else id + 1u
        issuedReceipt = null
        receiptRequest = null
        return V1NameRequest(context, id, payload).also { pending = it }
    }

    fun receive(context: V1NameContext, exchangeId: UInt, payload: ByteArray): V1NameResult {
        if (!observe()) return V1NameResult(V1NameResultCode.IGNORED)
        val request = pending ?: return V1NameResult(V1NameResultCode.IGNORED)
        if (request.context != context || request.exchangeId != exchangeId || current?.context != context)
            return V1NameResult(V1NameResultCode.IGNORED)
        val decoded = CompanionNamePayloadCodec.decode(payload)
        if (decoded == null) {
            invalidate()
            return V1NameResult(V1NameResultCode.INVALID_RESPONSE)
        }
        pending = null
        if (decoded.kind == CompanionNameKind.REJECTED) {
            // Re-read even proven rejection: stale revision may mean our baseline changed.
            invalidate()
            return V1NameResult(V1NameResultCode.REJECTED)
        }
        if (decoded.kind == CompanionNameKind.UNCERTAIN) {
            invalidate()
            return V1NameResult(V1NameResultCode.UNCERTAIN)
        }
        if (request.payload.kind == CompanionNameKind.READ && decoded.kind == CompanionNameKind.SNAPSHOT) {
            knownRevision = decoded.revision
            reconciliationRequired = false
            return V1NameResult(V1NameResultCode.SNAPSHOT, snapshot = decoded)
        }
        if (request.payload.kind == CompanionNameKind.WRITE && decoded.kind == CompanionNameKind.APPLIED &&
            request.payload.revision != ULong.MAX_VALUE && decoded.revision == request.payload.revision + 1uL &&
            decoded.name == request.payload.name) {
            knownRevision = decoded.revision
            reconciliationRequired = false
            val receipt = V1NameReadbackReceipt(this)
            issuedReceipt = receipt
            receiptRequest = request
            return V1NameResult(V1NameResultCode.APPLIED, receipt)
        }
        invalidate()
        return V1NameResult(V1NameResultCode.INVALID_RESPONSE)
    }

    internal fun consume(receipt: V1NameReadbackReceipt, label: V1SetupLabel?, token: V1SetupSessionToken?, name: V1DeviceName): Boolean {
        if (!observe() || issuedReceipt !== receipt) return false
        val request = receiptRequest ?: return false
        if (current?.context != request.context || request.context.setupLabel != label ||
            request.context.sessionToken != token || request.payload.name != name.value) return false
        issuedReceipt = null
        receiptRequest = null
        return true
    }
}
