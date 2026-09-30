package io.github.nbjelanovic.otclient

import kotlin.test.*

class ProtectedProtocolInfoDiagnosticsTest {
    @Test fun callbackPreservesExactStatusLengthAndSeparateMonotonicTimings() {
        val fixture = Fixture()
        fixture.diagnostics.begin(true, false)
        fixture.now = 5_200
        fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE,
            ProtectedProtocolInfoDiagnosticOutcome.PLATFORM_FAILURE, true, true,
            status = 133, valueBytes = 0, exactCharacteristic = true, callbackAt = 5_190, handlingAt = 5_197)
        val observation = fixture.events.last()
        assertEquals(5_100L, observation.sinceMillis)
        assertEquals(7L, observation.dispatchMillis)
        assertEquals(3L, observation.handlingMillis)
        assertEquals(133, observation.status)
        assertEquals(0, observation.valueBytes)
        assertTrue(observation.currentGatt && observation.exactCharacteristic == true && observation.pendingRead)
    }

    @Test fun staleCallbackAndConcurrentRefusalCannotReplaceOutstandingRequest() {
        val fixture = Fixture()
        fixture.diagnostics.begin(true, false)
        fixture.now = 200
        fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_LEGACY,
            ProtectedProtocolInfoDiagnosticOutcome.IGNORED_STALE, false, true,
            status = 0, valueBytes = 16, exactCharacteristic = false)
        fixture.diagnostics.begin(true, true)
        fixture.now = 300
        fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE,
            ProtectedProtocolInfoDiagnosticOutcome.ACCEPT, true, true, status = 0, valueBytes = 16)
        assertEquals(setOf(1L), fixture.events.map { it.request }.toSet())
        assertEquals(200L, fixture.events.last().sinceMillis)
        fixture.diagnostics.finish()
        fixture.now = 400
        fixture.diagnostics.begin(true, false)
        assertEquals(2L, fixture.events.last().request)
        assertEquals(0L, fixture.events.last().sinceMillis)
    }

    @Test fun closedOldLeaseCannotReplaceNewConnectionCorrelation() {
        val old = Fixture()
        val current = Fixture()
        val oldConnection = ProtectedProtocolInfoDiagnostics.create(old) { 100 }
        val newConnection = ProtectedProtocolInfoDiagnostics.create(current) { 100 }
        oldConnection.begin(true, false)
        oldConnection.finish()
        newConnection.begin(true, false)
        oldConnection.record(ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE,
            ProtectedProtocolInfoDiagnosticOutcome.IGNORED_STALE, false, false, status = 257, valueBytes = 0)
        assertEquals(old.events.first().connection, old.events.last().connection)
        assertEquals(old.events.last().connection + 1, current.events.single().connection)
        assertEquals(1L, current.events.single().request)
        assertEquals(ProtectedProtocolInfoDiagnosticOutcome.REQUESTED, current.events.single().outcome)
    }

    @Test fun unsupportedNumbersBecomeExplicitUnavailableValues() {
        val fixture = Fixture()
        fixture.diagnostics.begin(true, false)
        fixture.now = Long.MAX_VALUE
        fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE,
            ProtectedProtocolInfoDiagnosticOutcome.PLATFORM_FAILURE, true, true,
            status = Int.MIN_VALUE, valueBytes = Int.MAX_VALUE, newState = 4,
            callbackAt = -1, handlingAt = 0)
        with(fixture.events.last()) {
            assertNull(sinceMillis)
            assertNull(dispatchMillis)
            assertNull(handlingMillis)
            assertNull(status)
            assertTrue(statusOutOfRange)
            assertNull(valueBytes)
            assertTrue(valueBytesOutOfRange)
            assertNull(newState)
        }
        assertFailsWith<IllegalArgumentException> { fixture.events.last().copy(status = 65_536) }
        assertFailsWith<IllegalArgumentException> { fixture.events.last().copy(valueBytes = 513) }
        assertFailsWith<IllegalArgumentException> { fixture.events.last().copy(sinceMillis = 60_001) }
    }

    @Test fun eventFloodStopsAtFixedBudgetWithExplicitTerminalMarker() {
        val fixture = Fixture()
        fixture.diagnostics.begin(true, false)
        repeat(100) {
            fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE,
                ProtectedProtocolInfoDiagnosticOutcome.IGNORED_STALE, false, true)
        }
        assertEquals(PROTECTED_READ_DIAGNOSTIC_MAX_EVENTS, fixture.events.size)
        assertEquals(ProtectedProtocolInfoDiagnosticOrigin.LIMIT, fixture.events.last().origin)
        assertEquals(ProtectedProtocolInfoDiagnosticOutcome.LIMIT_REACHED, fixture.events.last().outcome)
    }

    @Test fun noProviderHasNoRecorderWorkAndRecorderFailureCannotEscape() {
        val noProvider = ProtectedProtocolInfoDiagnostics.create(null) { error("Production must not request diagnostic time") }
        noProvider.begin(true, false)
        noProvider.record(ProtectedProtocolInfoDiagnosticOrigin.READ_INITIATION,
            ProtectedProtocolInfoDiagnosticOutcome.STARTED, true, true)
        val throwing = ProtectedProtocolInfoDiagnostics.create(object : ProtectedProtocolInfoDiagnosticObserver {
            override fun observeProtectedProtocolInfo(diagnostic: ProtectedProtocolInfoDiagnostic) { error("Storage failed") }
        }) { 100 }
        throwing.begin(true, false)
        throwing.record(ProtectedProtocolInfoDiagnosticOrigin.READ_INITIATION,
            ProtectedProtocolInfoDiagnosticOutcome.STARTED, true, true)
        throwing.finish()
    }

    @Test fun unrelatedCallbacksOutsideProtectedReadAreNotCaptured() {
        val fixture = Fixture()
        fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.CONNECTION_STATE_CALLBACK,
            ProtectedProtocolInfoDiagnosticOutcome.CALLBACK_OBSERVED, true, false, status = 0, newState = 2)
        assertTrue(fixture.events.isEmpty())
        fixture.diagnostics.begin(true, false)
        fixture.diagnostics.finish()
        fixture.diagnostics.record(ProtectedProtocolInfoDiagnosticOrigin.SERVICE_CHANGED_CALLBACK,
            ProtectedProtocolInfoDiagnosticOutcome.CALLBACK_OBSERVED, true, false)
        assertEquals(1, fixture.events.size)
    }

    private class Fixture : ProtectedProtocolInfoDiagnosticObserver {
        var now = 100L
        val events = mutableListOf<ProtectedProtocolInfoDiagnostic>()
        val diagnostics = ProtectedProtocolInfoDiagnostics.create(this) { now }
        override fun observeProtectedProtocolInfo(diagnostic: ProtectedProtocolInfoDiagnostic) { events += diagnostic }
    }
}
