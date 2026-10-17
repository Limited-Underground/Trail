package io.github.nbjelanovic.otclient

import java.nio.ByteBuffer
import java.util.zip.CRC32
import kotlin.test.*

class V1InboxSnapshotCodecTest {
    private val device = requireNotNull(V1InboxDeviceToken.create(ByteArray(16) { 0x55 }))
    private val peer = requireNotNull(V1DirectPeerToken.create(ByteArray(16) { 0x33 }))
    private val policy = V1InboxPolicy(6, 1024, 86_400_000, V1InboxFullPolicy.REJECT_NEW)
    private fun key(message: UInt = UInt.MAX_VALUE) =
        V1InboxKey(ULong.MAX_VALUE, 2uL, UInt.MAX_VALUE, 4uL, message)

    @Test fun canonicalRoundTripPreservesUnsignedKeysAndDirectReceiptState() {
        val source = V1InboxSnapshot(device, policy, listOf(
            V1InboxStoredRecord(key(), "Trail ready ✓", peer, 99, opened = true,
                importPending = true, readPending = true),
        ), ULong.MAX_VALUE)
        val encoded = requireNotNull(V1InboxSnapshotCodec.encode(source))
        val decoded = requireNotNull(V1InboxSnapshotCodec.decode(encoded))
        assertContentEquals(encoded, V1InboxSnapshotCodec.encode(decoded))
        assertEquals(ULong.MAX_VALUE, decoded.generation)
        assertEquals(UInt.MAX_VALUE, decoded.records.single().key.epoch)
        assertEquals(UInt.MAX_VALUE, decoded.records.single().key.message)
        assertEquals(peer, decoded.records.single().directPeer)
        assertTrue(decoded.records.single().readPending)
    }

    @Test fun emptyAndExpiredPendingTombstoneRoundTrip() {
        val empty = V1InboxSnapshot(device, policy, emptyList(), 1uL)
        assertContentEquals(requireNotNull(V1InboxSnapshotCodec.encode(empty)),
            V1InboxSnapshotCodec.encode(requireNotNull(V1InboxSnapshotCodec.decode(requireNotNull(V1InboxSnapshotCodec.encode(empty))))))
        val tombstone = V1InboxSnapshot(device, policy,
            listOf(V1InboxStoredRecord(key(), "expired", null, 0, importPending = true)), 2uL)
        val decoded = requireNotNull(V1InboxSnapshotCodec.decode(requireNotNull(V1InboxSnapshotCodec.encode(tombstone))))
        assertEquals(0, decoded.records.single().remainingMs)
        assertTrue(decoded.records.single().importPending)
    }

    @Test fun checksumTruncationTrailingAndUnknownFlagsFailClosed() {
        val encoded = requireNotNull(V1InboxSnapshotCodec.encode(snapshot()))
        assertNull(V1InboxSnapshotCodec.decode(encoded.copyOf(encoded.size - 1)))
        assertNull(V1InboxSnapshotCodec.decode(encoded + byteArrayOf(0)))
        assertNull(V1InboxSnapshotCodec.decode(encoded.copyOf().also { it[10] = (it[10].toInt() xor 1).toByte() }))
        val flagsOffset = 46 + 32
        assertNull(V1InboxSnapshotCodec.decode(rechecksum(encoded.copyOf().also { it[flagsOffset] = 0x10 })))
        assertNull(V1InboxSnapshotCodec.decode(rechecksum(encoded.copyOf().also { it[4] = 2 })))
    }

    @Test fun duplicateInvalidAndOversizedSnapshotsDoNotEncode() {
        val record = V1InboxStoredRecord(key(), "hello", null, 1)
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, policy, listOf(record, record), 1uL)))
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, policy.copy(records = 300), emptyList(), 1uL)))
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, policy.copy(utf8Bytes = 300_000), emptyList(), 1uL)))
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, policy, emptyList(), 0uL)))
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, policy,
            listOf(V1InboxStoredRecord(key(), "bad\ttext", null, 1)), 1uL)))
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, policy,
            listOf(V1InboxStoredRecord(key(), "   ", null, 1)), 1uL)))
        val aggregatePolicy = policy.copy(records = 2, utf8Bytes = 8)
        assertNull(V1InboxSnapshotCodec.encode(V1InboxSnapshot(device, aggregatePolicy, listOf(
            V1InboxStoredRecord(key(1u), "12345", null, 1),
            V1InboxStoredRecord(key(2u), "67890", null, 1),
        ), 1uL)))
    }

    @Test fun impossibleReceiptAndExpiryStatesFailDecodeEvenWithValidChecksum() {
        val encoded = requireNotNull(V1InboxSnapshotCodec.encode(snapshot()))
        val flagsOffset = 46 + 32
        val remainingOffset = flagsOffset + 1
        val readWithoutOpen = encoded.copyOf().also { it[flagsOffset] = 0x08 }
        assertNull(V1InboxSnapshotCodec.decode(rechecksum(readWithoutOpen)))
        val expiredWithoutReceipt = encoded.copyOf().also {
            it[flagsOffset] = 0
            ByteBuffer.wrap(it, remainingOffset, Long.SIZE_BYTES).putLong(0)
        }
        assertNull(V1InboxSnapshotCodec.decode(rechecksum(expiredWithoutReceipt)))
    }

    @Test fun tokenCopyOutIsDefensiveAndStringsStayRedacted() {
        val copy = device.copyBytes()
        copy.fill(0)
        assertContentEquals(ByteArray(16) { 0x55 }, device.copyBytes())
        val peerCopy = peer.copyBytes()
        peerCopy.fill(0)
        assertContentEquals(ByteArray(16) { 0x33 }, peer.copyBytes())
        assertFalse(snapshot().toString().contains("hello"))
    }

    private fun snapshot() = V1InboxSnapshot(device, policy,
        listOf(V1InboxStoredRecord(key(), "hello", null, 100, importPending = true)), 1uL)

    private fun rechecksum(value: ByteArray): ByteArray {
        val payload = value.size - Int.SIZE_BYTES
        val checksum = CRC32().apply { update(value, 0, payload) }.value.toInt()
        ByteBuffer.wrap(value, payload, Int.SIZE_BYTES).putInt(checksum)
        return value
    }
}
