package io.github.nbjelanovic.otclient

internal const val V1_MAX_GROUP_MEMBERS = 6
internal const val V1_ADMINISTRATOR_COUNT = 1

enum class V1GroupLocationRule {
    REQUIRED,
    MEMBER_OPTIONAL,
}

enum class V1GroupMutationFailure {
    INVALID_MEMBER,
    DUPLICATE_MEMBER,
    GROUP_FULL,
    ADMINISTRATOR_CANNOT_LEAVE,
    ADMINISTRATOR_CANNOT_BE_REMOVED,
    NON_ADMINISTRATOR,
    MEMBER_MAY_ONLY_CHANGE_SELF,
    LOCATION_RECONSENT_REQUIRED,
}

class V1GroupMemberToken private constructor(private val bytes: ByteArray) {
    override fun equals(other: Any?): Boolean =
        other is V1GroupMemberToken && bytes.contentEquals(other.bytes)

    override fun hashCode(): Int = bytes.contentHashCode()
    override fun toString(): String = "V1GroupMemberToken(redacted)"

    companion object {
        const val SIZE_BYTES = 16

        fun create(value: ByteArray): V1GroupMemberToken? =
            value.takeIf { it.size == SIZE_BYTES && it.any { byte -> byte.toInt() != 0 } }
                ?.copyOf()
                ?.let(::V1GroupMemberToken)
    }
}

class V1GroupMember private constructor(
    val token: V1GroupMemberToken,
    val displayName: StablePeerDisplayAlias,
    val isAdministrator: Boolean,
    val locationSharingEnabled: Boolean,
) {
    override fun equals(other: Any?): Boolean =
        other is V1GroupMember &&
            token == other.token &&
            displayName == other.displayName &&
            isAdministrator == other.isAdministrator &&
            locationSharingEnabled == other.locationSharingEnabled

    override fun hashCode(): Int {
        var result = token.hashCode()
        result = 31 * result + displayName.hashCode()
        result = 31 * result + isAdministrator.hashCode()
        return 31 * result + locationSharingEnabled.hashCode()
    }

    override fun toString(): String =
        "V1GroupMember(token=redacted, displayName=$displayName, " +
            "isAdministrator=$isAdministrator, locationSharingEnabled=$locationSharingEnabled)"

    companion object {
        fun create(
            token: V1GroupMemberToken,
            displayName: String,
            isAdministrator: Boolean = false,
            locationSharingEnabled: Boolean = true,
        ): V1GroupMember? =
            StablePeerDisplayAlias.create(displayName)?.let {
                V1GroupMember(token, it, isAdministrator, locationSharingEnabled)
            }
    }
}

sealed interface V1GroupMutationResult {
    data class Accepted(val group: V1GroupProfile) : V1GroupMutationResult
    data class Rejected(val failure: V1GroupMutationFailure) : V1GroupMutationResult
}

class V1GroupProfile private constructor(
    val administratorToken: V1GroupMemberToken,
    val locationRule: V1GroupLocationRule,
    members: List<V1GroupMember>,
) {
    val members: List<V1GroupMember> = members.toList()

    fun addMember(actor: V1GroupMemberToken, member: V1GroupMember): V1GroupMutationResult {
        if (actor != administratorToken) return reject(V1GroupMutationFailure.NON_ADMINISTRATOR)
        if (member.isAdministrator) return reject(V1GroupMutationFailure.INVALID_MEMBER)
        if (members.any { it.token == member.token }) return reject(V1GroupMutationFailure.DUPLICATE_MEMBER)
        if (members.size >= V1_MAX_GROUP_MEMBERS) return reject(V1GroupMutationFailure.GROUP_FULL)
        return accept(locationRule, members + member.withLocationSharing(true))
    }

    fun removeMember(actor: V1GroupMemberToken, member: V1GroupMemberToken): V1GroupMutationResult {
        if (actor != administratorToken) return reject(V1GroupMutationFailure.NON_ADMINISTRATOR)
        if (member == administratorToken) return reject(V1GroupMutationFailure.ADMINISTRATOR_CANNOT_BE_REMOVED)
        if (members.none { it.token == member }) return reject(V1GroupMutationFailure.INVALID_MEMBER)
        return accept(locationRule, members.filterNot { it.token == member })
    }

    fun leave(member: V1GroupMemberToken): V1GroupMutationResult {
        if (member == administratorToken) return reject(V1GroupMutationFailure.ADMINISTRATOR_CANNOT_LEAVE)
        if (members.none { it.token == member }) return reject(V1GroupMutationFailure.INVALID_MEMBER)
        return accept(locationRule, members.filterNot { it.token == member })
    }

    fun setLocationRule(actor: V1GroupMemberToken, rule: V1GroupLocationRule): V1GroupMutationResult {
        if (actor != administratorToken) return reject(V1GroupMutationFailure.NON_ADMINISTRATOR)
        if (rule == V1GroupLocationRule.REQUIRED && members.any { !it.locationSharingEnabled }) {
            return reject(V1GroupMutationFailure.LOCATION_RECONSENT_REQUIRED)
        }
        return accept(rule, members)
    }

    fun setMemberLocationSharing(
        actor: V1GroupMemberToken,
        member: V1GroupMemberToken,
        enabled: Boolean,
    ): V1GroupMutationResult {
        if (members.none { it.token == member }) return reject(V1GroupMutationFailure.INVALID_MEMBER)
        if (actor != member) return reject(V1GroupMutationFailure.MEMBER_MAY_ONLY_CHANGE_SELF)
        if (!enabled && locationRule == V1GroupLocationRule.REQUIRED) {
            return reject(V1GroupMutationFailure.INVALID_MEMBER)
        }
        return accept(
            locationRule,
            members.map { if (it.token == member) it.withLocationSharing(enabled) else it },
        )
    }

    override fun toString(): String =
        "V1GroupProfile(administratorToken=redacted, locationRule=$locationRule, memberCount=${members.size})"

    private fun accept(rule: V1GroupLocationRule, nextMembers: List<V1GroupMember>): V1GroupMutationResult =
        create(administratorToken, rule, nextMembers)
            ?.let(V1GroupMutationResult::Accepted)
            ?: reject(V1GroupMutationFailure.INVALID_MEMBER)

    private fun reject(failure: V1GroupMutationFailure) = V1GroupMutationResult.Rejected(failure)

    companion object {
        fun create(
            administratorToken: V1GroupMemberToken,
            locationRule: V1GroupLocationRule,
            members: List<V1GroupMember>,
        ): V1GroupProfile? {
            if (members.size !in 1..V1_MAX_GROUP_MEMBERS) return null
            if (members.map { it.token }.distinct().size != members.size) return null
            val administrators = members.filter { it.isAdministrator }
            if (administrators.size != V1_ADMINISTRATOR_COUNT ||
                administrators.single().token != administratorToken
            ) return null
            if (locationRule == V1GroupLocationRule.REQUIRED &&
                members.any { !it.locationSharingEnabled }
            ) return null
            return V1GroupProfile(administratorToken, locationRule, members)
        }
    }
}

private fun V1GroupMember.withLocationSharing(enabled: Boolean): V1GroupMember =
    requireNotNull(
        V1GroupMember.create(token, displayName.value, isAdministrator, enabled),
    )
