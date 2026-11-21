package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*

/** Volatile display projection. A recipient handle and these hints cannot authorize a submit. */
data class V1TextState(
    val recipient: V1QuickStatusRecipient? = null,
    val received: String? = null,
    val outbound: BlePacketDeliveryPhase? = null,
    val reason: Int = 0,
    val currentAttachment: Boolean = false,
    val queued: List<V1QueuedText> = emptyList(),
    val queueUnavailable: Boolean = false,
) {
    override fun toString() = "V1TextState(outbound=$outbound, reason=$reason, content=redacted)"
}

data class V1QueuedText(val text: String, val status: String) {
    override fun toString() = "V1QueuedText(status=$status, text=redacted)"
}

object V1TextComposer {
    const val AUTHORITY_UNAVAILABLE = "This connection can't send text messages yet."
    fun refusal(text: String): String? = when {
        text.isEmpty() -> "Enter a message before sending."
        text.any { it.isSurrogate() } && !validScalars(text) -> "This message contains text MeshTrail can't read."
        OtxtPayloadCodec.encode(text) == null -> "Shorten your message before sending."
        else -> null
    }
    private fun validScalars(text: String): Boolean = try {
        Charsets.UTF_8.newEncoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
            .encode(java.nio.CharBuffer.wrap(text)); true
    } catch (_: java.nio.charset.CharacterCodingException) { false }
    fun reason(code: Int): String = when (code) {
        1 -> "Your device couldn't read the message."
        2 -> "This message is too long for your device."
        3 -> "The receiving device changed before sending."
        4 -> "Your device isn't ready to send to this person."
        5 -> "The connection changed before sending."
        6 -> "Your device refused a repeated send."
        7 -> "Your device doesn't recognize this kind of message."
        8 -> "Your device doesn't support this action."
        9 -> "Device is busy"
        10 -> "Your device couldn't save the details needed to send."
        11 -> "Your device couldn't send over the radio."
        12 -> "Sending was interrupted."
        13 -> "The connection timed out."
        14 -> "Your device couldn't verify this connection."
        15 -> "Reconnect before sending more messages."
        else -> ""
    }
}

/** Correlation and OT-0314 projection only; runtime must verify its current provider before entry. */
internal class V1TextConversation {
    private var request: CompanionTextSend? = null
    private var identity: Triple<ULong, UInt, ULong>? = null
    private var result: CompanionTextResult? = null
    private var lastReceivedCounter = 0uL
    private var lastSentCounter = 0uL
    private var receiveScope: CompanionAuthorityResponse? = null
    private var queuedEvidence = false
    var faulted = false
        private set
    var phase: BlePacketDeliveryPhase? = null
        private set
    var reason = 0
        private set
    var received: String? = null
        private set
    var currentAttachment = false
        private set
    val busy: Boolean get() = phase in listOf(BlePacketDeliveryPhase.AWAITING_ACCEPTANCE,
        BlePacketDeliveryPhase.QUEUE_ACCEPTED, BlePacketDeliveryPhase.TRANSMISSION_COMPLETED)
    val delivered: Boolean get() = phase == BlePacketDeliveryPhase.PROTECTED_PEER_ACK
    val origin: CompanionTextSend? get() = request
    val receipt: ULong? get() = identity?.first

    /** Replay watermark only; this cache is never consulted for authority. */
    fun attach(scope: CompanionAuthorityResponse) {
        val old = receiveScope
        if (old == null || old.group != scope.group || old.epoch != scope.epoch ||
            old.sessionGeneration != scope.sessionGeneration || old.membershipRevision != scope.membershipRevision ||
            !old.localIdentity.contentEquals(scope.localIdentity) || !old.remoteIdentity.contentEquals(scope.remoteIdentity)) { lastReceivedCounter = 0uL; lastSentCounter = 0uL }
        receiveScope = scope
        faulted = false
    }
    fun begin(value: CompanionTextSend): Boolean {
        if (busy) return false
        request = value; identity = null; result = null; queuedEvidence = false; faulted = false
        phase = BlePacketDeliveryPhase.AWAITING_ACCEPTANCE; reason = 0; currentAttachment = true
        return true
    }
    fun receive(value: CompanionTextRecord): Boolean {
        if (value is CompanionTextReceived) {
            if (value.counter <= lastReceivedCounter) return false
            lastReceivedCounter = value.counter; received = value.text
            return true
        }
        val origin = request ?: return false
        if (!currentAttachment || value.reference != origin.reference) return false
        return when (value) {
            is CompanionTextResult -> {
                if (value.exchange != origin.exchange || value.operation != origin.operation) return false
                val previous = result
                if (previous != null) {
                    if (previous == value) return false
                    contain(); return false
                }
                result = value
                if (!busy) {
                    if (value.disposition == 1 && identity == Triple(value.receipt, value.messageId, value.counter)) return true
                    contain(); return false
                }
                when (value.disposition) {
                    1 -> {
                        if (!bind(value.receipt, value.messageId, value.counter)) { contain(); return false }
                        if (phase == BlePacketDeliveryPhase.AWAITING_ACCEPTANCE) phase = BlePacketDeliveryPhase.QUEUE_ACCEPTED
                    }
                    2 -> {
                        if (identity != null) contain()
                        else { phase = BlePacketDeliveryPhase.AUTHORITATIVE_REJECTION; reason = value.reason }
                    }
                    3 -> {
                        if (value.receipt != 0uL && !bind(value.receipt, value.messageId, value.counter)) { contain(); return false }
                        uncertain(); reason = value.reason
                    }
                }
                true
            }
            is CompanionTextDelivery -> {
                if (value.originExchange != origin.exchange || value.operation != origin.operation || !busy) return false
                if (!bind(value.receipt, value.messageId, value.counter)) { contain(); return false }
                when (value.phase) {
                    1 -> {
                        if (queuedEvidence || phase !in listOf(BlePacketDeliveryPhase.AWAITING_ACCEPTANCE,
                            BlePacketDeliveryPhase.QUEUE_ACCEPTED)) { contain(); return false }
                        queuedEvidence = true; phase = BlePacketDeliveryPhase.QUEUE_ACCEPTED
                    }
                    2 -> {
                        if (phase != BlePacketDeliveryPhase.QUEUE_ACCEPTED) { contain(); return false }
                        phase = BlePacketDeliveryPhase.TRANSMISSION_COMPLETED
                    }
                    3 -> {
                        if (phase != BlePacketDeliveryPhase.TRANSMISSION_COMPLETED) { contain(); return false }
                        phase = BlePacketDeliveryPhase.PROTECTED_PEER_ACK
                    }
                    5 -> { phase = BlePacketDeliveryPhase.FAILED; reason = value.reason }
                    6 -> { uncertain(); reason = value.reason }
                }
                true
            }
            else -> false
        }
    }
    private fun bind(receipt: ULong, message: UInt, counter: ULong): Boolean {
        val next = Triple(receipt, message, counter)
        if (identity == null) {
            if (counter <= lastSentCounter) return false
            identity = next; lastSentCounter = counter
        }
        return identity == next
    }
    private fun contain() { faulted = true; uncertain() }
    fun uncertain() {
        if (busy) { phase = BlePacketDeliveryPhase.OUTCOME_UNKNOWN; reason = 12 }
    }
    fun retire(handedOff: Boolean = true) {
        if (!handedOff && phase == BlePacketDeliveryPhase.AWAITING_ACCEPTANCE) phase = BlePacketDeliveryPhase.NOT_SENT else uncertain()
        received = null; currentAttachment = false; request = null; identity = null; result = null
    }
    override fun toString() = "V1TextConversation(phase=$phase, fields=redacted)"
}
