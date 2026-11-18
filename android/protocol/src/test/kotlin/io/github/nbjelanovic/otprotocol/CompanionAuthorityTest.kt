package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Exact frozen serialization vectors. No fixture here authenticates an endpoint or supplies authority. */
class CompanionAuthorityTest {
    private val requestRecord = hex("4f544330000609000403020108070605000138004f545930010100002122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f4001000000000000000200000004030201")
    private val success = hex("4f54433000068b000403020108070605000174004f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
    private val refusal = hex("4f54433000068b000403020108070605000174004f54593001010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000403020100000000")
    private val local = ByteArray(32) { (it + 1).toByte() }
    private val remote = ByteArray(32) { (it + 33).toByte() }
    private fun request() = assertNotNull(CompanionAuthorityCodec.decodeRequest(requestRecord))
    private fun response() = assertNotNull(CompanionAuthorityCodec.decodeResponse(success))

    @Test fun record_01_request_bothDirections() {
        assertEquals(76, requestRecord.size)
        assertEquals(76, CompanionAuthorityCodec.REQUEST_BYTES)
        val value = request()
        assertContentEquals(remote, value.remoteIdentity)
        assertEquals(1uL, value.group)
        assertEquals(2u, value.epoch)
        assertEquals(0x01020304u, value.nonce)
        assertEquals(0x05060708u, value.exchange)
        assertContentEquals(requestRecord, CompanionAuthorityCodec.encodeRequest(
            remote, 1uL, 2u, 0x01020304u, 0x05060708u))
        assertContentEquals(requestRecord, CompanionAuthorityCodec.encodeRequest(
            value.remoteIdentity, value.group, value.epoch, value.nonce, value.exchange))
    }

    @Test fun record_02_success_bothDirections() {
        assertEquals(136, success.size)
        assertEquals(136, CompanionAuthorityCodec.RESPONSE_BYTES)
        assertEquals(151, CompanionAuthorityCodec.MINIMUM_ATT_MTU)
        val value = response()
        assertFalse(value.isRefusal)
        assertContentEquals(local, value.localIdentity)
        assertContentEquals(remote, value.remoteIdentity)
        assertEquals(1uL, value.group)
        assertEquals(2u, value.epoch)
        assertEquals(3uL, value.deviceGeneration)
        assertEquals(3uL, value.sessionGeneration)
        assertEquals(4uL, value.membershipRevision)
        assertEquals(30_000L, value.remainingMillis)
        assertEquals(0x01020304u, value.nonce)
        assertEquals(0x05060708u, value.exchange)
        assertTrue(CompanionAuthorityCodec.matchesRequest(value, request(), local))
        assertContentEquals(success, CompanionAuthorityCodec.encodeResponse(value))
    }

    @Test fun record_03_refusal_bothDirections() {
        val value = assertNotNull(CompanionAuthorityCodec.decodeResponse(refusal))
        assertTrue(value.isRefusal)
        assertEquals(0uL, value.group)
        assertEquals(0u, value.epoch)
        assertEquals(0uL, value.deviceGeneration)
        assertEquals(0uL, value.sessionGeneration)
        assertEquals(0uL, value.membershipRevision)
        assertEquals(0L, value.remainingMillis)
        assertTrue(value.localIdentity.all { it == 0.toByte() })
        assertTrue(value.remoteIdentity.all { it == 0.toByte() })
        assertEquals(0x01020304u, value.nonce)
        assertEquals(0x05060708u, value.exchange)
        assertTrue(CompanionAuthorityCodec.matchesRequest(value, request(), local))
        assertNull(CompanionAuthorityCodec.deadlineMillis(1000, value.remainingMillis, 1001))
        assertContentEquals(refusal, CompanionAuthorityCodec.encodeResponse(value))
    }

    @Test fun payload_01_bad_magic() {
        val record = success.copyOfRange(0, 20) + hex("00545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_02_bad_schema() {
        val record = success.copyOfRange(0, 20) + hex("4f545930020103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_03_bad_domain() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010203000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_04_unknown_flags() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010107000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_05_reserved() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103010102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_06_zero_local_first_byte_only_valid() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000002030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        val changed = assertNotNull(CompanionAuthorityCodec.decodeResponse(record))
        assertFalse(CompanionAuthorityCodec.matchesRequest(changed, request(), local))
        assertTrue(CompanionAuthorityCodec.matchesRequest(changed, request(), local.copyOf().also { it[0] = 0 }))
    }

    @Test fun payload_07_zero_local_identity() {
        val record = success.copyOfRange(0, 20) + hex("4f5459300101030000000000000000000000000000000000000000000000000000000000000000002122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_08_zero_peer_identity() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2000000000000000000000000000000000000000000000000000000000000000000100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_09_zero_group() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400000000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_10_zero_epoch() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000000000000300000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_11_zero_device_generation() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000000000000000000030000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_12_zero_session_generation() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000000000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_13_zero_membership_revision() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000000000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_14_zero_nonce() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000000000030750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_15_zero_lifetime() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020100000000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_16_generation_mismatch() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000050000000000000004000000000000000403020130750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun payload_17_over_lifetime() {
        val record = success.copyOfRange(0, 20) + hex("4f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020131750000")
        assertNull(CompanionAuthorityCodec.decodeResponse(record))
    }

    @Test fun expiry_01_fixedDeadline_validControl() {
        val deadline = assertNotNull(CompanionAuthorityCodec.deadlineMillis(1000, 30_000, 1001))
        assertEquals(31_000L, deadline)
        assertTrue(CompanionAuthorityCodec.isBeforeDeadline(1001, deadline, 30_999))
        assertFalse(CompanionAuthorityCodec.isBeforeDeadline(1001, deadline, 31_000))
    }

    @Test fun expiry_02_oneMillisecond_expiredOnReply() {
        assertNull(CompanionAuthorityCodec.deadlineMillis(1000, 1, 1001))
    }

    @Test fun expiry_03_lateReply_expiryEquality() {
        assertNull(CompanionAuthorityCodec.deadlineMillis(1000, 30_000, 31_000))
    }

    @Test fun expiry_04_exactFrozenOverflow() {
        assertNull(CompanionAuthorityCodec.deadlineMillis(9_223_372_036_854_775_800L, 30, 9_223_372_036_854_775_800L))
    }

    // State cases 2/3/4/5/6 require real producer or future B2 owner lifecycle assertions.
    // They are not replaced by Boolean authority fixtures or counted as codec test passes.
    @Test fun state_01_wrongPinsAndContext() {
        val value = response()
        assertFalse(CompanionAuthorityCodec.matchesRequest(value, request(), local.copyOf().also { it[5] = 0 }))
        assertFalse(CompanionAuthorityCodec.matchesRequest(value, requestWith(remote.copyOf().also { it[6] = 0 }), local))
        assertFalse(CompanionAuthorityCodec.matchesRequest(value, requestWith(group = 2uL), local))
        assertFalse(CompanionAuthorityCodec.matchesRequest(value, requestWith(epoch = 3u), local))
        assertNull(CompanionAuthorityCodec.decodeResponse(success.copyOf().also { it[25] = 2 }))
    }

    @Test fun state_07_rollbackOverflowAndEquality() {
        assertNull(CompanionAuthorityCodec.deadlineMillis(1000, 30_000, 999))
        assertNull(CompanionAuthorityCodec.deadlineMillis(-1, 30_000, 1000))
        assertNull(CompanionAuthorityCodec.deadlineMillis(Long.MAX_VALUE, 1, Long.MAX_VALUE))
        assertNull(CompanionAuthorityCodec.deadlineMillis(1000, 0, 1000))
        assertNull(CompanionAuthorityCodec.deadlineMillis(1000, 30_001, 1000))
        assertFalse(CompanionAuthorityCodec.isBeforeDeadline(1001, 31_000, 1000))
        assertFalse(CompanionAuthorityCodec.isBeforeDeadline(1001, 31_000, 31_000))
        assertFalse(CompanionAuthorityCodec.isBeforeDeadline(1001, 31_000, 31_001))
    }

    @Test fun state_08_refusalRejectsEveryNonzeroAuthorityByte() {
        for (offset in (28 until 128) + (132 until 136)) {
            assertNull(CompanionAuthorityCodec.decodeResponse(refusal.copyOf().also { it[offset] = 1 }), "offset $offset")
        }
    }

    @Test fun state_09_strictEnvelopeBothDirections() {
        for ((record, decode) in listOf(
            requestRecord to { b: ByteArray -> CompanionAuthorityCodec.decodeRequest(b) },
            success to { b: ByteArray -> CompanionAuthorityCodec.decodeResponse(b) },
        )) {
            for (size in 0 until record.size) assertNull(decode(record.copyOf(size)), "size $size")
            assertNull(decode(record + 0.toByte()))
            for (offset in listOf(0, 4, 5, 6, 7, 16, 17, 18, 19, 20, 24, 25, 27)) {
                assertNull(decode(record.copyOf().also { it[offset] = (it[offset].toInt() xor 1).toByte() }), "offset $offset")
            }
            for (range in listOf(8..11, 12..15)) {
                assertNull(decode(record.copyOf().also { bytes -> range.forEach { bytes[it] = 0 } }))
            }
        }
        for (flag in 1..255) if (flag != 3) {
            assertNull(CompanionAuthorityCodec.decodeResponse(success.copyOf().also { it[26] = flag.toByte() }))
        }
        assertNull(CompanionAuthorityCodec.decodeRequest(requestRecord.copyOf().also { it[26] = 3 }))
        assertNull(CompanionAuthorityCodec.decodeRequest(requestRecord.copyOf().also { it[72] = 0 }))
        assertNull(CompanionAuthorityCodec.decodeResponse(success.copyOf().also { it[128] = 0 }))
    }

    @Test fun state_10_identityBitMutationRequiresExactPin() {
        val mutatedLocal = local.copyOf().also { it[31] = (it[31].toInt() xor 1).toByte() }
        val mutated = assertNotNull(CompanionAuthorityCodec.decodeResponse(success.copyOf().also { mutatedLocal.copyInto(it, 28) }))
        assertFalse(CompanionAuthorityCodec.matchesRequest(mutated, request(), local))
        assertTrue(CompanionAuthorityCodec.matchesRequest(mutated, request(), mutatedLocal))
    }

    @Test fun correlationNeverAcceptsWrongNonceOrExchange() {
        assertFalse(CompanionAuthorityCodec.matchesRequest(response(), requestWith(nonce = 1u), local))
        assertFalse(CompanionAuthorityCodec.matchesRequest(response(), requestWith(exchange = 1u), local))
        assertFalse(CompanionAuthorityCodec.matchesRequest(response(), request(), ByteArray(32)))
        assertFalse(CompanionAuthorityCodec.matchesRequest(response(), request(), ByteArray(31)))
    }

    @Test fun semanticBoundsAndUnsignedWidths() {
        assertNull(CompanionAuthorityCodec.encodeRequest(ByteArray(32), 1uL, 2u, 1u, 1u))
        assertNull(CompanionAuthorityCodec.encodeRequest(ByteArray(31) { 1 }, 1uL, 2u, 1u, 1u))
        assertNull(CompanionAuthorityCodec.encodeRequest(remote, 0uL, 2u, 1u, 1u))
        assertNull(CompanionAuthorityCodec.encodeRequest(remote, 1uL, 0u, 1u, 1u))
        assertNull(CompanionAuthorityCodec.encodeRequest(remote, 1uL, 2u, 0u, 1u))
        assertNull(CompanionAuthorityCodec.encodeRequest(remote, 1uL, 2u, 1u, 0u))
        assertNull(CompanionAuthorityCodec.decodeResponse(success.copyOf().also { local.copyInto(it, 60) }))
        val highRequest = assertNotNull(CompanionAuthorityCodec.decodeRequest(assertNotNull(
            CompanionAuthorityCodec.encodeRequest(remote, ULong.MAX_VALUE, UInt.MAX_VALUE, UInt.MAX_VALUE, UInt.MAX_VALUE))))
        assertEquals(ULong.MAX_VALUE, highRequest.group)
        assertEquals(UInt.MAX_VALUE, highRequest.epoch)
        val high = success.copyOf().also { bytes ->
            for (range in listOf(92..99, 104..111, 112..119, 120..127)) range.forEach { bytes[it] = 0xff.toByte() }
        }
        val decoded = assertNotNull(CompanionAuthorityCodec.decodeResponse(high))
        assertEquals(ULong.MAX_VALUE, decoded.group)
        assertEquals(ULong.MAX_VALUE, decoded.deviceGeneration)
        assertEquals(ULong.MAX_VALUE, decoded.sessionGeneration)
        assertEquals(ULong.MAX_VALUE, decoded.membershipRevision)
        assertContentEquals(high, CompanionAuthorityCodec.encodeResponse(decoded))
    }

    @Test fun ownedBytesAndRedactedDescriptions() {
        val input = success.copyOf()
        val decoded = assertNotNull(CompanionAuthorityCodec.decodeResponse(input))
        input.fill(0)
        decoded.localIdentity.fill(0)
        decoded.remoteIdentity.fill(0)
        CompanionAuthorityCodec.encodeResponse(decoded).fill(0)
        assertContentEquals(success, CompanionAuthorityCodec.encodeResponse(decoded))
        val requestInput = requestRecord.copyOf()
        val decodedRequest = assertNotNull(CompanionAuthorityCodec.decodeRequest(requestInput))
        requestInput.fill(0)
        decodedRequest.remoteIdentity.fill(0)
        assertContentEquals(remote, decodedRequest.remoteIdentity)
        assertEquals("CompanionAuthorityRequest(fields=redacted)", decodedRequest.toString())
        assertEquals("CompanionAuthorityResponse(fields=redacted)", decoded.toString())
    }

    private fun requestWith(
        peer: ByteArray = remote, group: ULong = 1uL, epoch: UInt = 2u,
        nonce: UInt = 0x01020304u, exchange: UInt = 0x05060708u,
    ) = assertNotNull(CompanionAuthorityCodec.decodeRequest(assertNotNull(
        CompanionAuthorityCodec.encodeRequest(peer, group, epoch, nonce, exchange))))

    private fun hex(value: String): ByteArray = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
