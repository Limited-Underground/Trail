package io.github.nbjelanovic.otclient

/** Fresh random request correlation supplied by the future authenticated transport owner. */
class V1ChatRequestToken private constructor(private val bytes: ByteArray) {
    override fun equals(other: Any?): Boolean = other is V1ChatRequestToken && bytes.contentEquals(other.bytes)
    override fun hashCode(): Int = bytes.contentHashCode()
    override fun toString(): String = "V1ChatRequestToken(redacted)"
    companion object {
        const val SIZE_BYTES = 16
        fun create(bytes: ByteArray): V1ChatRequestToken? = bytes.takeIf {
            it.size == SIZE_BYTES && it.any { value -> value.toInt() != 0 }
        }?.copyOf()?.let(::V1ChatRequestToken)
    }
}

/** This outbound consent request deliberately has no user-message field. Not a wire codec. */
class V1OutboundChatRequest internal constructor(
    val peerToken: V1DirectPeerToken,
    val requestToken: V1ChatRequestToken,
) {
    override fun toString(): String = "V1OutboundChatRequest(peer=redacted, request=redacted)"
}

enum class V1ChatRequestDecision { ACCEPTED, DECLINED, BLOCKED }

/**
 * Authentication is a caller precondition: the future trusted adapter must verify the
 * decision against the authenticated peer/session before constructing this receipt.
 * Peer and request tokens are correlation only, never proof of authentication.
 */
class V1ChatDecisionReceipt internal constructor(
    internal val peerToken: V1DirectPeerToken,
    internal val requestToken: V1ChatRequestToken,
    internal val decision: V1ChatRequestDecision,
) {
    private val consumed = java.util.concurrent.atomic.AtomicBoolean(false)
    internal fun consumeOnce(): Boolean = consumed.compareAndSet(false, true)
    override fun toString(): String = "V1ChatDecisionReceipt(peer=redacted, request=redacted, decision=$decision)"
}

/** Local enqueue candidate, never evidence of sent/delivered. requestToken is its dedupe key. */
class V1FirstMessageQueueItem internal constructor(
    val peerToken: V1DirectPeerToken,
    val requestToken: V1ChatRequestToken,
    val text: String,
) {
    override fun toString(): String = "V1FirstMessageQueueItem(peer=redacted, request=redacted, text=redacted)"
}

enum class V1PendingDirectChatFailure { REQUEST_NOT_PENDING, RECEIPT_MISMATCH, DECISION_ALREADY_USED }

sealed interface V1PendingDirectChatResult {
    class Applied internal constructor(val next: V1PendingDirectChat, val messageToQueue: V1FirstMessageQueueItem?) : V1PendingDirectChatResult {
        override fun toString(): String = "V1PendingDirectChatResult.Applied(message=redacted)"
    }
    data class Rejected(val failure: V1PendingDirectChatFailure) : V1PendingDirectChatResult
}

/**
 * Immutable, host-only consent state. The controller must atomically enqueue the candidate
 * and adopt next, deduplicating by request token. If enqueue fails it retains the whole
 * Applied result for retry, not the consumed receipt. Old snapshots must never replace the
 * current state. No persistence, authenticated adapter, or wire activation currently exists.
 */
class V1PendingDirectChat private constructor(
    val peerToken: V1DirectPeerToken,
    val requestToken: V1ChatRequestToken,
    val state: V1DirectContactState,
    private val firstMessage: String?,
) {
    val locationSharingEnabled: Boolean get() = false
    val hasLocalFirstMessage: Boolean get() = firstMessage != null
    val outboundRequest: V1OutboundChatRequest?
        get() = if (state == V1DirectContactState.OUTGOING_REQUEST_PENDING) V1OutboundChatRequest(peerToken, requestToken) else null

    fun receiveDecision(receipt: V1ChatDecisionReceipt): V1PendingDirectChatResult {
        if (state != V1DirectContactState.OUTGOING_REQUEST_PENDING || firstMessage == null) {
            return V1PendingDirectChatResult.Rejected(V1PendingDirectChatFailure.REQUEST_NOT_PENDING)
        }
        if (receipt.peerToken != peerToken || receipt.requestToken != requestToken) {
            return V1PendingDirectChatResult.Rejected(V1PendingDirectChatFailure.RECEIPT_MISMATCH)
        }
        if (!receipt.consumeOnce()) {
            return V1PendingDirectChatResult.Rejected(V1PendingDirectChatFailure.DECISION_ALREADY_USED)
        }
        val nextState = when (receipt.decision) {
            V1ChatRequestDecision.ACCEPTED -> V1DirectContactState.ACCEPTED
            V1ChatRequestDecision.DECLINED -> V1DirectContactState.DECLINED
            V1ChatRequestDecision.BLOCKED -> V1DirectContactState.BLOCKED
        }
        val candidate = if (receipt.decision == V1ChatRequestDecision.ACCEPTED) {
            V1FirstMessageQueueItem(peerToken, requestToken, firstMessage)
        } else null
        return V1PendingDirectChatResult.Applied(V1PendingDirectChat(peerToken, requestToken, nextState, null), candidate)
    }

    /** Local cancellation/block also invalidates late acceptance of this request. */
    fun cancel(): V1PendingDirectChat = if (state == V1DirectContactState.OUTGOING_REQUEST_PENDING) {
        V1PendingDirectChat(peerToken, requestToken, V1DirectContactState.DECLINED, null)
    } else this
    fun block(): V1PendingDirectChat = V1PendingDirectChat(peerToken, requestToken, V1DirectContactState.BLOCKED, null)

    override fun toString(): String = "V1PendingDirectChat(state=$state, peer=redacted, request=redacted, message=redacted)"

    companion object {
        const val MAX_MESSAGE_CHARS = 500
        fun begin(contact: V1DirectContact, requestToken: V1ChatRequestToken, firstMessage: String): V1PendingDirectChat? {
            if (firstMessage.isBlank() || firstMessage.length > MAX_MESSAGE_CHARS ||
                firstMessage.any { it.isISOControl() && it != '\n' } ||
                !Charsets.UTF_8.newEncoder().canEncode(firstMessage)) return null
            val requested = contact.requestChat() as? V1DirectContactResult.Accepted ?: return null
            return V1PendingDirectChat(contact.peerToken, requestToken, requested.contact.state, firstMessage)
        }
    }
}
