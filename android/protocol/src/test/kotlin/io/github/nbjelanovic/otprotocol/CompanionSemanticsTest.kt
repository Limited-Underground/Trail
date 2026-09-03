package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class CompanionSemanticsTest {
    private val vectors by lazy {
        checkNotNull(javaClass.classLoader.getResourceAsStream("companion_semantics_v0_vectors.csv"))
            .bufferedReader().readLines().drop(1).filter(String::isNotBlank)
            .associate { line ->
                val fields = line.split(',', limit = 2)
                fields[0] to fields[1].decodeHex()
            }
    }

    private fun status() = CompanionStatusSnapshot(
        revision = 0x11223344,
        radio = CompanionRadioState.READY,
        gnss = CompanionGnssState.SEARCHING,
        power = CompanionPowerState.LOW,
        positionSharing = CompanionPositionSharingState.WAITING_FOR_FIX,
        queuedActionCount = 0x5566,
        pendingCriticalAlertId = 0x0102030405060708uL,
    )

    @Test
    fun snapshotAndStatusMatchAcceptedCppGoldenVectors() {
        assertContentEquals(vectors.getValue("snapshot_request"), CompanionSemanticCodec.encodeSnapshotRequest())
        assertEquals(CompanionSnapshotRequest, CompanionSemanticCodec.decodeSnapshotRequest(vectors.getValue("snapshot_request")).value)

        val encoded = CompanionSemanticCodec.encodeStatusSnapshot(status())
        assertTrue(encoded.isSuccess)
        assertContentEquals(vectors.getValue("status_snapshot"), encoded.value)
        assertEquals(status(), CompanionSemanticCodec.decodeStatusSnapshot(encoded.value!!).value)
    }

    @Test
    fun allClosedStatusEnumsRoundTrip() {
        CompanionRadioState.entries.forEach { value -> assertEquals(value, roundTrip(status().copy(radio = value)).radio) }
        CompanionGnssState.entries.forEach { value -> assertEquals(value, roundTrip(status().copy(gnss = value)).gnss) }
        CompanionPowerState.entries.forEach { value -> assertEquals(value, roundTrip(status().copy(power = value)).power) }
        CompanionPositionSharingState.entries.forEach { value ->
            assertEquals(value, roundTrip(status().copy(positionSharing = value)).positionSharing)
        }
    }

    @Test
    fun statusAndSnapshotRejectVersionsReservesUnknownEnumsAndInvalidRevision() {
        val request = vectors.getValue("snapshot_request")
        assertEquals(CompanionSemanticCodecError.MALFORMED, CompanionSemanticCodec.decodeSnapshotRequest(request.copyOf(7)).error)
        assertEquals(CompanionSemanticCodecError.UNSUPPORTED_VERSION, CompanionSemanticCodec.decodeSnapshotRequest(request.changed(5, 1)).error)
        assertEquals(CompanionSemanticCodecError.RESERVED_BITS_SET, CompanionSemanticCodec.decodeSnapshotRequest(request.changed(7, 1)).error)

        val snapshot = vectors.getValue("status_snapshot")
        assertEquals(CompanionSemanticCodecError.UNKNOWN_ENUM, CompanionSemanticCodec.decodeStatusSnapshot(snapshot.changed(6, 0xff)).error)
        assertEquals(CompanionSemanticCodecError.RESERVED_BITS_SET, CompanionSemanticCodec.decodeStatusSnapshot(snapshot.changed(24, 1)).error)
        val zeroRevision = snapshot.copyOf().also { (12..15).forEach { index -> it[index] = 0 } }
        assertEquals(CompanionSemanticCodecError.INVALID_REVISION, CompanionSemanticCodec.decodeStatusSnapshot(zeroRevision).error)
        assertEquals(CompanionSemanticCodecError.INVALID_REVISION, CompanionSemanticCodec.encodeStatusSnapshot(status().copy(revision = 0)).error)
    }

    @Test
    fun fourQuickStatusesAndExactAlertAckRoundTrip() {
        CompanionQuickStatus.entries.forEach { quick ->
            val request = CompanionActionRequest(CompanionActionKind.QUICK_STATUS, quick)
            assertEquals(request, CompanionSemanticCodec.decodeActionRequest(CompanionSemanticCodec.encodeActionRequest(request).value!!).value)
        }
        assertContentEquals(
            vectors.getValue("quick_status_available"),
            CompanionSemanticCodec.encodeActionRequest(
                CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.AVAILABLE_TO_HELP),
            ).value,
        )
        val ack = CompanionActionRequest(
            CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT,
            criticalAlertId = 0x1122334455667788uL,
        )
        assertContentEquals(vectors.getValue("critical_alert_ack"), CompanionSemanticCodec.encodeActionRequest(ack).value)
        assertEquals(ack, CompanionSemanticCodec.decodeActionRequest(vectors.getValue("critical_alert_ack")).value)
    }

    @Test
    fun positionStartAndStopAreDistinctExactIntents() {
        val start = CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)
        val stop = CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING)
        assertContentEquals(vectors.getValue("position_start"), CompanionSemanticCodec.encodeActionRequest(start).value)
        assertContentEquals(vectors.getValue("position_stop"), CompanionSemanticCodec.encodeActionRequest(stop).value)
        assertEquals(start, CompanionSemanticCodec.decodeActionRequest(vectors.getValue("position_start")).value)
        assertEquals(stop, CompanionSemanticCodec.decodeActionRequest(vectors.getValue("position_stop")).value)
    }

    @Test
    fun factoryResetUsesExactKindFiveAndNonzeroLittleEndianReceipt() {
        val request = CompanionActionRequest(
            CompanionActionKind.FACTORY_RESET,
            factoryResetReceipt = 0x1122334455667788uL,
        )
        val encoded = CompanionSemanticCodec.encodeActionRequest(request)

        assertTrue(encoded.isSuccess)
        assertContentEquals(vectors.getValue("factory_reset_receipt"), encoded.value)
        assertEquals(request, CompanionSemanticCodec.decodeActionRequest(encoded.value!!).value)
        assertEquals(
            CompanionSemanticCodecError.RESERVED_BITS_SET,
            CompanionSemanticCodec.decodeActionRequest(encoded.value!!.changed(7, 1)).error,
        )
        assertEquals(
            CompanionSemanticCodecError.INCOHERENT_ACTION,
            CompanionSemanticCodec.encodeActionRequest(request.copy(criticalAlertId = 1uL)).error,
        )
        assertEquals(
            CompanionSemanticCodecError.INCOHERENT_ACTION,
            CompanionSemanticCodec.encodeActionRequest(request.copy(factoryResetReceipt = 0uL)).error,
        )
        assertEquals(
            CompanionSemanticCodecError.RESERVED_BITS_SET,
            CompanionSemanticCodec.decodeActionRequest(encoded.value!!.changed(16, 1)).error,
        )
        val admitted = CompanionActionResult(
            kind = CompanionActionKind.FACTORY_RESET,
            factoryResetReceipt = request.factoryResetReceipt,
            disposition = CompanionActionDisposition.ADMITTED,
        )
        assertContentEquals(
            vectors.getValue("admitted_factory_reset_receipt"),
            CompanionSemanticCodec.encodeActionResult(admitted).value,
        )
        assertEquals(
            admitted,
            CompanionSemanticCodec.decodeActionResult(vectors.getValue("admitted_factory_reset_receipt")).value,
        )
    }

    @Test
    fun actionRequestsRejectUnknownAmbiguousAndReservedValues() {
        assertEquals(
            CompanionSemanticCodecError.INVALID_ALERT_ID,
            CompanionSemanticCodec.encodeActionRequest(
                CompanionActionRequest(CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT),
            ).error,
        )
        assertEquals(
            CompanionSemanticCodecError.INCOHERENT_ACTION,
            CompanionSemanticCodec.encodeActionRequest(
                CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING, criticalAlertId = 1uL),
            ).error,
        )
        val quick = vectors.getValue("quick_status_available")
        assertEquals(CompanionSemanticCodecError.UNKNOWN_ENUM, CompanionSemanticCodec.decodeActionRequest(quick.changed(6, 0xff)).error)
        assertEquals(CompanionSemanticCodecError.INVALID_QUICK_STATUS, CompanionSemanticCodec.decodeActionRequest(quick.changed(7, 5)).error)
        assertEquals(CompanionSemanticCodecError.RESERVED_BITS_SET, CompanionSemanticCodec.decodeActionRequest(quick.changed(16, 1)).error)
        assertEquals(
            CompanionSemanticCodecError.RESERVED_BITS_SET,
            CompanionSemanticCodec.decodeActionRequest(vectors.getValue("position_start").changed(7, 1)).error,
        )
    }

    @Test
    fun queuedOutboundAndAdmittedLocalResultsMatchCppVectorsWithoutDeliveryClaims() {
        val queued = CompanionActionResult(
            kind = CompanionActionKind.QUICK_STATUS,
            quickStatus = CompanionQuickStatus.NEED_ASSISTANCE,
            disposition = CompanionActionDisposition.QUEUED,
        )
        val admitted = CompanionActionResult(
            kind = CompanionActionKind.START_POSITION_SHARING,
            disposition = CompanionActionDisposition.ADMITTED,
        )
        assertContentEquals(vectors.getValue("queued_need_assistance"), CompanionSemanticCodec.encodeActionResult(queued).value)
        assertContentEquals(vectors.getValue("admitted_position_start"), CompanionSemanticCodec.encodeActionResult(admitted).value)
        assertEquals(queued, CompanionSemanticCodec.decodeActionResult(vectors.getValue("queued_need_assistance")).value)
        assertEquals(admitted, CompanionSemanticCodec.decodeActionResult(vectors.getValue("admitted_position_start")).value)
        assertFalse(CompanionActionDisposition.entries.any { it.name.contains("SENT") || it.name.contains("DELIVER") })
    }

    @Test
    fun rejectedResultsAreTypedAndCoherent() {
        val stale = CompanionActionResult(
            kind = CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT,
            criticalAlertId = 0x1122334455667788uL,
            disposition = CompanionActionDisposition.REJECTED,
            rejectReason = CompanionActionRejectReason.STALE_ALERT,
        )
        assertContentEquals(vectors.getValue("rejected_stale_alert"), CompanionSemanticCodec.encodeActionResult(stale).value)
        assertEquals(stale, CompanionSemanticCodec.decodeActionResult(vectors.getValue("rejected_stale_alert")).value)
        assertEquals(
            CompanionSemanticCodecError.INCOHERENT_RESULT,
            CompanionSemanticCodec.encodeActionResult(
                CompanionActionResult(
                    kind = CompanionActionKind.QUICK_STATUS,
                    disposition = CompanionActionDisposition.ADMITTED,
                ),
            ).error,
        )
        assertEquals(
            CompanionSemanticCodecError.INCOHERENT_RESULT,
            CompanionSemanticCodec.encodeActionResult(
                CompanionActionResult(
                    kind = CompanionActionKind.START_POSITION_SHARING,
                    disposition = CompanionActionDisposition.REJECTED,
                    rejectReason = CompanionActionRejectReason.QUEUE_FULL,
                ),
            ).error,
        )
    }

    @Test
    fun resultDecoderRejectsUnknownEnumsReservesAndIncoherence() {
        val queued = vectors.getValue("queued_need_assistance")
        assertEquals(CompanionSemanticCodecError.UNKNOWN_ENUM, CompanionSemanticCodec.decodeActionResult(queued.changed(6, 0xff)).error)
        assertEquals(CompanionSemanticCodecError.RESERVED_BITS_SET, CompanionSemanticCodec.decodeActionResult(queued.changed(10, 1)).error)
        assertEquals(CompanionSemanticCodecError.INCOHERENT_RESULT, CompanionSemanticCodec.decodeActionResult(queued.changed(6, 1)).error)
        assertEquals(CompanionSemanticCodecError.INCOHERENT_RESULT, CompanionSemanticCodec.decodeActionResult(queued.changed(7, 4)).error)
    }

    @Test
    fun semanticRecordsBindOnlyToExactSingleOtc0KindsWithin128Bytes() {
        val cases = listOf(
            CompanionFrameKind.SNAPSHOT_REQUEST to vectors.getValue("snapshot_request"),
            CompanionFrameKind.SNAPSHOT to vectors.getValue("status_snapshot"),
            CompanionFrameKind.ACTION_REQUEST to vectors.getValue("quick_status_available"),
            CompanionFrameKind.ACTION_RESULT to vectors.getValue("queued_need_assistance"),
        )
        cases.forEachIndexed { index, (kind, payload) ->
            val fragment = CompanionFragment(kind, 7, (index + 1).toLong(), payload = payload)
            assertEquals(CompanionSemanticCodecError.NONE, CompanionSemanticCodec.validateSemanticFragment(fragment))
            CompanionFrameKind.entries.filter { it != kind }.forEach { wrongKind ->
                val error = CompanionSemanticCodec.validateSemanticFragment(fragment.copy(kind = wrongKind))
                assertTrue(error != CompanionSemanticCodecError.NONE)
            }
        }
        val fragmented = CompanionFragment(CompanionFrameKind.ACTION_REQUEST, 7, 9, fragmentCount = 2, payload = vectors.getValue("quick_status_available"))
        assertEquals(CompanionSemanticCodecError.MALFORMED, CompanionSemanticCodec.validateSemanticFragment(fragmented))
        val oversized = CompanionFragment(CompanionFrameKind.ACTION_REQUEST, 7, 9, payload = ByteArray(129))
        assertEquals(CompanionSemanticCodecError.MALFORMED, CompanionSemanticCodec.validateSemanticFragment(oversized))
    }

    private fun roundTrip(snapshot: CompanionStatusSnapshot): CompanionStatusSnapshot =
        CompanionSemanticCodec.decodeStatusSnapshot(CompanionSemanticCodec.encodeStatusSnapshot(snapshot).value!!).value!!

    private fun ByteArray.changed(offset: Int, value: Int): ByteArray = copyOf().also { it[offset] = value.toByte() }
    private fun String.decodeHex(): ByteArray = chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
