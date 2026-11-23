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
    fun joiningHonorsTheExactRuleChoiceAndRequiredFix() {
        val admin = member(1, "Brian", administrator = true, location = false)
        // The old member flag cannot substitute for an explicit, current choice.
        val joining = member(2, "Alex", location = true)
        for (rule in V1GroupLocationRule.entries) {
            val group = assertNotNull(V1GroupProfile.create(admin.token, rule, listOf(admin)))
            for (choice in V1GroupJoinChoice.entries) {
                for (fresh in listOf(false, true)) {
                    val expected = when (rule) {
                        V1GroupLocationRule.OFF -> choice == V1GroupJoinChoice.WITHOUT_LOCATION
                        V1GroupLocationRule.MEMBER_OPTIONAL -> true
                        V1GroupLocationRule.REQUIRED -> choice == V1GroupJoinChoice.SHARE_LOCATION && fresh
                    }
                    val result = group.addMember(admin.token, joining, choice, fresh)
                    if (expected) {
                        val updated = assertIs<V1GroupMutationResult.Accepted>(result).group
                        assertEquals(choice == V1GroupJoinChoice.SHARE_LOCATION,
                            updated.members.single { it.token == joining.token }.locationSharingEnabled)
                    } else {
                        assertRejected(V1GroupMutationFailure.LOCATION_CONSENT_OR_FIX_REQUIRED, result)
                        assertEquals(listOf(admin), group.members)
                    }
                }
            }
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
        var group = requireNotNull(V1GroupProfile.create(admin.token, V1GroupLocationRule.MEMBER_OPTIONAL, listOf(admin)))
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
    fun changingRuleAndWithdrawingConsentPauseRequiredParticipationWithoutRemovingAnyone() {
        val admin = member(1, "Brian", administrator = true)
        val alex = member(2, "Alex")
        val preferred = assertNotNull(
            V1GroupProfile.create(admin.token, V1GroupLocationRule.MEMBER_OPTIONAL, listOf(admin, alex)),
        )
        val required = assertIs<V1GroupMutationResult.Accepted>(
            preferred.setLocationRule(admin.token, V1GroupLocationRule.REQUIRED),
        ).group
        assertEquals(listOf(admin.token, alex.token), required.members.map { it.token })
        assertTrue(required.members.none { it.locationSharingEnabled })
        assertFalse(required.memberCanParticipate(alex.token))
        assertRejected(V1GroupMutationFailure.MEMBER_MAY_ONLY_CHANGE_SELF,
            required.setMemberLocationSharing(admin.token, alex.token, true))

        val agreed = assertIs<V1GroupMutationResult.Accepted>(
            required.setMemberLocationSharing(alex.token, alex.token, true),
        ).group
        assertTrue(agreed.memberCanParticipate(alex.token))
        val stopped = assertIs<V1GroupMutationResult.Accepted>(
            agreed.setMemberLocationSharing(alex.token, alex.token, false),
        ).group
        assertEquals(listOf(admin.token, alex.token), stopped.members.map { it.token })
        assertFalse(stopped.memberCanParticipate(alex.token))

        val disabled = assertIs<V1GroupMutationResult.Accepted>(
            agreed.setLocationRule(admin.token, V1GroupLocationRule.OFF),
        ).group
        assertTrue(disabled.members.none { it.locationSharingEnabled })
        assertTrue(disabled.memberCanParticipate(alex.token))
        assertRejected(V1GroupMutationFailure.LOCATION_DISABLED,
            disabled.setMemberLocationSharing(alex.token, alex.token, true))
    }

    @Test
    fun audienceChangesClearOlderConsentButPreserveTheMatchingJoinChoice() {
        val admin = member(1, "Brian", administrator = true)
        val alex = member(2, "Alex")
        val preferred = assertNotNull(
            V1GroupProfile.create(admin.token, V1GroupLocationRule.MEMBER_OPTIONAL, listOf(admin, alex)),
        )
        val joining = member(3, "Casey", location = false)
        val expanded = assertIs<V1GroupMutationResult.Accepted>(
            preferred.addMember(admin.token, joining, V1GroupJoinChoice.SHARE_LOCATION),
        ).group
        assertEquals(listOf(false, false, true), expanded.members.map { it.locationSharingEnabled })
        val reduced = assertIs<V1GroupMutationResult.Accepted>(expanded.leave(alex.token)).group
        assertEquals(listOf(admin.token, joining.token), reduced.members.map { it.token })
        assertTrue(reduced.members.none { it.locationSharingEnabled })
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
