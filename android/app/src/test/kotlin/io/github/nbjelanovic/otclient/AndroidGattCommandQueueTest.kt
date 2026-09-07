package io.github.nbjelanovic.otclient

import kotlin.test.*

class AndroidGattCommandQueueTest {
    private class Fixture {
        val gate = AndroidGattOperationGate()
        val queue = AndroidGattCommandQueue(gate, 8)
        val sent = mutableListOf<ByteArray>()
        var accepted = true
        val write: (ByteArray) -> Boolean = { sent += it.copyOf(); accepted }
        init {
            check(gate.beginConnection()); check(gate.beginDiscovery()); check(gate.acceptProfile())
            check(gate.beginMtuRequest()); check(gate.acceptMtu())
            check(gate.beginProtocolInfoRead()); check(gate.acceptProtocolInfo())
            check(gate.beginIndicationSubscription()); check(gate.acceptIndicationSubscription())
        }
    }

    @Test fun indicationBeforeWriteCallbackQueuesOneOwnedSuccessorAndDrainsOnce() {
        val f = Fixture()
        assertTrue(f.queue.submit(byteArrayOf(1), f.write))
        assertTrue(f.gate.acceptsStreamIndication())
        val successor = byteArrayOf(2)
        assertTrue(f.queue.submit(successor, f.write))
        successor[0] = 99
        assertTrue(f.queue.hasQueued)
        assertFalse(f.queue.submit(byteArrayOf(3), f.write))
        assertEquals(1, f.sent.size)
        assertTrue(f.queue.acknowledge(f.write))
        assertContentEquals(byteArrayOf(2), f.sent.last())
        assertFalse(f.queue.hasQueued)
        assertEquals(AndroidGattStage.COMMAND_WRITE_PENDING, f.gate.stage)
        assertTrue(f.queue.acknowledge(f.write))
        assertEquals(AndroidGattStage.READY, f.gate.stage)
        assertEquals(2, f.sent.size)
    }

    @Test fun writeCallbackBeforeIndicationAllowsImmediateSuccessor() {
        val f = Fixture()
        assertTrue(f.queue.submit(byteArrayOf(1), f.write))
        assertTrue(f.queue.acknowledge(f.write))
        assertTrue(f.queue.submit(byteArrayOf(2), f.write))
        assertEquals(2, f.sent.size)
        assertFalse(f.queue.hasQueued)
    }

    @Test fun closeOrExpiryDiscardsQueuedBytesAndCannotReplay() {
        val f = Fixture()
        f.queue.submit(byteArrayOf(1), f.write)
        f.queue.submit(byteArrayOf(2), f.write)
        f.queue.close()
        assertFalse(f.queue.hasQueued)
        assertFalse(f.queue.acknowledge(f.write))
        assertFalse(f.queue.submit(byteArrayOf(3), f.write))
        assertEquals(1, f.sent.size)
        assertEquals(AndroidGattStage.CLOSED, f.gate.stage)
    }

    @Test fun uncertainDeferredWriteClosesWithoutReplay() {
        val f = Fixture()
        f.queue.submit(byteArrayOf(1), f.write)
        f.queue.submit(byteArrayOf(2), f.write)
        f.accepted = false
        assertFalse(f.queue.acknowledge(f.write))
        f.accepted = true
        assertFalse(f.queue.acknowledge(f.write))
        assertFalse(f.queue.submit(byteArrayOf(2), f.write))
        assertEquals(2, f.sent.size)
        assertFalse(f.queue.hasQueued)
    }

    @Test fun invalidLengthOrStageDoesNotStartOrReplaceAWrite() {
        val f = Fixture()
        assertFalse(f.queue.submit(byteArrayOf(), f.write))
        assertFalse(f.queue.submit(ByteArray(9), f.write))
        assertEquals(AndroidGattStage.READY, f.gate.stage)
        assertTrue(f.queue.submit(ByteArray(8) { 1 }, f.write))
        assertFalse(f.queue.submit(ByteArray(9), f.write))
        assertFalse(f.queue.hasQueued)
        val unopened = AndroidGattCommandQueue(AndroidGattOperationGate())
        assertFalse(unopened.submit(byteArrayOf(1), f.write))
        assertEquals(1, f.sent.size)
    }

    @Test fun duplicateAcknowledgementAndUncertainInitialWriteFailClosed() {
        val f = Fixture()
        assertFalse(f.queue.acknowledge(f.write))
        assertFalse(f.queue.submit(byteArrayOf(1), f.write))
        assertTrue(f.sent.isEmpty())
        val g = Fixture(); g.accepted = false
        assertFalse(g.queue.submit(byteArrayOf(1), g.write))
        assertFalse(g.queue.acknowledge(g.write))
        assertEquals(1, g.sent.size)
    }
}
