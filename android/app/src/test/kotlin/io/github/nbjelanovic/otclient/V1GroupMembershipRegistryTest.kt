package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class V1GroupMembershipRegistryTest {
    @Test
    fun localPersonCanBelongToOnlyOneGroup() {
        val admin = member(1, "Brian", true)
        val first = accept(V1GroupMembershipRegistry.empty(admin.token).join(groupToken(7), group(admin))).registry
        reject(V1GroupRegistryFailure.ALREADY_IN_GROUP, first.join(groupToken(8), group(admin)))
    }

    @Test
    fun profileMustAlreadyContainTheJoiningPerson() {
        val admin = member(1, "Brian", true)
        val outsider = member(2, "Alex")
        reject(
            V1GroupRegistryFailure.NOT_A_MEMBER,
            V1GroupMembershipRegistry.empty(outsider.token).join(groupToken(7), group(admin)),
        )
    }

    @Test
    fun joiningCannotBypassTheLocationOnDefault() {
        val admin = member(1, "Brian", true)
        val alexOff = member(2, "Alex", location = false)
        val optional = requireNotNull(
            V1GroupProfile.create(
                admin.token,
                V1GroupLocationRule.MEMBER_OPTIONAL,
                listOf(admin, alexOff),
            ),
        )
        reject(
            V1GroupRegistryFailure.LOCATION_MUST_START_ON,
            V1GroupMembershipRegistry.empty(alexOff.token).join(groupToken(7), optional),
        )
    }

    @Test
    fun ordinaryMemberMayLeaveAndReturnsUpdatedRemainingGroup() {
        val admin = member(1, "Brian", true)
        val alex = member(2, "Alex")
        val profile = group(admin, alex)
        val joined = accept(V1GroupMembershipRegistry.empty(alex.token).join(groupToken(7), profile)).registry
        val left = accept(joined.leave())
        assertNull(left.registry.active)
        assertEquals(listOf(admin.token), assertNotNull(left.remainingGroup).members.map { it.token })
    }

    @Test
    fun administratorMustConfirmExactGroupDeletionBeforeLeaving() {
        val admin = member(1, "Brian", true)
        val token = groupToken(7)
        val joined = accept(V1GroupMembershipRegistry.empty(admin.token).join(token, group(admin))).registry
        reject(V1GroupRegistryFailure.ADMINISTRATOR_MUST_DELETE, joined.leave())
        val otherIssued = issueDelete(
            accept(V1GroupMembershipRegistry.empty(admin.token).join(groupToken(8), group(admin))).registry,
            admin.token,
            100,
        )
        reject(
            V1GroupRegistryFailure.STALE_DELETE_CONFIRMATION,
            joined.deleteGroup(admin.token, otherIssued.confirmation, 101),
        )
        val issued = issueDelete(joined, admin.token, 100)
        reject(
            V1GroupRegistryFailure.STALE_DELETE_CONFIRMATION,
            issued.registry.deleteGroup(
                admin.token,
                issued.confirmation,
                100 + V1_GROUP_DELETE_CONFIRMATION_MILLIS,
            ),
        )
        val fresh = issueDelete(joined, admin.token, 200)
        val deleted = accept(fresh.registry.deleteGroup(admin.token, fresh.confirmation, 201))
        assertNull(deleted.registry.active)
        assertNull(deleted.remainingGroup)
    }

    @Test
    fun registryAndConfirmationStringsRedactPrivateTokens() {
        val admin = member(1, "Brian", true)
        val token = groupToken(7)
        val registry = accept(V1GroupMembershipRegistry.empty(admin.token).join(token, group(admin))).registry
        assertFalse(registry.toString().contains("Brian"))
        assertFalse(issueDelete(registry, admin.token, 100).confirmation.toString().contains("7"))
    }

    @Test
    fun newerSameGroupDeleteConfirmationInvalidatesTheEarlierOne() {
        val admin = member(1, "Brian", true)
        val joined = accept(
            V1GroupMembershipRegistry.empty(admin.token).join(groupToken(7), group(admin)),
        ).registry
        val first = issueDelete(joined, admin.token, 100)
        val second = assertIs<V1GroupDeleteBeginResult.Issued>(
            first.registry.beginDeleteConfirmation(
                admin.token,
                200,
                ByteArray(V1GroupDeleteConfirmation.NONCE_SIZE_BYTES) { 8 },
            ),
        )
        reject(
            V1GroupRegistryFailure.STALE_DELETE_CONFIRMATION,
            second.registry.deleteGroup(admin.token, first.confirmation, 201),
        )
        assertNull(accept(second.registry.deleteGroup(admin.token, second.confirmation, 201)).registry.active)
    }

    private fun group(admin: V1GroupMember, vararg others: V1GroupMember): V1GroupProfile =
        requireNotNull(V1GroupProfile.create(admin.token, V1GroupLocationRule.REQUIRED, listOf(admin) + others))

    private fun member(
        marker: Int,
        name: String,
        administrator: Boolean = false,
        location: Boolean = true,
    ): V1GroupMember {
        val token = requireNotNull(V1GroupMemberToken.create(ByteArray(V1GroupMemberToken.SIZE_BYTES) { marker.toByte() }))
        return requireNotNull(V1GroupMember.create(token, name, administrator, location))
    }

    private fun groupToken(marker: Int) =
        requireNotNull(V1GroupToken.create(ByteArray(V1GroupToken.SIZE_BYTES) { marker.toByte() }))

    private fun issueDelete(
        registry: V1GroupMembershipRegistry,
        admin: V1GroupMemberToken,
        now: Long,
    ) = assertIs<V1GroupDeleteBeginResult.Issued>(
        registry.beginDeleteConfirmation(
            admin,
            now,
            ByteArray(V1GroupDeleteConfirmation.NONCE_SIZE_BYTES) { 9 },
        )
    )

    private fun accept(result: V1GroupRegistryResult) = assertIs<V1GroupRegistryResult.Accepted>(result)

    private fun reject(expected: V1GroupRegistryFailure, result: V1GroupRegistryResult) {
        assertEquals(expected, assertIs<V1GroupRegistryResult.Rejected>(result).failure)
    }
}
