package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class BleGroupRosterSessionTest {
    private val wire = GroupRosterResponseFixture

    @Test
    fun discoverAndFourSlotReadPublishOnlyTheCompleteCorrelatedRoster() {
        val h = Harness()
        assertTrue(h.session.submit(V1BoardGroupAction.Refresh))
        h.expect(wire.message(wire.DISCOVER))
        assertFalse(h.session.receive(h.last.exchange + 1u, wire.message(wire.DISCOVER)))
        assertEquals(1, h.sent.size)

        h.deliver(wire.message(wire.DISCOVER))
        h.missing(0)
        h.roster(1)
        assertTrue(h.session.state.busy)
        assertTrue(h.session.state.groups.isEmpty())
        h.missing(2)
        h.missing(3)

        val group = h.session.state.groups.single()
        assertEquals(1, group.slot)
        assertEquals(wire.GROUP_ID, group.groupId)
        assertEquals(1u, group.version)
        assertEquals(1, group.locationRule)
        assertEquals(listOf(V1BoardGroupMember("Admin", true)), group.members)
        assertFalse(group.distrusted)
        assertTrue(h.session.state.available)
        assertTrue(h.session.state.canCreate)
        assertFalse(h.session.state.busy)
    }

    @Test
    fun createNeedsDiscoverAndPublishesSuccessOnlyAfterRosterReadback() {
        val h = Harness()
        val create = V1BoardGroupAction.Create(locationRule = 1)
        assertFalse(h.session.submit(create))
        assertTrue(h.sent.isEmpty())
        h.emptyRoster()
        assertFalse(h.session.submit(V1BoardGroupAction.Create(locationRule = 3)))
        assertTrue(h.session.submit(create))
        h.expect(wire.message(wire.CREATE) + 1.toByte())
        h.deliver(wire.message(wire.CREATE) + wire.scope())
        assertNull(h.session.state.notice)
        assertTrue(h.session.state.groups.isEmpty())

        h.roster(0)
        h.missing(1)
        h.missing(2)
        assertTrue(h.session.state.groups.isEmpty())
        h.missing(3)
        assertEquals(wire.GROUP_ID, h.session.state.groups.single().groupId)
        assertTrue(assertNotNull(h.session.state.notice).contains("Group created"))
        assertEquals(1, h.sent.count { it.payload[5] == wire.CREATE.toByte() })
    }

    @Test
    fun stopTrustRequiresTheCurrentScopeAndReadbackOfDistrust() {
        val h = Harness()
        h.refresh()
        h.roster(0)
        (1..3).forEach(h::missing)
        val old = h.session.state.groups.single()

        h.refresh()
        h.roster(0)
        (1..3).forEach(h::missing)
        val current = h.session.state.groups.single()
        assertEquals(old, current)
        assertFalse(h.session.submit(V1BoardGroupAction.StopTrust(old)))
        assertFalse(h.session.submit(V1BoardGroupAction.StopTrust(current.copy(slot = 1))))
        assertTrue(h.session.submit(V1BoardGroupAction.StopTrust(current)))
        h.expect(wire.message(wire.STOP_TRUST) + wire.scope())

        h.deliver(wire.message(wire.STOP_TRUST) + wire.scope())
        assertNull(h.session.state.notice)
        assertTrue(h.session.state.groups.isEmpty())
        h.roster(0, state = wire.DISTRUSTED)
        (1..3).forEach(h::missing)

        val distrusted = h.session.state.groups.single()
        assertTrue(distrusted.distrusted)
        assertTrue(assertNotNull(h.session.state.notice).contains("stopped trusting"))
        assertFalse(h.session.submit(V1BoardGroupAction.StopTrust(distrusted)))
        assertEquals(1, h.sent.count { it.payload[5] == wire.STOP_TRUST.toByte() })
    }

    @Test
    fun createReceiptCannotClaimSuccessWithoutTheExpectedSignedRoster() {
        val h = Harness()
        h.emptyRoster()
        assertTrue(h.session.submit(V1BoardGroupAction.Create(locationRule = 1)))
        val differentScope = wire.scope().apply { this[lastIndex] = 1 }
        h.deliver(wire.message(wire.CREATE) + differentScope)
        h.roster(0)
        (1..3).forEach(h::missing)
        assertTrue(h.session.state.groups.isEmpty())
        assertFalse(h.session.state.canCreate)
        assertTrue(assertNotNull(h.session.state.notice).contains("Couldn't confirm"))
    }

    @Test
    fun changedFragmentSlotStateScopeOrOffsetDiscardsThePartialRoster() {
        val unexpected = listOf(
            wire.rosterResult(slot = 1, offset = 42),
            wire.rosterResult(slot = 0, offset = 42, state = wire.DISTRUSTED),
            wire.rosterResult(slot = 0, offset = 42, scope = wire.scope().apply { this[lastIndex] = 1 }),
            wire.rosterResult(slot = 0, offset = 84),
        )
        for (payload in unexpected) {
            val h = Harness()
            h.refresh()
            h.deliver(wire.rosterResult(slot = 0, offset = 0))
            h.expect(wire.readRequest(slot = 0, offset = 42))
            val count = h.sent.size
            h.deliver(payload)
            assertEquals(count, h.sent.size)
            assertFalse(h.session.state.busy)
            assertFalse(h.session.state.canCreate)
            assertTrue(h.session.state.groups.isEmpty())
        }
    }

    @Test
    fun changedSignedBytesCannotPassTheProtectedDigest() {
        val h = Harness()
        h.refresh()
        val altered = wire.rosterBytes().apply { this[lastIndex] = 1 }
        h.roster(0, bytes = altered)
        assertTrue(h.session.state.groups.isEmpty())
        assertFalse(h.session.state.busy)
        assertFalse(h.session.state.available)
        assertNotNull(h.session.state.notice)
    }

    @Test
    fun uncertainAndLostMutationsNeverReplayAndRetirementDropsLateResults() {
        for (timedOut in listOf(false, true)) {
            val h = Harness()
            h.emptyRoster()
            val create = V1BoardGroupAction.Create(locationRule = 1)
            assertTrue(h.session.submit(create))
            val exchange = h.last.exchange
            val count = h.sent.size
            if (timedOut) h.session.lost(exchange) else h.deliver(wire.message(wire.CREATE, wire.UNCERTAIN))
            assertFalse(h.session.state.busy)
            assertFalse(h.session.submit(create))
            assertFalse(h.session.receive(exchange, wire.message(wire.CREATE) + wire.scope()))
            assertEquals(count, h.sent.size)

            h.refresh()
            val retired = h.last.exchange
            h.current = false
            assertFalse(h.session.receive(retired, wire.rosterResult(slot = 0, offset = 0)))
            h.session.close()
            assertEquals(V1BoardGroupState(), h.session.state)
            assertFalse(h.session.receive(retired, wire.rosterResult(slot = 0, offset = 0)))
        }
    }

    private class Harness {
        data class Sent(val exchange: UInt, val payload: ByteArray)
        private var nextExchange = 1u
        var current = true
        val sent = mutableListOf<Sent>()
        val last get() = sent.last()
        val session = BleGroupRosterSession(
            isCurrent = { current },
            allocate = { nextExchange++ },
            send = { exchange, payload -> sent.add(Sent(exchange, payload)) },
            changed = {},
        )

        fun expect(payload: ByteArray) = assertContentEquals(payload, last.payload)

        fun deliver(payload: ByteArray) {
            assertTrue(session.receive(last.exchange, payload))
        }

        fun refresh() {
            assertTrue(session.submit(V1BoardGroupAction.Refresh))
            expect(GroupRosterResponseFixture.message(GroupRosterResponseFixture.DISCOVER))
            deliver(GroupRosterResponseFixture.message(GroupRosterResponseFixture.DISCOVER))
        }

        fun emptyRoster() {
            refresh()
            (0..3).forEach(::missing)
            assertTrue(session.state.available)
            assertTrue(session.state.canCreate)
        }

        fun missing(slot: Int) {
            expect(GroupRosterResponseFixture.readRequest(slot))
            deliver(GroupRosterResponseFixture.message(GroupRosterResponseFixture.READ_ROSTER, GroupRosterResponseFixture.MISSING))
        }

        fun roster(slot: Int, state: Int = GroupRosterResponseFixture.ACTIVE, bytes: ByteArray = GroupRosterResponseFixture.rosterBytes()) {
            for (offset in bytes.indices step 42) {
                expect(GroupRosterResponseFixture.readRequest(slot, offset, state))
                assertTrue(session.state.groups.isEmpty())
                deliver(GroupRosterResponseFixture.rosterResult(slot, offset, state = state, bytes = bytes))
            }
        }
    }
}

/** Matches the firmware's OTGC field layout. The sample OTGR signature is synthetic. */
internal object GroupRosterResponseFixture {
    const val DISCOVER = 1
    const val CREATE = 2
    const val STOP_TRUST = 3
    const val READ_ROSTER = 4
    const val UNCERTAIN = 4
    const val MISSING = 5
    const val ACTIVE = 1
    const val DISTRUSTED = 2
    const val GROUP_ID = 0xf102030405060708uL

    fun message(operation: Int, status: Int = 0): ByteArray =
        "OTGC".encodeToByteArray() + byteArrayOf(1, operation.toByte(), status.toByte(), 0)

    fun scope(): ByteArray = hex(
        "23".repeat(32) + "08070605040302f1" +
            "86c78b57b4e887d5a5358af1e7e8bd778b5540b8bd37f9f67fc3db0d9e8ff105",
    )

    fun rosterBytes(): ByteArray = hex(
        "4f54475201f10203040506070800000001" + "00".repeat(32) +
            "01010100000001" + "23".repeat(32) + "0541646d696e" + "5a".repeat(64),
    )

    fun readRequest(slot: Int, offset: Int = 0, state: Int = ACTIVE): ByteArray =
        message(READ_ROSTER) + position(slot, if (offset == 0) 0 else state, offset) +
            if (offset == 0) byteArrayOf() else scope()

    fun rosterResult(
        slot: Int,
        offset: Int,
        state: Int = ACTIVE,
        scope: ByteArray = scope(),
        bytes: ByteArray = rosterBytes(),
    ): ByteArray = message(READ_ROSTER) + position(slot, state, offset) +
        littleEndian16(bytes.size) + scope + bytes.copyOfRange(offset, minOf(bytes.size, offset + 42))

    private fun position(slot: Int, state: Int, offset: Int): ByteArray =
        byteArrayOf(slot.toByte(), state.toByte()) + littleEndian16(offset)

    private fun littleEndian16(value: Int): ByteArray =
        byteArrayOf(value.toByte(), (value ushr 8).toByte())

    private fun hex(value: String): ByteArray =
        value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
