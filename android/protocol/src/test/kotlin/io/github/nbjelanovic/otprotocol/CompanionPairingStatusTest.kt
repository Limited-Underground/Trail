package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class CompanionPairingStatusTest {
    private val status = CompanionStatusSnapshot(
        revision = 0x11223344,
        radio = CompanionRadioState.READY,
        gnss = CompanionGnssState.SEARCHING,
        power = CompanionPowerState.LOW,
        positionSharing = CompanionPositionSharingState.WAITING_FOR_FIX,
        queuedActionCount = 0x5566,
        pendingCriticalAlertId = 0x0102030405060708uL,
    )
    private val paired = CompanionPairingResult(
        CompanionPairingState.PAIRED_BOTH, peerShortId = 0xabcd,
        originExchangeId = 0x12345678, originSessionNonce = 0x9abcdef0, revision = 0x10203040,
    )

    @Test
    fun optInUsesDistinctWireFormAndLeavesV0Strict() {
        assertContentEquals(hex("4f54583100000000"), CompanionSemanticCodec.encodePairingSnapshotRequest())
        assertTrue(CompanionSemanticCodec.decodePairingSnapshotRequest(CompanionSemanticCodec.encodePairingSnapshotRequest()).isSuccess)
        assertFalse(CompanionSemanticCodec.decodeSnapshotRequest(CompanionSemanticCodec.encodePairingSnapshotRequest()).isSuccess)
        assertFalse(CompanionSemanticCodec.decodePairingSnapshotRequest(CompanionSemanticCodec.encodeSnapshotRequest()).isSuccess)
        val bytes = encode(paired)
        assertContentEquals(
            hex("4f544e3100000202030166554433221108070605040302010300cdab78563412f0debc9a40302010"),
            bytes,
        )
        assertFalse(CompanionSemanticCodec.decodeStatusSnapshot(bytes).isSuccess)
        val legacy = checkNotNull(CompanionSemanticCodec.encodeStatusSnapshot(status).value)
        assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(legacy).isSuccess)
        assertEquals(status, decode(bytes).snapshot)
        assertEquals(paired, decode(bytes).pairing)
        assertEquals(status, CompanionSemanticCodec.decodeStatusSnapshot(legacy).value)
    }

    @Test
    fun emptyAndAllSupportedOutcomesHaveIndependentPairingRevisions() {
        val empty = CompanionPairingResult()
        assertEquals(empty, decode(encode(empty)).pairing)
        assertEquals(empty.copy(revision = 99), decode(encode(empty.copy(revision = 99))).pairing)
        for (state in CompanionPairingState.entries.filterNot { it == CompanionPairingState.NONE }) {
            val reason = when (state) {
                CompanionPairingState.FAILED -> CompanionPairingReason.NO_REPLY
                CompanionPairingState.CANCELLED -> CompanionPairingReason.REQUEST_ENDED
                else -> CompanionPairingReason.NONE
            }
            val value = paired.copy(state = state, reason = reason, peerShortId = 0, revision = 7)
            val result = decode(encode(value))
            assertEquals(value, result.pairing)
            assertEquals(status, result.snapshot)
        }
        for (reason in CompanionPairingReason.entries.filterNot { it == CompanionPairingReason.NONE }) {
            val failure = paired.copy(state = CompanionPairingState.FAILED, reason = reason, peerShortId = 0)
            assertEquals(failure, decode(encode(failure)).pairing)
        }
        for (reason in listOf(CompanionPairingReason.NONE, CompanionPairingReason.STORAGE, CompanionPairingReason.REQUEST_ENDED)) {
            val pending = paired.copy(state = CompanionPairingState.INCOMPLETE, reason = reason, peerShortId = 0)
            assertEquals(pending, decode(encode(pending)).pairing)
        }
    }

    @Test
    fun malformedVersionLengthEnumsAndTelemetryNeverBecomeResults() {
        val valid = encode(paired)
        for (length in listOf(0, 24, 32, 39, 41, 128)) {
            assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(valid.copyOf(length)).isSuccess)
        }
        for (offset in listOf(3, 4, 5, 6, 7, 8, 9, 24, 25)) {
            assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(valid.changed(offset, 0xff)).isSuccess)
        }
        assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(valid.copyOf().also { it.fill(0, 12, 16) }).isSuccess)
        val request = CompanionSemanticCodec.encodePairingSnapshotRequest()
        for (offset in 4..7) assertFalse(CompanionSemanticCodec.decodePairingSnapshotRequest(request.changed(offset, 1)).isSuccess)
        for (length in listOf(7, 9)) assertFalse(CompanionSemanticCodec.decodePairingSnapshotRequest(request.copyOf(length)).isSuccess)
    }

    @Test
    fun invalidStateReasonCorrelationAndPeerCombinationsAreRefused() {
        val running = paired.copy(state = CompanionPairingState.RUNNING, peerShortId = 0)
        val invalid = listOf(
            paired.copy(reason = CompanionPairingReason.STORAGE),
            paired.copy(originExchangeId = 0),
            paired.copy(originSessionNonce = 0),
            paired.copy(revision = 0),
            paired.copy(revision = 0x1_0000_0000),
            paired.copy(peerShortId = -1),
            paired.copy(peerShortId = 0x10000),
            paired.copy(originExchangeId = -1),
            paired.copy(originSessionNonce = 0x1_0000_0000),
            paired.copy(state = CompanionPairingState.NONE),
            running.copy(reason = CompanionPairingReason.NO_REPLY),
            running.copy(peerShortId = 1),
            running.copy(state = CompanionPairingState.FAILED),
            running.copy(state = CompanionPairingState.CANCELLED),
            running.copy(state = CompanionPairingState.INCOMPLETE, reason = CompanionPairingReason.NO_REPLY),
        )
        invalid.forEach { value ->
            assertFalse(CompanionSemanticCodec.encodePairingStatusSnapshot(CompanionPairingStatusSnapshot(status, value)).isSuccess, value.toString())
        }
        val bytes = encode(paired)
        assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(bytes.changed(25, 1)).isSuccess)
        assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(bytes.copyOf().also { it.fill(0, 28, 32) }).isSuccess)
        assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(bytes.copyOf().also { it.fill(0, 32, 36) }).isSuccess)
        assertFalse(CompanionSemanticCodec.decodePairingStatusSnapshot(bytes.copyOf().also { it.fill(0, 36, 40) }).isSuccess)
    }

    @Test
    fun legacySemanticDispatchRequiresExplicitOptInForBothDirections() {
        val extended = CompanionFragment(CompanionFrameKind.SNAPSHOT, 7, 8, payload = encode(paired))
        assertEquals(CompanionSemanticCodecError.MALFORMED, CompanionSemanticCodec.validateSemanticFragment(extended))
        val request = extended.copy(kind = CompanionFrameKind.SNAPSHOT_REQUEST, payload = CompanionSemanticCodec.encodePairingSnapshotRequest())
        assertEquals(CompanionSemanticCodecError.MALFORMED, CompanionSemanticCodec.validateSemanticFragment(request))
    }

    private fun encode(pairing: CompanionPairingResult): ByteArray =
        checkNotNull(CompanionSemanticCodec.encodePairingStatusSnapshot(CompanionPairingStatusSnapshot(status, pairing)).value)

    private fun decode(bytes: ByteArray): CompanionPairingStatusSnapshot =
        checkNotNull(CompanionSemanticCodec.decodePairingStatusSnapshot(bytes).value)

    private fun ByteArray.changed(offset: Int, value: Int): ByteArray = copyOf().also { it[offset] = value.toByte() }
    private fun hex(value: String): ByteArray = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
