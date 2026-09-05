package io.github.nbjelanovic.otclient

enum class V1GroupRegistryFailure {
    ALREADY_IN_GROUP,
    NOT_A_MEMBER,
    LOCATION_MUST_START_ON,
    NO_ACTIVE_GROUP,
    ADMINISTRATOR_MUST_DELETE,
    NON_ADMINISTRATOR,
    STALE_DELETE_CONFIRMATION,
}

internal const val V1_GROUP_DELETE_CONFIRMATION_MILLIS = 30_000L

class V1GroupToken private constructor(private val bytes: ByteArray) {
    override fun equals(other: Any?): Boolean = other is V1GroupToken && bytes.contentEquals(other.bytes)
    override fun hashCode(): Int = bytes.contentHashCode()
    override fun toString(): String = "V1GroupToken(redacted)"

    companion object {
        const val SIZE_BYTES = 16

        fun create(value: ByteArray): V1GroupToken? =
            value.takeIf { it.size == SIZE_BYTES && it.any { byte -> byte.toInt() != 0 } }
                ?.copyOf()
                ?.let(::V1GroupToken)
    }
}

class V1GroupDeleteConfirmation private constructor(
    internal val groupToken: V1GroupToken,
    internal val administratorToken: V1GroupMemberToken,
    private val nonce: ByteArray,
) {
    override fun toString(): String = "V1GroupDeleteConfirmation(redacted)"

    internal companion object {
        const val NONCE_SIZE_BYTES = 16

        fun issue(
            groupToken: V1GroupToken,
            administratorToken: V1GroupMemberToken,
            nonce: ByteArray,
        ): V1GroupDeleteConfirmation? =
            nonce.takeIf { it.size == NONCE_SIZE_BYTES && it.any { byte -> byte.toInt() != 0 } }
                ?.copyOf()
                ?.let { V1GroupDeleteConfirmation(groupToken, administratorToken, it) }
    }
}

private data class V1PendingGroupDelete(
    val confirmation: V1GroupDeleteConfirmation,
    val issuedAtElapsedMillis: Long,
    val expiresAtElapsedMillis: Long,
)

data class V1ActiveGroupMembership(
    val groupToken: V1GroupToken,
    val profile: V1GroupProfile,
)

sealed interface V1GroupRegistryResult {
    data class Accepted(
        val registry: V1GroupMembershipRegistry,
        val remainingGroup: V1GroupProfile?,
    ) : V1GroupRegistryResult

    data class Rejected(val failure: V1GroupRegistryFailure) : V1GroupRegistryResult
}

sealed interface V1GroupDeleteBeginResult {
    data class Issued(
        val registry: V1GroupMembershipRegistry,
        val confirmation: V1GroupDeleteConfirmation,
    ) : V1GroupDeleteBeginResult

    data class Rejected(val failure: V1GroupRegistryFailure) : V1GroupDeleteBeginResult
}

class V1GroupMembershipRegistry private constructor(
    val localMemberToken: V1GroupMemberToken,
    val active: V1ActiveGroupMembership?,
    private val pendingDelete: V1PendingGroupDelete?,
) {
    fun join(groupToken: V1GroupToken, profile: V1GroupProfile): V1GroupRegistryResult {
        if (active != null) return reject(V1GroupRegistryFailure.ALREADY_IN_GROUP)
        if (profile.members.none { it.token == localMemberToken }) {
            return reject(V1GroupRegistryFailure.NOT_A_MEMBER)
        }
        if (!profile.members.single { it.token == localMemberToken }.locationSharingEnabled) {
            return reject(V1GroupRegistryFailure.LOCATION_MUST_START_ON)
        }
        return accept(V1ActiveGroupMembership(groupToken, profile), profile)
    }

    fun leave(): V1GroupRegistryResult {
        val membership = active ?: return reject(V1GroupRegistryFailure.NO_ACTIVE_GROUP)
        if (membership.profile.administratorToken == localMemberToken) {
            return reject(V1GroupRegistryFailure.ADMINISTRATOR_MUST_DELETE)
        }
        val remaining = when (val result = membership.profile.leave(localMemberToken)) {
            is V1GroupMutationResult.Accepted -> result.group
            is V1GroupMutationResult.Rejected -> return reject(V1GroupRegistryFailure.NOT_A_MEMBER)
        }
        return accept(null, remaining)
    }

    fun deleteGroup(
        actor: V1GroupMemberToken,
        confirmation: V1GroupDeleteConfirmation,
        nowElapsedMillis: Long,
    ): V1GroupRegistryResult {
        val membership = active ?: return reject(V1GroupRegistryFailure.NO_ACTIVE_GROUP)
        if (actor != membership.profile.administratorToken || actor != localMemberToken) {
            return reject(V1GroupRegistryFailure.NON_ADMINISTRATOR)
        }
        val pending = pendingDelete
        if (nowElapsedMillis < 0 ||
            pending == null ||
            confirmation !== pending.confirmation ||
            confirmation.groupToken != membership.groupToken ||
            confirmation.administratorToken != actor ||
            nowElapsedMillis < pending.issuedAtElapsedMillis ||
            nowElapsedMillis >= pending.expiresAtElapsedMillis
        ) {
            return reject(V1GroupRegistryFailure.STALE_DELETE_CONFIRMATION)
        }
        return accept(null, null)
    }

    fun beginDeleteConfirmation(
        actor: V1GroupMemberToken,
        nowElapsedMillis: Long,
        nonce: ByteArray,
    ): V1GroupDeleteBeginResult {
        val membership = active
            ?: return V1GroupDeleteBeginResult.Rejected(V1GroupRegistryFailure.NO_ACTIVE_GROUP)
        if (actor != membership.profile.administratorToken || actor != localMemberToken) {
            return V1GroupDeleteBeginResult.Rejected(V1GroupRegistryFailure.NON_ADMINISTRATOR)
        }
        if (nowElapsedMillis < 0 ||
            nowElapsedMillis > Long.MAX_VALUE - V1_GROUP_DELETE_CONFIRMATION_MILLIS
        ) {
            return V1GroupDeleteBeginResult.Rejected(V1GroupRegistryFailure.STALE_DELETE_CONFIRMATION)
        }
        val confirmation = V1GroupDeleteConfirmation.issue(membership.groupToken, actor, nonce)
            ?: return V1GroupDeleteBeginResult.Rejected(V1GroupRegistryFailure.STALE_DELETE_CONFIRMATION)
        val pending = V1PendingGroupDelete(
            confirmation,
            nowElapsedMillis,
            nowElapsedMillis + V1_GROUP_DELETE_CONFIRMATION_MILLIS,
        )
        return V1GroupDeleteBeginResult.Issued(
            V1GroupMembershipRegistry(localMemberToken, membership, pending),
            confirmation,
        )
    }

    override fun toString(): String =
        "V1GroupMembershipRegistry(localMemberToken=redacted, active=${active != null})"

    private fun accept(active: V1ActiveGroupMembership?, remaining: V1GroupProfile?) =
        V1GroupRegistryResult.Accepted(V1GroupMembershipRegistry(localMemberToken, active, null), remaining)

    private fun reject(failure: V1GroupRegistryFailure) = V1GroupRegistryResult.Rejected(failure)

    companion object {
        fun empty(localMemberToken: V1GroupMemberToken): V1GroupMembershipRegistry =
            V1GroupMembershipRegistry(localMemberToken, null, null)
    }
}
