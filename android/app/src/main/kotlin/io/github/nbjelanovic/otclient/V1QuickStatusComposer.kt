package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionQuickStatus

/** Correlation to one admitted pair/attachment, never authentication or a durable recipient ID. */
class V1QuickStatusRecipient internal constructor() {
    override fun toString() = "V1QuickStatusRecipient(redacted)"
}

/** Copied display hints only. Every submit rechecks the live provider in the runtime. */
data class V1QuickStatusState(
    val recipient: V1QuickStatusRecipient? = null,
    val received: CompanionQuickStatus? = null,
    val outbound: BlePacketDeliveryPhase? = null,
)

/** Only the four existing wire meanings. No truncation, inferred synonyms or text encoding. */
object V1QuickStatusComposer {
    const val TEXT_UNAVAILABLE = "This message can't be sent as a quick message."
    const val AUTHORITY_UNAVAILABLE = "This connection can't send quick messages yet."
    fun label(status: CompanionQuickStatus): String = when (status) {
        CompanionQuickStatus.OK -> "I'm OK"
        CompanionQuickStatus.NEED_ASSISTANCE -> "Need assistance"
        CompanionQuickStatus.ANYONE_ONLINE -> "Anyone online?"
        CompanionQuickStatus.AVAILABLE_TO_HELP -> "Available to help"
    }
    fun fromTemplate(text: String): CompanionQuickStatus? =
        CompanionQuickStatus.entries.firstOrNull { label(it) == text }

    fun evidence(phase: BlePacketDeliveryPhase): String = when (phase) {
        BlePacketDeliveryPhase.AWAITING_ACCEPTANCE -> "Waiting for your device to accept it."
        BlePacketDeliveryPhase.QUEUE_ACCEPTED -> "Your device is waiting to send it."
        BlePacketDeliveryPhase.TRANSMISSION_COMPLETED -> "Your device sent it and hasn't confirmed delivery."
        BlePacketDeliveryPhase.PROTECTED_PEER_ACK -> "The other device received it."
        BlePacketDeliveryPhase.AUTHORITATIVE_REJECTION -> "Your device refused to send it."
        BlePacketDeliveryPhase.FAILED -> "Your device couldn't send it."
        BlePacketDeliveryPhase.OUTCOME_UNKNOWN -> "Delivery is uncertain.\nCheck before sending again."
        BlePacketDeliveryPhase.NOT_SENT -> "Not sent"
    }
}
