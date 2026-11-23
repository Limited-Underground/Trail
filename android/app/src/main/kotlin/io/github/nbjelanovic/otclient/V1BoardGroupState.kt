package io.github.nbjelanovic.otclient

/** Group readback is scoped to the current protected connection. */
data class V1BoardGroupState(
    val supported: Boolean = false,
    val available: Boolean = false,
    val busy: Boolean = false,
    val canCreate: Boolean = false,
    val groups: List<V1BoardGroupSummary> = emptyList(),
    val notice: String? = null,
    /** The live invite this board just issued, for display until it expires. */
    val invite: V1BoardGroupInvite? = null,
)

data class V1BoardGroupSummary(
    val slot: Int,
    val groupId: ULong,
    val version: UInt,
    val locationRule: Int,
    val members: List<V1BoardGroupMember>,
    val distrusted: Boolean,
) {
    override fun toString() = "V1BoardGroupSummary(slot=$slot, version=$version, distrusted=$distrusted)"
}

data class V1BoardGroupMember(val alias: String, val administrator: Boolean) {
    override fun toString() = "V1BoardGroupMember(administrator=$administrator)"
}

/** A signed invite read back from the admin board. [code] is the QR and text payload. */
class V1BoardGroupInvite(val groupId: ULong, val label: String, val code: String, val issuedAtMs: Long) {
    val expiresAtMs get() = issuedAtMs + LIFETIME_MS
    override fun toString() = "V1BoardGroupInvite(redacted)"
    companion object { const val LIFETIME_MS = 180_000L }
}

sealed interface V1BoardGroupAction {
    data object Refresh : V1BoardGroupAction
    data class Create(val locationRule: Int) : V1BoardGroupAction
    data class StopTrust(val group: V1BoardGroupSummary) : V1BoardGroupAction
    /** Admin board issues (or repeats) the live invite; [label] is what the joiner sees. */
    data class Invite(val group: V1BoardGroupSummary, val label: String) : V1BoardGroupAction
    /** Joiner board verifies and claims a scanned or pasted invite. */
    class Join(invite: ByteArray) : V1BoardGroupAction {
        private val bytes = invite.copyOf()
        val invite: ByteArray get() = bytes.copyOf()
        override fun equals(other: Any?) = other is Join && bytes.contentEquals(other.bytes)
        override fun hashCode() = bytes.contentHashCode()
        override fun toString() = "Join(redacted)"
    }
}
