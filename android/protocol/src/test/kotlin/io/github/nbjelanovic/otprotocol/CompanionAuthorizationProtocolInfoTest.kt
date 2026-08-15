package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class CompanionAuthorizationProtocolInfoTest {
    private val vector by lazy {
        checkNotNull(javaClass.classLoader.getResourceAsStream("companion_gatt_authorization_v0_vectors.csv"))
            .bufferedReader().readLines().drop(1).single { it.isNotBlank() }
            .substringAfter(',').decodeHex()
    }

    @Test
    fun exactCppGeneratedVectorRoundTripsWithoutChangingV0Codec() {
        val decoded = CompanionAuthorizationProtocolInfoCodec.decode(vector)
        assertTrue(decoded.isSuccess)
        assertEquals(
            CompanionAuthorizationProtocolInfo(provisionalSessionNonce = 0x11223344),
            decoded.value,
        )
        assertContentEquals(vector, CompanionAuthorizationProtocolInfoCodec.encode(decoded.value!!).value)
        assertEquals(
            CompanionCodecError.MALFORMED,
            CompanionProtocolCodec.decodeProtocolInfo(vector).error,
        )
    }

    @Test
    fun exactCapabilityVersionReservesAndBoundsFailClosed() {
        assertEquals(
            CompanionAuthorizationProtocolInfoError.MALFORMED,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.copyOf(19)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.UNSUPPORTED_VERSION,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.changed(5, 2)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.INVALID_CAPABILITY,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.changed(7, 0x0f)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.RESERVED_BITS_SET,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.changed(18, 1)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.RESERVED_BITS_SET,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.changed(19, 1)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.INVALID_ROLE,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.changed(6, 2)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.INVALID_SESSION_NONCE,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.copyOf().also { bytes ->
                (14..17).forEach { bytes[it] = 0 }
            }).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.INVALID_LIMIT,
            CompanionAuthorizationProtocolInfoCodec.decode(vector.changed(13, 2)).error,
        )
        assertEquals(
            CompanionAuthorizationProtocolInfoError.INVALID_LIMIT,
            CompanionAuthorizationProtocolInfoCodec.decode(
                vector.copyOf().also { bytes ->
                    bytes[8] = 27
                    bytes[9] = 0
                },
            ).error,
        )
        assertTrue(
            CompanionAuthorizationProtocolInfoCodec.decode(
                vector.copyOf().also { bytes ->
                    bytes[8] = 28
                    bytes[9] = 0
                },
            ).isSuccess,
        )
    }

    @Test
    fun manuallyConstructedValuesCannotExceedWireWidths() {
        listOf(-1L, 0L, 0x1_0000_0000L, Long.MAX_VALUE).forEach { nonce ->
            assertEquals(
                CompanionAuthorizationProtocolInfoError.INVALID_SESSION_NONCE,
                CompanionAuthorizationProtocolInfoCodec.encode(
                    CompanionAuthorizationProtocolInfo(provisionalSessionNonce = nonce),
                ).error,
            )
        }
        assertEquals(
            CompanionAuthorizationProtocolInfoError.INVALID_LIMIT,
            CompanionAuthorizationProtocolInfoCodec.encode(
                CompanionAuthorizationProtocolInfo(
                    minimumNormalAttMtu = 0x1_0000,
                    provisionalSessionNonce = 1,
                ),
            ).error,
        )
    }

    private fun ByteArray.changed(index: Int, value: Int): ByteArray = copyOf().also { it[index] = value.toByte() }

    private fun String.decodeHex(): ByteArray = chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
