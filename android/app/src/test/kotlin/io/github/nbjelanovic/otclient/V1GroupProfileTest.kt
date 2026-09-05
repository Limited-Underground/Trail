package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1GroupProfileTest {
    @Test
    fun groupRequiresExactlyOneMatchingAdministratorAndAtMostSixMembers() {
        val admin = member(1, "Brian", administrator = true)
        assertNotNull(V1GroupProfile.create(admin.token, V1GroupLocationRule.REQUIRED, listOf(admin)))
        assertNull(V1GroupProfile.create(admin.token, V1GroupLocationRule.REQUIRED, emptyList()))
        assertNull(V1GroupProfile.create(
            admin.token,
            V1GroupLocationRule.REQUIRED,
            listOf(admin, member(2, "Alex", administrator = true)),
        ))
        assertNull(V1GroupProfile.create(
            admin.token,
            V1GroupLocationRule.REQUIRED,
            listOf(admin) + (2..7).map { member(it, "Member $it") },
        ))
    }

    @Test
    fun joiningAlwaysStartsLocationSharingOnForEitherAdminRule() {
        V1GroupLocationRule.entries.forEach { rule ->
            val admin = member(1, "Brian", administrator = true)
            val group = requireNotNull(V1GroupProfile.create(admin.token, rule, listOf(admin)))
            val proposed = member(2, "Alex", location = false)
            val accepted = assertIs<V1GroupMutationResult.Accepted>(group.addMember(admin.token, proposed))
            assertTrue(accepted.group.members.single { it.token == proposed.token }.locationSharingEnabled)
        }
    }

    @Test
    fun onlyAdministratorMayAddRemoveOrChangeLocationRule() {
        val admin = member(1, "Brian", administrator = true)
        val alex = member(2, "Alex")
        val group = requireNotNull(
            V1GroupProfile.create(admin.token, V1GroupLocationRule.MEMBER_OPTIONAL, listOf(admin, alex)),
        )
        assertRejected(V1GroupMutationFailure.NON_ADMINISTRATOR, group.addMember(alex.token, member(3, "Jamie")))
        assertRejected(V1GroupMutationFailure.NON_ADMINISTRATOR, group.removeMember(alex.token, admin.token))
        assertRejected(
            V1GroupMutationFailure.NON_ADMINISTRATOR,
            group.setLocationRule(alex.token, V1GroupLocationRule.REQUIRED),
        )
    }

    @Test
    fun groupRejectsSeventhMemberDuplicateAndSecondAdministrator() {
        val admin = member(1, "Brian", administrator = true)
        var group = requireNotNull(V1GroupProfile.create(admin.token, V1GroupLocationRule.REQUIRED, listOf(admin)))
        for (index in 2..6) {
            group = assertIs<V1GroupMutationResult.Accepted>(
                group.addMember(admin.token, member(index, "Member $index")),
            ).group
        }
        assertEquals(V1_MAX_GROUP_MEMBERS, group.members.size)
        assertRejected(V1GroupMutationFailure.GROUP_FULL, group.addMember(admin.token, member(7, "Member 7")))
        assertRejected(V1GroupMutationFailure.DUPLICATE_MEMBER, group.addMember(admin.token, member(6, "Again")))
        assertRejected(
            V1GroupMutationFailure.INVALID_MEMBER,
            group.addMember(admin.token, member(8, "Other admin", administrator = true)),
        )
    }

    @Test
    fun administratorCannotLeaveOrBeRemovedButOrdinaryMemberCan() {
        val admin = member(1, "Brian", administrator = true)
        val alex = member(2, "Alex")
        val group = requireNotNull(
            V1GroupProfile.create(admin.token, V1GroupLocationRule.REQUIRED, listOf(admin, alex)),
        )
        assertRejected(V1GroupMutationFailure.ADMINISTRATOR_CANNOT_LEAVE, group.leave(admin.token))
        assertRejected(V1GroupMutationFailure.ADMINISTRATOR_CANNOT_BE_REMOVED, group.removeMember(admin.token, admin.token))
        val left = assertIs<V1GroupMutationResult.Accepted>(group.leave(alex.token))
        assertEquals(listOf(admin.token), left.group.members.map { it.token })
    }

    @Test
    fun requiredRuleForcesLocationOnAndOptionalRuleAllowsMemberToStop() {
        val admin = member(1, "Brian", administrator = true)
        val alex = member(2, "Alex")
        val optional = requireNotNull(
            V1GroupProfile.create(admin.token, V1GroupLocationRule.MEMBER_OPTIONAL, listOf(admin, alex)),
        )
        val stopped = assertIs<V1GroupMutationResult.Accepted>(
            optional.setMemberLocationSharing(alex.token, alex.token, false),
        ).group
        assertFalse(stopped.members.single { it.token == alex.token }.locationSharingEnabled)
        assertRejected(
            V1GroupMutationFailure.LOCATION_RECONSENT_REQUIRED,
            stopped.setLocationRule(admin.token, V1GroupLocationRule.REQUIRED),
        )
        assertRejected(
            V1GroupMutationFailure.MEMBER_MAY_ONLY_CHANGE_SELF,
            stopped.setMemberLocationSharing(admin.token, alex.token, true),
        )
        val reconsented = assertIs<V1GroupMutationResult.Accepted>(
            stopped.setMemberLocationSharing(alex.token, alex.token, true),
        ).group
        val required = assertIs<V1GroupMutationResult.Accepted>(
            reconsented.setLocationRule(admin.token, V1GroupLocationRule.REQUIRED),
        ).group
        assertTrue(required.members.all { it.locationSharingEnabled })
        assertRejected(
            V1GroupMutationFailure.INVALID_MEMBER,
            required.setMemberLocationSharing(alex.token, alex.token, false),
        )
    }

    @Test
    fun tokensAndModelStringsDoNotExposePrivateBytes() {
        val admin = member(42, "Brian", administrator = true)
        val group = requireNotNull(V1GroupProfile.create(admin.token, V1GroupLocationRule.REQUIRED, listOf(admin)))
        assertEquals("V1GroupMemberToken(redacted)", admin.token.toString())
        assertFalse(admin.toString().contains("42"))
        assertFalse(group.toString().contains("42"))
        assertTrue(group.toString().contains("memberCount=1"))
    }

    @Test
    fun constructorsCopyAndRejectInvalidPrivateTokensAndNames() {
        assertNull(V1GroupMemberToken.create(ByteArray(V1GroupMemberToken.SIZE_BYTES)))
        assertNull(V1GroupMemberToken.create(ByteArray(V1GroupMemberToken.SIZE_BYTES - 1) { 1 }))
        val bytes = ByteArray(V1GroupMemberToken.SIZE_BYTES) { 3 }
        val token = assertNotNull(V1GroupMemberToken.create(bytes))
        bytes.fill(9)
        assertEquals(token, V1GroupMemberToken.create(ByteArray(V1GroupMemberToken.SIZE_BYTES) { 3 }))
        assertNull(V1GroupMember.create(token, " bad"))
    }

    private fun member(
        marker: Int,
        name: String,
        administrator: Boolean = false,
        location: Boolean = true,
    ): V1GroupMember {
        val token = requireNotNull(
            V1GroupMemberToken.create(ByteArray(V1GroupMemberToken.SIZE_BYTES) { marker.toByte() }),
        )
        return requireNotNull(V1GroupMember.create(token, name, administrator, location))
    }

    private fun assertRejected(expected: V1GroupMutationFailure, result: V1GroupMutationResult) {
        assertEquals(expected, assertIs<V1GroupMutationResult.Rejected>(result).failure)
    }
}
