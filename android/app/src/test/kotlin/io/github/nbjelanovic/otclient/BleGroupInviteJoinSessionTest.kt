package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** F8 live Invite and Join on the protected profile 5 group lane. */
class BleGroupInviteJoinSessionTest {
    private val wire = GroupRosterResponseFixture
    private val inviteId = ByteArray(16) { (0xa0 + it).toByte() }
    private val label = "Crew"

    private fun descriptor(group: String = "f102030405060708", admin: String = "23".repeat(32), id: ByteArray = inviteId) = hex(
        "4f54475101010200" + admin + group + "00000001" +
            "86c78b57b4e887d5a5358af1e7e8bd778b5540b8bd37f9f67fc3db0d9e8ff105" +
            id.joinToString("") { "%02x".format(it) } + "0101" + "04" + "43726577" + "5c".repeat(64),
    )

    @Test
    fun inviteReadsTheSignedInviteBackThenConfirmsTheRoster() {
        val h = Harness(nowMs = 5_000L)
        val group = h.oneGroup()
        assertTrue(h.session.submit(V1BoardGroupAction.Invite(group, label)))
        h.expect(wire.message(INVITE) + wire.scope() + byteArrayOf(4) + label.encodeToByteArray())
        assertNull(h.session.state.invite)
        val invite = descriptor()
        h.deliver(wire.message(INVITE) + wire.scope() + inviteId + le16(invite.size))
        for (offset in invite.indices step 96) {
            h.expect(wire.message(READ_INVITE) + inviteId + le16(offset))
            h.deliver(wire.message(READ_INVITE) + inviteId + le16(offset) + le16(invite.size) +
                invite.copyOfRange(offset, minOf(invite.size, offset + 96)))
        }
        assertNull(h.session.state.invite)
        h.roster(0)
        (1..3).forEach(h::missing)
        val issued = assertNotNull(h.session.state.invite)
        assertEquals(wire.GROUP_ID, issued.groupId)
        assertEquals(label, issued.label)
        assertEquals(185_000L, issued.expiresAtMs)
        assertContentEquals(invite, V1InviteCodeText.decode(issued.code))
        assertTrue(assertNotNull(h.session.state.notice).contains("Invitation ready"))
        assertEquals(1, h.sent.count { it.payload[5] == INVITE.toByte() })
    }

    @Test
    fun inviteForAnotherGroupOrBrokenPagingFails() {
        val h = Harness()
        val group = h.oneGroup()
        assertTrue(h.session.submit(V1BoardGroupAction.Invite(group, label)))
        val other = descriptor(group = "f102030405060709")
        h.deliver(wire.message(INVITE) + wire.scope() + inviteId + le16(other.size))
        h.deliver(wire.message(READ_INVITE) + inviteId + le16(0) + le16(other.size) + other.copyOfRange(0, 96))
        h.deliver(wire.message(READ_INVITE) + inviteId + le16(96) + le16(other.size) + other.copyOfRange(96, other.size))
        assertNull(h.session.state.invite)
        assertTrue(assertNotNull(h.session.state.notice).contains("Couldn't create the invitation"))

        val again = h.oneGroup()
        assertTrue(h.session.submit(V1BoardGroupAction.Invite(again, label)))
        val invite = descriptor()
        h.deliver(wire.message(INVITE) + wire.scope() + inviteId + le16(invite.size))
        // A page at the wrong offset ends the transaction.
        h.deliver(wire.message(READ_INVITE) + inviteId + le16(96) + le16(invite.size) + invite.copyOfRange(96, invite.size))
        assertNull(h.session.state.invite)
        assertFalse(h.session.state.busy)
    }

    @Test
    fun inviteRefusalExplainsAdminAndSizeLimits() {
        val h = Harness()
        val group = h.oneGroup()
        assertTrue(h.session.submit(V1BoardGroupAction.Invite(group, label)))
        h.deliver(wire.message(INVITE, REFUSED))
        assertTrue(assertNotNull(h.session.state.notice).contains("Only the administrator"))
        // An empty label never reaches the board. The six-member limit is the board's (host tests).
        val sentBefore = h.sent.size
        val current = h.oneGroup()
        assertFalse(h.session.submit(V1BoardGroupAction.Invite(current, "")))
        assertEquals(sentBefore + 8, h.sent.size)  // refresh and roster reads only
    }

    @Test
    fun joinUploadsPagesInOrderAndNeedsTheMatchingScope() {
        val h = Harness()
        val invite = descriptor()
        assertFalse(h.session.submit(V1BoardGroupAction.Join(invite)))  // needs a confirmed refresh first
        h.emptyRoster()
        assertFalse(h.session.submit(V1BoardGroupAction.Join(byteArrayOf(1, 2, 3))))
        assertTrue(h.session.submit(V1BoardGroupAction.Join(invite)))
        h.expect(wire.message(JOIN) + le16(0) + le16(invite.size) + invite.copyOfRange(0, 96))
        h.deliver(wire.message(JOIN) + le16(0) + le16(invite.size))
        h.expect(wire.message(JOIN) + le16(96) + le16(invite.size) + invite.copyOfRange(96, invite.size))
        h.deliver(wire.message(JOIN) + le16(96) + le16(invite.size) + wire.scope() + inviteId)
        (0..3).forEach(h::missing)
        assertTrue(assertNotNull(h.session.state.notice).contains("accepted the invitation to Crew"))
        assertNull(h.session.state.invite)
        assertEquals(2, h.sent.count { it.payload[5] == JOIN.toByte() })
    }

    @Test
    fun joinWithAMismatchedReceiptOrStaleInviteFails() {
        val h = Harness()
        h.emptyRoster()
        val invite = descriptor()
        assertTrue(h.session.submit(V1BoardGroupAction.Join(invite)))
        h.deliver(wire.message(JOIN) + le16(0) + le16(invite.size))
        val otherScope = wire.scope().also { it[0] = 0x24 }
        h.deliver(wire.message(JOIN) + le16(96) + le16(invite.size) + otherScope + inviteId)
        assertTrue(assertNotNull(h.session.state.notice).contains("Couldn't confirm the invitation"))

        h.emptyRoster()
        assertTrue(h.session.submit(V1BoardGroupAction.Join(invite)))
        h.deliver(wire.message(JOIN, STALE))
        assertTrue(assertNotNull(h.session.state.notice).contains("already used"))
        assertEquals(3, h.sent.count { it.payload[5] == JOIN.toByte() })
    }

    @Test
    fun inviteCodeTextRoundTripsAndRejectsOtherText() {
        val invite = descriptor()
        val code = V1InviteCodeText.encode(invite)
        assertTrue(code.startsWith("MESHTRAIL-INVITE-1:"))
        assertContentEquals(invite, V1InviteCodeText.decode("  $code\n"))
        assertNull(V1InviteCodeText.decode(code.removePrefix("MESHTRAIL-INVITE-1:")))
        assertNull(V1InviteCodeText.decode(code + "="))
        assertNull(V1InviteCodeText.decode(code.replace('A', '+')))
        assertNull(V1InviteCodeText.decode("MESHTRAIL-INVITE-1:"))
        assertNull(V1InviteCodeText.decode(V1InviteCodeText.encode(ByteArray(288))))
    }

    private inner class Harness(nowMs: Long = 0L) {
        inner class Sent(val exchange: UInt, val payload: ByteArray)
        private var nextExchange = 1u
        val sent = mutableListOf<Sent>()
        val last get() = sent.last()
        val session = BleGroupRosterSession(
            isCurrent = { true },
            allocate = { nextExchange++ },
            send = { exchange, payload -> sent.add(Sent(exchange, payload)) },
            changed = {},
            now = { nowMs },
        )

        fun expect(payload: ByteArray) = assertContentEquals(payload, last.payload)
        fun deliver(payload: ByteArray) = assertTrue(session.receive(last.exchange, payload))

        fun refresh() {
            assertTrue(session.submit(V1BoardGroupAction.Refresh))
            deliver(wire.message(wire.DISCOVER))
        }

        fun emptyRoster() {
            refresh()
            (0..3).forEach(::missing)
        }

        fun oneGroup(): V1BoardGroupSummary {
            refresh()
            roster(0)
            (1..3).forEach(::missing)
            return session.state.groups.single()
        }

        fun missing(slot: Int) {
            expect(wire.readRequest(slot))
            deliver(wire.message(wire.READ_ROSTER, wire.MISSING))
        }

        fun roster(slot: Int) {
            val bytes = wire.rosterBytes()
            for (offset in bytes.indices step 42) {
                expect(wire.readRequest(slot, offset))
                deliver(wire.rosterResult(slot, offset, bytes = bytes))
            }
        }
    }

    private fun le16(value: Int) = byteArrayOf(value.toByte(), (value ushr 8).toByte())
    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()

    private companion object {
        const val INVITE = 5
        const val READ_INVITE = 6
        const val JOIN = 7
        const val REFUSED = 2
        const val STALE = 3
    }
}
