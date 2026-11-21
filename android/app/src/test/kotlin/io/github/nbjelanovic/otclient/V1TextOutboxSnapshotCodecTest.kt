package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.OtxtPayloadCodec
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class V1TextOutboxSnapshotCodecTest {
    private val recipient = "8ed69077".repeat(8)
    private val sample = V1TextOutboxEntry(1, "See you at camp", V1TextOutboxPhase.WAITING, 0)

    @Test
    fun roundTripsEmptyAndFullSnapshotsWithUnicodeAndAttemptStates() {
        val phases = listOf(V1TextOutboxPhase.IN_FLIGHT, V1TextOutboxPhase.SENT,
            V1TextOutboxPhase.UNCONFIRMED, V1TextOutboxPhase.FAILED)
        val entries = List(V1_TEXT_OUTBOX_LIMIT) { index ->
            val phase = phases.getOrElse(index) { V1TextOutboxPhase.WAITING }
            val attempts = if (phase == V1TextOutboxPhase.WAITING || phase == V1TextOutboxPhase.FAILED) 0 else 1
            V1TextOutboxEntry(index + 1L, "é".repeat(OtxtPayloadCodec.MAX_TEXT_UTF8_BYTES / 2),
                phase, attempts)
        }
        for (snapshot in listOf(V1TextOutboxSnapshot(), V1TextOutboxSnapshot(recipient, entries))) {
            val encoded = checkNotNull(V1TextOutboxSnapshotCodec.encode(snapshot))
            assertEquals(snapshot, V1TextOutboxSnapshotCodec.decode(encoded))
        }
    }

    @Test
    fun rejectsUnboundInvalidOversizedAndDuplicateEntries() {
        val invalid = listOf(
            V1TextOutboxSnapshot(entries = listOf(sample)),
            V1TextOutboxSnapshot("unknown", listOf(sample)),
            V1TextOutboxSnapshot(recipient, listOf(sample, sample)),
            V1TextOutboxSnapshot(recipient, listOf(sample.copy(id = 0))),
            V1TextOutboxSnapshot(recipient, listOf(sample.copy(attempts = V1_TEXT_OUTBOX_MAX_ATTEMPTS + 1))),
            V1TextOutboxSnapshot(recipient, listOf(sample.copy(text = "é".repeat(46)))),
            V1TextOutboxSnapshot(recipient, List(V1_TEXT_OUTBOX_LIMIT + 1) { sample.copy(id = it + 1L) }),
        )
        invalid.forEach { assertNull(V1TextOutboxSnapshotCodec.encode(it)) }
    }

    @Test
    fun rejectsImpossibleAttemptStatesAndOutOfOrderInFlightEntries() {
        val active = sample.copy(state = V1TextOutboxPhase.IN_FLIGHT, attempts = 1)
        val invalid = listOf(
            listOf(sample.copy(attempts = V1_TEXT_OUTBOX_MAX_ATTEMPTS)),
            listOf(sample.copy(state = V1TextOutboxPhase.IN_FLIGHT)),
            listOf(sample.copy(state = V1TextOutboxPhase.SENT)),
            listOf(sample.copy(state = V1TextOutboxPhase.UNCONFIRMED)),
            listOf(active, active.copy(id = 2)),
            listOf(sample, active.copy(id = 2)),
        )
        invalid.forEach { assertNull(V1TextOutboxSnapshotCodec.encode(V1TextOutboxSnapshot(recipient, it))) }
    }

    @Test
    fun rejectsTruncationUnknownVersionTrailingBytesAndMalformedText() {
        val bytes = checkNotNull(V1TextOutboxSnapshotCodec.encode(V1TextOutboxSnapshot(recipient, listOf(sample))))
        for (size in bytes.indices) {
            assertNull(V1TextOutboxSnapshotCodec.decode(bytes.copyOf(size)))
        }
        assertNull(V1TextOutboxSnapshotCodec.decode(bytes + 0.toByte()))
        assertNull(V1TextOutboxSnapshotCodec.decode(bytes.copyOf().apply { this[0] = 2 }))
        assertNull(V1TextOutboxSnapshotCodec.decode(bytes.copyOf().apply { this[lastIndex] = 0x80.toByte() }))
    }
}
