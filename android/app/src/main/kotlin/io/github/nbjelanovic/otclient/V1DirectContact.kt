package io.github.nbjelanovic.otclient

enum class V1DirectContactState {
    AVAILABLE,
    OUTGOING_REQUEST_PENDING,
    INCOMING_REQUEST_PENDING,
    ACCEPTED,
    DECLINED,
    BLOCKED,
}

enum class V1DirectContactFailure { INVALID_TRANSITION, CONTACT_BLOCKED, CHAT_NOT_ACCEPTED }

class V1DirectPeerToken private constructor(private val bytes: ByteArray) {
    override fun equals(other: Any?): Boolean =
        other is V1DirectPeerToken && bytes.contentEquals(other.bytes)

    override fun hashCode(): Int = bytes.contentHashCode()
    override fun toString(): String = "V1DirectPeerToken(redacted)"

    companion object {
        const val SIZE_BYTES = 16

        fun create(value: ByteArray): V1DirectPeerToken? =
            value.takeIf { it.size == SIZE_BYTES && it.any { byte -> byte.toInt() != 0 } }
                ?.copyOf()
                ?.let(::V1DirectPeerToken)
    }
}

sealed interface V1DirectContactResult {
    data class Accepted(val contact: V1DirectContact) : V1DirectContactResult
    data class Rejected(val failure: V1DirectContactFailure) : V1DirectContactResult
}

class V1DirectContact private constructor(
    val peerToken: V1DirectPeerToken,
    val peerName: V1PublicName,
    val state: V1DirectContactState,
    val locationSharingEnabled: Boolean,
) {
    val maySendTypedMessage: Boolean
        get() = state == V1DirectContactState.ACCEPTED

    val generatedIncomingRequestText: String?
        get() = if (state == V1DirectContactState.INCOMING_REQUEST_PENDING) {
            "${peerName.value} is requesting to chat with you"
        } else {
            null
        }

    fun requestChat(): V1DirectContactResult = when (state) {
        V1DirectContactState.AVAILABLE,
        V1DirectContactState.DECLINED,
        -> accepted(V1DirectContactState.OUTGOING_REQUEST_PENDING, false)
        V1DirectContactState.BLOCKED -> rejected(V1DirectContactFailure.CONTACT_BLOCKED)
        else -> rejected(V1DirectContactFailure.INVALID_TRANSITION)
    }

    fun receiveRequest(): V1DirectContactResult = when (state) {
        V1DirectContactState.AVAILABLE,
        V1DirectContactState.DECLINED,
        -> accepted(V1DirectContactState.INCOMING_REQUEST_PENDING, false)
        V1DirectContactState.BLOCKED -> rejected(V1DirectContactFailure.CONTACT_BLOCKED)
        else -> rejected(V1DirectContactFailure.INVALID_TRANSITION)
    }

    fun acceptRequest(): V1DirectContactResult =
        if (state == V1DirectContactState.INCOMING_REQUEST_PENDING) {
            accepted(V1DirectContactState.ACCEPTED, false)
        } else rejected(V1DirectContactFailure.INVALID_TRANSITION)

    fun markAcceptedByPeer(): V1DirectContactResult =
        if (state == V1DirectContactState.OUTGOING_REQUEST_PENDING) {
            accepted(V1DirectContactState.ACCEPTED, false)
        } else rejected(V1DirectContactFailure.INVALID_TRANSITION)

    fun declineRequest(): V1DirectContactResult =
        if (state == V1DirectContactState.INCOMING_REQUEST_PENDING) {
            accepted(V1DirectContactState.DECLINED, false)
        } else rejected(V1DirectContactFailure.INVALID_TRANSITION)

    fun block(): V1DirectContactResult = accepted(V1DirectContactState.BLOCKED, false)

    fun unblock(): V1DirectContactResult =
        if (state == V1DirectContactState.BLOCKED) {
            accepted(V1DirectContactState.AVAILABLE, false)
        } else rejected(V1DirectContactFailure.INVALID_TRANSITION)

    fun setLocationSharing(enabled: Boolean): V1DirectContactResult =
        if (state == V1DirectContactState.ACCEPTED) {
            accepted(state, enabled)
        } else rejected(V1DirectContactFailure.CHAT_NOT_ACCEPTED)

    override fun toString(): String =
        "V1DirectContact(peerToken=redacted, peerName=redacted, state=$state, " +
            "locationSharingEnabled=$locationSharingEnabled)"

    private fun accepted(nextState: V1DirectContactState, shareLocation: Boolean) =
        V1DirectContactResult.Accepted(V1DirectContact(peerToken, peerName, nextState, shareLocation))

    private fun rejected(failure: V1DirectContactFailure) = V1DirectContactResult.Rejected(failure)

    companion object {
        fun available(peerToken: V1DirectPeerToken, peerName: V1PublicName): V1DirectContact =
            V1DirectContact(peerToken, peerName, V1DirectContactState.AVAILABLE, false)
    }
}
