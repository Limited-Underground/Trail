package io.github.nbjelanovic.otprotocol

import kotlin.test.*

class CompanionConfirmationCodecTest {
    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    private fun vectors(): List<Pair<ByteArray, ByteArray>> {
        val text = checkNotNull(javaClass.getResourceAsStream("/companion_confirmation_v1.json")).bufferedReader().use { it.readText() }
        val payloads = Regex("\"payload_hex\"\\s*:\\s*\"([0-9a-f]+)\"").findAll(text).map { hex(it.groupValues[1]) }.toList()
        val frames = Regex("\"frame_hex\"\\s*:\\s*\"([0-9a-f]+)\"").findAll(text).map { hex(it.groupValues[1]) }.toList()
        assertEquals(11, payloads.size); assertEquals(payloads.size, frames.size)
        return payloads.zip(frames)
    }

    @Test fun independentSharedPayloadAndFrameGoldensMatchExactly() {
        vectors().forEach { (payload, frame) ->
            val decoded = assertNotNull(CompanionConfirmationCodec.decode(payload))
            assertContentEquals(payload, decoded.encoded())
            val wrapped = assertNotNull(CompanionConfigurationCodec.decodeFrame(frame, 127))
            assertContentEquals(payload, wrapped.payload)
            assertContentEquals(frame, CompanionConfigurationCodec.encodeFrame(wrapped))
            assertNull(CompanionConfigurationCodec.decodeFrame(frame, 2))
            assertNull(CompanionConfigurationCodec.decodeFrame(frame, 3))
        }
        val offer = assertNotNull(CompanionConfirmationCodec.decode(vectors()[1].first))
        assertEquals(0xfedcba9876543210uL, offer.group)
        assertEquals(0x8877665544332211uL, offer.transportGeneration)
        assertEquals(0x0102030405060708L, offer.attempt)
        assertEquals(15_000L, offer.remainingMillis)
    }

    @Test fun profileRequiresExactOptInVersionAndLimits() {
        val info = CompanionConfigurationInfo(0xdf, minorVersion = 127)
        val bytes = assertNotNull(CompanionConfigurationCodec.encodeInfo(info))
        assertEquals(info, CompanionConfigurationCodec.decodeInfo(bytes, 127))
        assertNull(CompanionConfigurationCodec.decodeInfo(bytes))
        assertNull(CompanionConfigurationCodec.decodeInfo(bytes, 3))
        assertNull(CompanionConfigurationCodec.encodeInfo(info.copy(capabilities = 0xff)))
        assertNull(CompanionConfigurationCodec.encodeInfo(info.copy(fragmentCount = 2)))
        val read = CompanionConfirmationCodec.read().encoded()
        listOf(2,3).forEach { assertNull(CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(7,1u,1u,read,it))) }
    }

    @Test fun malformedFieldWidthsEnumsAndReservedBytesRefuse() {
        val raw = vectors()[1].first
        for (size in listOf(0,127,129)) assertNull(CompanionConfirmationCodec.decode(raw.copyOf(size)))
        for (offset in listOf(0,4,6,7,124,125,126,127)) {
            assertNull(CompanionConfirmationCodec.decode(raw.copyOf().also { it[offset] = 99 }))
        }
        for ((start, end) in listOf(8 to 16,16 to 24,24 to 28,28 to 44,44 to 76,76 to 108,108 to 112,112 to 120,120 to 124)) {
            assertNull(CompanionConfirmationCodec.decode(raw.copyOf().also { it.fill(0,start,end) }))
        }
        assertNull(CompanionConfirmationCodec.decode(raw.copyOf().also { it[15] = 0x80.toByte() }))
        assertNull(CompanionConfirmationCodec.decode(raw.copyOf().also { it[108] = 0x61; it[109] = 0xea.toByte() }))
        val read = CompanionConfirmationCodec.read().encoded()
        (6..127).forEach { offset -> assertNull(CompanionConfirmationCodec.decode(read.copyOf().also { it[offset] = 1 })) }
    }

    @Test fun payloadOwnsBytesAndExactDecisionEchoIsStable() {
        val raw = vectors()[1].first
        val payload = assertNotNull(CompanionConfirmationCodec.decode(raw))
        val before = payload.encoded()
        raw.fill(0); payload.encoded().fill(0); payload.peer.fill(0)
        assertContentEquals(before, payload.encoded())
        val confirm = assertNotNull(payload.withOperation(2))
        assertTrue(payload.sameDescriptor(confirm))
        assertContentEquals(vectors()[4].first, confirm.encoded())
        assertFalse(payload.toString().contains("887766"))
    }
}
