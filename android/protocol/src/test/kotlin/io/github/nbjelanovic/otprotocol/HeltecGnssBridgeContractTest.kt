package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class HeltecGnssBridgeContractTest {
    @Test
    fun actualFirmwareEncoderVectorsDecodeWithoutProtocolOrNumericExtensions() {
        val stream = assertNotNull(javaClass.getResourceAsStream("/heltec_v4_gnss_snapshots.txt"))
        val vectors = stream.bufferedReader().use { it.readLines() }.filter { it.isNotBlank() }
        assertEquals(CompanionGnssState.entries.map { it.name }, vectors.map { it.substringBefore(' ') })
        var previousRevision = 0L
        vectors.forEach { line ->
            val expected = CompanionGnssState.valueOf(line.substringBefore(' '))
            val bytes = line.substringAfter(' ').chunked(2).map { it.toInt(16).toByte() }.toByteArray()
            assertEquals(32, bytes.size)
            assertTrue(bytes.drop(24).all { it == 0.toByte() })
            val decoded = CompanionSemanticCodec.decodeStatusSnapshot(bytes)
            val snapshot = assertNotNull(decoded.value)
            assertEquals(expected, snapshot.gnss)
            assertTrue(snapshot.revision > previousRevision)
            previousRevision = snapshot.revision
            assertEquals(CompanionRadioState.UNAVAILABLE, snapshot.radio)
            assertEquals(CompanionPowerState.UNKNOWN, snapshot.power)
            assertEquals(CompanionPositionSharingState.STOPPED, snapshot.positionSharing)
            assertEquals(0, snapshot.queuedActionCount)
            assertContentEquals(bytes, CompanionSemanticCodec.encodeStatusSnapshot(snapshot).value)
            assertEquals(
                CompanionSemanticCodecError.RESERVED_BITS_SET,
                CompanionSemanticCodec.decodeStatusSnapshot(bytes.copyOf().also { it[24] = 1 }).error,
            )
        }
    }
}
