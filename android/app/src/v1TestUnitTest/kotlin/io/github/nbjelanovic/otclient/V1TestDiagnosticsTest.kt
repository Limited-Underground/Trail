package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class V1TestDiagnosticsTest {
    @Test
    fun boundedRingRetainsNewestTypedRecordsInSequenceOrder() {
        val log = V1TestDiagnosticRingBuffer(capacity = 3)
        repeat(5) {
            assertTrue(log.append(it.toLong(), V1TestDiagnosticPayload.AppLifecycle(V1TestAppLifecycleState.FOREGROUND)))
        }
        assertEquals(listOf(2L, 3L, 4L), log.snapshot().map { it.sequence })
        assertEquals(listOf(2L, 3L, 4L), log.snapshot().map { it.elapsedMillis })
    }

    @Test
    fun ordinaryBufferHasNoArbitraryTextChannel() {
        val note = assertNotNull(V1TestDiagnosticPayload.TesterNote.create("check antenna placement"))
        assertFalse(V1TestDiagnosticRingBuffer().append(1, note))
        assertTrue(V1TestDiagnosticRingBuffer(allowUserSuppliedText = true).append(1, note))
        assertFalse(note.toString().contains("antenna"))
    }

    @Test
    fun typedPayloadsValidateBoundsWithoutCoordinatesOrMessageBodies() {
        assertFailsWith<IllegalArgumentException> { V1TestDiagnosticPayload.RadioSignal(1, 0) }
        assertFailsWith<IllegalArgumentException> { V1TestDiagnosticPayload.PowerSample(4_000, 101, false) }
        assertFailsWith<IllegalArgumentException> { V1TestDiagnosticPayload.RadioCounters(-1, 0, 0) }
        val gps = V1TestDiagnosticPayload.GpsHealth(V1TestGpsHealthState.CURRENT, fixAgeMillis = 500)
        assertEquals(V1TestGpsHealthState.CURRENT, gps.state)
        assertEquals(500, gps.fixAgeMillis)
    }

    @Test
    fun elapsedTimeCannotGoBackwardAndSequenceCannotOverflow() {
        val log = V1TestDiagnosticRingBuffer()
        assertTrue(log.append(10, V1TestDiagnosticPayload.Restart(V1TestRestartReason.COLD_START)))
        assertFalse(log.append(9, V1TestDiagnosticPayload.Restart(V1TestRestartReason.UNKNOWN)))
        val exhausted = V1TestDiagnosticRingBuffer(initialSequence = Long.MAX_VALUE)
        assertFalse(exhausted.append(0, V1TestDiagnosticPayload.Failure(V1TestFailureCode.UNKNOWN)))
    }

    @Test
    fun snapshotIsACopyAndClearDoesNotResetSequenceIdentity() {
        val log = V1TestDiagnosticRingBuffer(capacity = 2)
        assertTrue(log.append(1, V1TestDiagnosticPayload.Restart(V1TestRestartReason.COLD_START)))
        val snapshot = log.snapshot()
        log.clear()
        assertEquals(1, snapshot.size)
        assertTrue(log.snapshot().isEmpty())
        assertTrue(log.append(2, V1TestDiagnosticPayload.Restart(V1TestRestartReason.PROCESS_RESTART)))
        assertEquals(1L, log.snapshot().single().sequence)
    }

    @Test
    fun constructorAndNotesFailClosedAndRecordStringRedactsPayload() {
        assertFailsWith<IllegalArgumentException> { V1TestDiagnosticRingBuffer(0) }
        assertFailsWith<IllegalArgumentException> { V1TestDiagnosticRingBuffer(initialSequence = -1) }
        assertFalse(V1TestDiagnosticPayload.TesterNote.create("bad\ntext") != null)
        val record = V1TestDiagnosticRecord(1, 2, V1TestDiagnosticPayload.Failure(V1TestFailureCode.BLE))
        assertFalse(record.toString().contains("BLE"))
    }
}
