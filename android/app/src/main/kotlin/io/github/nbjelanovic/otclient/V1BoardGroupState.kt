package io.github.nbjelanovic.otclient

/** Group readback is scoped to the current protected connection. */
data class V1BoardGroupState(
    val supported: Boolean = false,
    val available: Boolean = false,
    val busy: Boolean = false,
    val canCreate: Boolean = false,
    val groups: List<V1BoardGroupSummary> = emptyList(),
    val notice: String? = null,
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

sealed interface V1BoardGroupAction {
    data object Refresh : V1BoardGroupAction
    data class Create(val locationRule: Int) : V1BoardGroupAction
    data class StopTrust(val group: V1BoardGroupSummary) : V1BoardGroupAction
}
