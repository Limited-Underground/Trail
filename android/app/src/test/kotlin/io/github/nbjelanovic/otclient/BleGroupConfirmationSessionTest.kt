package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.test.*

internal object ConfirmationTestPayloads {
    fun offer(session: UInt = 17u, ttl: Int = 1_000): ByteArray = ByteBuffer.allocate(128).order(ByteOrder.LITTLE_ENDIAN).apply {
        put(byteArrayOf(0x4f,0x54,0x47,0x43,1,4,0,1)); putLong(1); putLong(2); putInt(3)
        put(ByteArray(16) { (it+1).toByte() }); put(ByteArray(32) { (it+2).toByte() }); put(ByteArray(32) { (it+3).toByte() })
        putInt(ttl); putLong(4); putInt(session.toInt()); putInt(0)
    }.array()
    fun result(offered: ByteArray, status: Int): ByteArray = offered.copyOf().also { it[5]=5; it[6]=status.toByte() }
}

class BleGroupConfirmationSessionTest {
    private class Fixture {
        var now = 100L
        var current = true
        var lane = true
        var sent = true
        var sequence = 1u
        val authority = V1GroupConfirmationAuthority()
        val writes = mutableListOf<ByteArray>()
        var onSend: () -> Unit = {}
        val source = BleGroupConfirmationSession(authority,17u,{current},{if(lane)sequence++ else null},
            { writes += it; onSend(); sent },{now})
        fun receive(payload: ByteArray, exchange: UInt = sequence-1u, nonce: UInt = 17u): Boolean =
            source.receive(CompanionConfigurationFrame(0x89,nonce,exchange,payload,127))
        fun readOffer(): V1GroupConfirmationOffer {
            assertNull(source.pollOffer()); assertTrue(source.busy)
            assertTrue(receive(ConfirmationTestPayloads.offer()))
            return assertNotNull(source.pollOffer())
        }
    }

    @Test fun readsOnceAndAnchorsExpiryToSendTime() {
        val f=Fixture(); assertNull(f.source.pollOffer()); f.now=400
        assertTrue(f.receive(ConfirmationTestPayloads.offer()))
        val offer=assertNotNull(f.source.pollOffer())
        assertEquals(1_100L,offer.deadlineMillis); assertEquals(64,offer.peerFingerprint.length)
        assertNull(f.source.pollOffer()); assertEquals(1,f.writes.size)
    }

    @Test fun confirmAndCancelEchoEveryFieldAndProduceOnlyExactCategoricalResults() {
        listOf(V1GroupConfirmationDecision.CONFIRM,V1GroupConfirmationDecision.CANCEL).forEach { decision ->
            val f=Fixture(); val offer=f.readOffer()
            assertTrue(f.source.submit(offer,decision)); assertTrue(f.source.busy)
            val frame=assertNotNull(CompanionConfigurationCodec.decodeFrame(f.writes.last(),127))
            val expected=ConfirmationTestPayloads.offer().also { it[5]=if(decision==V1GroupConfirmationDecision.CONFIRM)2 else 3 }
            assertContentEquals(expected,frame.payload)
            assertFalse(f.source.submit(offer,decision))
            assertTrue(f.receive(ConfirmationTestPayloads.result(ConfirmationTestPayloads.offer(),if(decision==V1GroupConfirmationDecision.CONFIRM)1 else 2)))
            assertFalse(f.source.busy)
            val result=assertNotNull(f.source.pollResult()); assertSame(offer,result.offer); assertEquals(decision,result.decision)
            assertNull(f.source.pollResult())
        }
    }

    @Test fun everyChangedEchoFieldAndWrongDecisionResultRefuse() {
        for(offset in listOf(7,8,16,24,28,44,76,108,112,120)) {
            val f=Fixture(); val offer=f.readOffer(); assertTrue(f.source.submit(offer,V1GroupConfirmationDecision.CONFIRM))
            val raw=ConfirmationTestPayloads.result(ConfirmationTestPayloads.offer(),1); raw[offset]=(raw[offset].toInt() xor 1).toByte()
            assertTrue(f.receive(raw)); assertTrue(f.source.busy)
            assertFailsWith<IllegalStateException>{f.source.pollResult()}
        }
        val f=Fixture(); val offer=f.readOffer(); assertTrue(f.source.submit(offer,V1GroupConfirmationDecision.CONFIRM))
        assertTrue(f.receive(ConfirmationTestPayloads.result(ConfirmationTestPayloads.offer(),2)))
        assertFailsWith<IllegalStateException>{f.source.pollResult()}
    }

    @Test fun lateOfferRollbackAndChangedSessionNeverPublish() {
        val late=Fixture(); late.source.pollOffer(); late.now=1_100
        assertTrue(late.receive(ConfirmationTestPayloads.offer())); assertFailsWith<IllegalStateException>{late.source.pollOffer()}
        val rollback=Fixture(); rollback.source.pollOffer(); rollback.now=99
        assertTrue(rollback.receive(ConfirmationTestPayloads.offer())); assertFailsWith<IllegalStateException>{rollback.source.pollOffer()}
        val stale=Fixture(); stale.source.pollOffer(); stale.current=false
        assertFalse(stale.receive(ConfirmationTestPayloads.offer())); assertFailsWith<IllegalStateException>{stale.source.pollOffer()}
    }

    @Test fun unrelatedResultsCannotConsumeTheCurrentExchange() {
        val f=Fixture(); f.source.pollOffer()
        assertFalse(f.receive(ConfirmationTestPayloads.offer(),99u))
        assertFalse(f.receive(ConfirmationTestPayloads.offer(),1u,99u)); assertTrue(f.source.busy)
        assertTrue(f.receive(ConfirmationTestPayloads.offer())); assertNotNull(f.source.pollOffer())
    }

    @Test fun localCloseAndTimeoutPreserveUncertainLaneFenceUntilSessionEnds() {
        val f=Fixture(); val offer=f.readOffer(); assertTrue(f.source.submit(offer,V1GroupConfirmationDecision.CONFIRM))
        val exchange=f.sequence-1u; f.source.close(); assertTrue(f.source.busy)
        f.source.lost(exchange); assertTrue(f.source.busy)
        f.source.endSession(); assertFalse(f.source.busy)
        assertFailsWith<IllegalStateException>{f.source.pollResult()}
        val failed=Fixture(); failed.sent=false
        assertFailsWith<IllegalStateException>{failed.source.pollOffer()}; assertTrue(failed.source.busy)
    }

    @Test fun waitingRequiresExplicitReadAndUnavailableNeverManufacturesAnOffer() {
        val f=Fixture(); f.source.pollOffer()
        val waiting=CompanionConfirmationCodec.read().encoded().also{it[5]=5;it[6]=5}
        assertTrue(f.receive(waiting)); assertFalse(f.source.busy); assertEquals(1,f.writes.size)
        f.source.pollOffer(); assertEquals(2,f.writes.size)
        waiting[6]=4; assertTrue(f.receive(waiting))
        assertFailsWith<IllegalStateException>{f.source.pollOffer()}
    }

    @Test fun occupiedLaneAndExpiredDecisionSendNothing() {
        val f=Fixture(); f.lane=false; assertNull(f.source.pollOffer()); assertTrue(f.writes.isEmpty())
        f.lane=true; val offer=f.readOffer(); f.now=1_100
        assertFalse(f.source.submit(offer,V1GroupConfirmationDecision.CONFIRM)); assertEquals(1,f.writes.size)
    }
}
