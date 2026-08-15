package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class CompanionProtocolCodecTest {
    private val vectors by lazy {
        checkNotNull(javaClass.classLoader.getResourceAsStream("companion_protocol_v0_vectors.csv"))
            .bufferedReader()
            .readLines()
            .drop(1)
            .filter(String::isNotBlank)
            .associate { line ->
                val fields = line.split(',', limit = 2)
                fields[0] to fields[1].decodeHex()
            }
    }

    @Test
    fun protocolInfoMatchesSharedCppGoldenVector() {
        val info = CompanionProtocolInfo(capabilities = COMPANION_KNOWN_CAPABILITY_MASK)
        val encoded = CompanionProtocolCodec.encodeProtocolInfo(info)

        assertTrue(encoded.isSuccess)
        assertContentEquals(vectors.getValue("full_protocol_info"), encoded.value)
        assertEquals(info, CompanionProtocolCodec.decodeProtocolInfo(encoded.value!!).value)
    }

    @Test
    fun actionRequestMatchesSharedCppGoldenVector() {
        val fragment = CompanionFragment(
            kind = CompanionFrameKind.ACTION_REQUEST,
            sessionNonce = 0x11223344,
            exchangeId = 0xa1b2c3d4,
            payload = byteArrayOf(0x10, 0x20, 0x30),
        )
        val encoded = CompanionProtocolCodec.encodeFragment(fragment)

        assertTrue(encoded.isSuccess)
        assertContentEquals(vectors.getValue("action_request_11223344_a1b2c3d4"), encoded.value)
        assertEquals(fragment, CompanionProtocolCodec.decodeFragment(encoded.value!!).value)
    }

    @Test
    fun everyKnownKindAndMaximumFragmentRoundTrips() {
        CompanionFrameKind.entries.forEach { kind ->
            val source = CompanionFragment(
                kind = kind,
                sessionNonce = 0xffff_ffffL,
                exchangeId = 17,
                fragmentIndex = 15,
                fragmentCount = 16,
                payload = ByteArray(COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) { kind.wireValue.toByte() },
            )
            val encoded = CompanionProtocolCodec.encodeFragment(source)
            assertEquals(COMPANION_FRAGMENT_HEADER_BYTES + COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES, encoded.value!!.size)
            assertEquals(source, CompanionProtocolCodec.decodeFragment(encoded.value!!).value)
        }
    }

    @Test
    fun protocolInfoRejectsMalformedUnsupportedAndUnknownValues() {
        val canonical = vectors.getValue("full_protocol_info")
        assertEquals(CompanionCodecError.MALFORMED, CompanionProtocolCodec.decodeProtocolInfo(canonical.copyOf(15)).error)
        assertEquals(CompanionCodecError.UNSUPPORTED_VERSION, CompanionProtocolCodec.decodeProtocolInfo(canonical.changed(5, 1)).error)
        assertEquals(CompanionCodecError.UNKNOWN_ROLE, CompanionProtocolCodec.decodeProtocolInfo(canonical.changed(6, 0xff)).error)
        assertEquals(CompanionCodecError.UNKNOWN_CAPABILITY, CompanionProtocolCodec.decodeProtocolInfo(canonical.changed(7, 0x80)).error)
        assertEquals(CompanionCodecError.RESERVED_BITS_SET, CompanionProtocolCodec.decodeProtocolInfo(canonical.changed(14, 1)).error)
    }

    @Test
    fun fragmentRejectsMalformedReservedAndUnknownValues() {
        val canonical = vectors.getValue("action_request_11223344_a1b2c3d4")
        assertEquals(CompanionCodecError.MALFORMED, CompanionProtocolCodec.decodeFragment(canonical.copyOf(19)).error)
        assertEquals(CompanionCodecError.UNSUPPORTED_VERSION, CompanionProtocolCodec.decodeFragment(canonical.changed(4, 1)).error)
        assertEquals(CompanionCodecError.UNKNOWN_FRAME_KIND, CompanionProtocolCodec.decodeFragment(canonical.changed(6, 0x7f)).error)
        assertEquals(CompanionCodecError.RESERVED_BITS_SET, CompanionProtocolCodec.decodeFragment(canonical.changed(7, 1)).error)
        assertEquals(CompanionCodecError.MALFORMED, CompanionProtocolCodec.decodeFragment(canonical.changed(18, 2)).error)
        val oversized = ByteArray(COMPANION_FRAGMENT_HEADER_BYTES + COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES + 1)
        canonical.copyInto(oversized, endIndex = COMPANION_FRAGMENT_HEADER_BYTES)
        oversized[18] = (COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES + 1).toByte()
        oversized[19] = 0
        assertEquals(CompanionCodecError.PAYLOAD_TOO_LARGE, CompanionProtocolCodec.decodeFragment(oversized).error)
    }

    @Test
    fun encoderRejectsInvalidLimitsAndIdentifiers() {
        assertEquals(
            CompanionCodecError.UNKNOWN_CAPABILITY,
            CompanionProtocolCodec.encodeProtocolInfo(CompanionProtocolInfo(capabilities = 0x80)).error,
        )
        assertEquals(
            CompanionCodecError.INVALID_LIMIT,
            CompanionProtocolCodec.encodeProtocolInfo(
                CompanionProtocolInfo(capabilities = 0, maxActiveControllers = 2),
            ).error,
        )
        assertEquals(
            CompanionCodecError.INVALID_SESSION_NONCE,
            CompanionProtocolCodec.encodeFragment(
                CompanionFragment(CompanionFrameKind.SNAPSHOT_REQUEST, 0, 1),
            ).error,
        )
        assertFalse(
            CompanionProtocolCodec.encodeFragment(
                CompanionFragment(CompanionFrameKind.SNAPSHOT_REQUEST, 1, 1, payload = ByteArray(129)),
            ).isSuccess,
        )
    }

    private fun ByteArray.changed(offset: Int, value: Int): ByteArray = copyOf().also { it[offset] = value.toByte() }

    private fun String.decodeHex(): ByteArray {
        require(length % 2 == 0)
        return chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }
}
