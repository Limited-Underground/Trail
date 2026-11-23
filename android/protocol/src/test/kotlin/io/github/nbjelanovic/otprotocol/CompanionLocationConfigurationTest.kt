package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class CompanionLocationConfigurationTest {
    private val codec = CompanionLocationCodec
    private val admin = ByteArray(32) { (it + 1).toByte() }
    private val digest = ByteArray(32) { (it + 64).toByte() }
    private val group = CompanionLocationRequest(
        CompanionLocationOperation.START,
        codec.GROUP_KIND,
        3,
        0xf102030405060708uL,
        0xf3040506uL,
        sample = 0xf5060708090a0b0cuL,
        ageMs = 13_609,
        latitudeE4 = -1,
        longitudeE4 = 0x010203,
        adminFingerprint = admin,
        rosterDigest = digest,
    )
    private val direct = CompanionLocationRequest(
        CompanionLocationOperation.RENEW, codec.DIRECT_KIND, codec.DIRECT_SLOT,
        0x8877665544332211uL, 0xf010203040506070uL, lease = 0xf001020304050607uL,
    )

    @Test
    fun groupStartAndDirectRenewMatchExactBigEndianFixtures() {
        val groupWire = hex(
            "01020103f10203040506070800000000f30405060000000000000000" +
                "f5060708090a0b0c00003529ffffffff00010203",
        ) + admin + digest
        val directWire = hex(
            "010302ff8877665544332211f010203040506070f001020304050607",
        ) + ByteArray(84)
        assertEquals(codec.REQUEST_BYTES, groupWire.size)
        assertEquals(codec.REQUEST_BYTES, directWire.size)
        assertContentEquals(groupWire, codec.encode(group))
        assertContentEquals(directWire, codec.encode(direct))
        assertTrue(codec.validCommand(groupWire))
        assertTrue(codec.validCommand(directWire))
    }

    @Test
    fun acceptsOnlyExactScopeAndFreshOrAbsentFixAsRequiredByOperation() {
        val valid = listOf(
            direct.copy(operation = CompanionLocationOperation.CONSENT, lease = 0uL),
            direct.copy(operation = CompanionLocationOperation.WITHDRAW, lease = 0uL),
            direct.copy(operation = CompanionLocationOperation.STOP),
            direct,
            direct.copy(sample = 1uL, ageMs = 14_999, latitudeE4 = 900_000, longitudeE4 = -1_800_000),
            group.copy(ageMs = 0, latitudeE4 = -900_000, longitudeE4 = 1_800_000),
        )
        valid.forEach { assertTrue(codec.validCommand(assertNotNull(codec.encode(it)))) }

        val invalid = listOf(
            group.copy(kind = 0),
            group.copy(slot = 4),
            group.copy(scopeId = 0uL),
            group.copy(generation = 0uL),
            group.copy(generation = UInt.MAX_VALUE.toULong() + 1uL),
            group.copy(adminFingerprint = ByteArray(32)),
            group.copy(rosterDigest = ByteArray(32)),
            group.copy(rosterDigest = ByteArray(31)),
            group.copy(lease = 1uL),
            group.copy(sample = 0uL),
            group.copy(ageMs = 15_000),
            group.copy(ageMs = -1),
            group.copy(latitudeE4 = -900_001),
            group.copy(longitudeE4 = 1_800_001),
            direct.copy(slot = 0),
            direct.copy(adminFingerprint = admin),
            direct.copy(rosterDigest = digest),
            direct.copy(lease = 0uL),
            direct.copy(ageMs = 1),
            direct.copy(latitudeE4 = 1),
            direct.copy(operation = CompanionLocationOperation.CONSENT),
            direct.copy(operation = CompanionLocationOperation.WITHDRAW, lease = 0uL, sample = 1uL),
            direct.copy(operation = CompanionLocationOperation.STOP, sample = 1uL),
        )
        invalid.forEach { assertNull(codec.encode(it)) }
        val source = assertNotNull(codec.encode(group))
        for ((offset, value) in listOf(0 to 2, 1 to 0, 2 to 3, 3 to 4, 36 to 255)) {
            assertFalse(codec.validCommand(source.copyOf().also { it[offset] = value.toByte() }))
        }
        assertFalse(codec.validCommand(source.copyOf(source.size - 1)))
        assertFalse(codec.validCommand(source + byteArrayOf(0)))
    }

    @Test
    fun validatesStatusAndCorrelatesTheOperationScopeGenerationAndLease() {
        val wire = hex("01020100f10203040506070800000000f30405068000000000000001")
        val accepted = assertNotNull(codec.decodeReply(wire, group))
        assertEquals(CompanionLocationStatus.OK, accepted.status)
        assertEquals(0x8000000000000001uL, accepted.lease)
        assertEquals(group.scopeId, accepted.scopeId)
        assertEquals(accepted, codec.decodeReply(wire, group.operation, group.scopeId, group.generation))

        val renewed = reply(direct, CompanionLocationStatus.OK, direct.lease)
        assertEquals(direct.lease, assertNotNull(codec.decodeReply(renewed, direct)).lease)
        for (stale in listOf(
            direct.copy(operation = CompanionLocationOperation.STOP),
            direct.copy(scopeId = 2uL),
            direct.copy(generation = 2uL),
            direct.copy(lease = 2uL),
        )) {
            assertNull(codec.decodeReply(renewed, stale))
        }

        for (status in listOf(CompanionLocationStatus.DENIED, CompanionLocationStatus.UNAVAILABLE)) {
            assertEquals(status, assertNotNull(codec.decodeReply(reply(group, status, 0uL), group)).status)
            assertNull(codec.decodeReply(reply(group, status, 1uL), group))
        }
        assertNull(codec.decodeReply(reply(group, CompanionLocationStatus.OK, 0uL), group))
        val stop = direct.copy(operation = CompanionLocationOperation.STOP)
        assertNotNull(codec.decodeReply(reply(stop, CompanionLocationStatus.OK, 0uL), stop))
        assertNull(codec.decodeReply(reply(stop, CompanionLocationStatus.OK, direct.lease), stop))
        for ((offset, value) in listOf(0 to 2, 1 to 0, 2 to 4, 3 to 1)) {
            assertFalse(codec.validReply(wire.copyOf().also { it[offset] = value.toByte() }))
        }
        assertFalse(codec.validReply(wire.copyOf(wire.size - 1)))
        assertFalse(codec.validReply(wire + byteArrayOf(0)))
    }

    @Test
    fun protectedEnvelopeAdmitsOnlyProfileFiveAndPreservesExistingCorrelation() {
        val payload = assertNotNull(codec.encode(group))
        val result = reply(group, CompanionLocationStatus.OK, 89uL)
        val nonce = 0x44332211u
        val exchange = 0x78563412u
        for ((kind, body) in listOf(codec.REQUEST_KIND to payload, codec.RESPONSE_KIND to result)) {
            val frame = CompanionConfigurationFrame(kind, nonce, exchange, body, 5)
            val raw = assertNotNull(CompanionConfigurationCodec.encodeFrame(frame, allowProfile06 = true))
            val restored = assertNotNull(CompanionConfigurationCodec.decodeFrame(raw, expectedMinorVersion = 5))
            assertEquals(kind, restored.kind)
            assertEquals(nonce, restored.sessionNonce)
            assertEquals(exchange, restored.exchangeId)
            assertContentEquals(body, restored.payload)

            for (minor in listOf(2, 3, 4, 6, 127)) {
                val crossProfile = CompanionConfigurationFrame(kind, nonce, exchange, body, minor)
                assertNull(CompanionConfigurationCodec.encodeFrame(crossProfile, allowProfile06 = true))
                val changedMinor = raw.copyOf().also { it[5] = minor.toByte() }
                assertNull(CompanionConfigurationCodec.decodeFrame(changedMinor, minor, allowProfile06 = true))
            }
            val wrongDirection = if (kind == codec.REQUEST_KIND) codec.RESPONSE_KIND else codec.REQUEST_KIND
            assertNull(CompanionConfigurationCodec.encodeFrame(
                CompanionConfigurationFrame(wrongDirection, nonce, exchange, body, 5),
            ))
            assertNull(CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(kind, 0u, exchange, body, 5)))
            assertNull(CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(kind, nonce, 0u, body, 5)))
            val malformedPayload = raw.copyOf().also { it[20] = 2 }
            assertNull(CompanionConfigurationCodec.decodeFrame(malformedPayload, expectedMinorVersion = 5))
        }
    }

    @Test
    fun requestAndReplyDoNotDescribePrivateFields() {
        assertEquals("CompanionLocationRequest(operation=START, fields=redacted)", group.toString())
        val parsed = assertNotNull(codec.decodeReply(reply(group, CompanionLocationStatus.OK, 89uL), group))
        assertEquals("CompanionLocationReply(operation=START, status=OK, fields=redacted)", parsed.toString())
    }

    private fun reply(request: CompanionLocationRequest, status: CompanionLocationStatus, lease: ULong) =
        ByteBuffer.allocate(codec.REPLY_BYTES).order(ByteOrder.BIG_ENDIAN)
            .put(1).put(request.operation.wire.toByte()).put(status.wire.toByte()).put(0)
            .putLong(request.scopeId.toLong()).putLong(request.generation.toLong()).putLong(lease.toLong())
            .array()

    private fun hex(value: String): ByteArray = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
