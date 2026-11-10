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
    const val TEXT_UNAVAILABLE = "Typed text is not supported yet. This message cannot be sent."
    const val AUTHORITY_UNAVAILABLE = "Quick statuses require a current authenticated recipient. Production messaging remains disabled."
    fun label(status: CompanionQuickStatus): String = when (status) {
        CompanionQuickStatus.OK -> "I'm OK"
        CompanionQuickStatus.NEED_ASSISTANCE -> "Need assistance"
        CompanionQuickStatus.ANYONE_ONLINE -> "Anyone online?"
        CompanionQuickStatus.AVAILABLE_TO_HELP -> "Available to help"
    }
    fun fromTemplate(text: String): CompanionQuickStatus? =
        CompanionQuickStatus.entries.firstOrNull { label(it) == text }

    fun evidence(phase: BlePacketDeliveryPhase): String = when (phase) {
        BlePacketDeliveryPhase.AWAITING_ACCEPTANCE -> "Awaiting device acceptance"
        BlePacketDeliveryPhase.QUEUE_ACCEPTED -> "Queued on device; delivery not confirmed"
        BlePacketDeliveryPhase.TRANSMISSION_COMPLETED -> "Transmission complete; peer receipt not confirmed"
        BlePacketDeliveryPhase.PROTECTED_PEER_ACK -> "Protected peer ACK observed; not a human or read acknowledgment"
        BlePacketDeliveryPhase.AUTHORITATIVE_REJECTION -> "Rejected by device"
        BlePacketDeliveryPhase.FAILED -> "Device reported failure"
        BlePacketDeliveryPhase.OUTCOME_UNKNOWN -> "Outcome unknown; do not resend automatically"
        BlePacketDeliveryPhase.NOT_SENT -> "Not sent"
    }
}
