package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

class CompanionAppControllerTest {
    @Test
    fun selectionConnectAndDisconnectExposeEveryExplicitState() {
        val controller = CompanionAppController(FakeCompanionTransport())
        val observed = mutableListOf<CompanionUiState>()
        controller.observe(observed::add)

        assertIs<CompanionUiState.Disconnected>(controller.state)
        controller.chooseDevice()
        val choosing = assertIs<CompanionUiState.Selecting>(controller.state)
        assertEquals(listOf("Bench candidate A", "Bench candidate B"), choosing.candidates.map { it.publicLabel })

        controller.connect(choosing.candidates.first().endpointToken)
        val connected = assertIs<CompanionUiState.Connected>(controller.state)
        assertEquals("Bench candidate A", connected.connection.publicLabel)
        assertTrue(connected.connection.status.contains("no Bluetooth or radio evidence"))
        assertTrue(observed.any { it is CompanionUiState.Connecting })

        controller.disconnect()
        assertIs<CompanionUiState.Disconnected>(controller.state)
    }

    @Test
    fun unavailableAndRefusedCandidatesFailClosed() {
        val candidates = listOf(CompanionCandidate("fake-a", "Bench candidate A"))
        val controller = CompanionAppController(FakeCompanionTransport(candidates, failingEndpoint = "fake-a"))

        controller.chooseDevice()
        controller.connect("missing")
        assertEquals("That test companion is no longer available.", assertIs<CompanionUiState.Failed>(controller.state).publicReason)

        controller.retrySelection()
        controller.connect("fake-a")
        assertEquals("The deterministic test connection was refused.", assertIs<CompanionUiState.Failed>(controller.state).publicReason)
    }

    @Test
    fun connectedSessionCannotBeReplacedOrCancelledWithoutDisconnect() {
        val controller = CompanionAppController(FakeCompanionTransport())
        controller.chooseDevice()
        controller.connect("fake-a")
        val connected = assertIs<CompanionUiState.Connected>(controller.state)

        controller.connect("fake-b")
        controller.chooseDevice()
        controller.cancelSelection()
        assertEquals(connected, controller.state)

        controller.disconnect()
        controller.chooseDevice()
        controller.connect("fake-b")
        assertEquals("Bench candidate B", assertIs<CompanionUiState.Connected>(controller.state).connection.publicLabel)
    }

    @Test
    fun transportPermitsOnlyOneActiveFakeConnection() {
        val transport = FakeCompanionTransport()
        assertIs<ConnectionAttempt.Connected>(transport.connect("fake-a"))
        assertEquals(
            "Disconnect the current test companion first.",
            assertIs<ConnectionAttempt.Failed>(transport.connect("fake-b")).publicReason,
        )
        transport.disconnect("fake-a")
        assertIs<ConnectionAttempt.Connected>(transport.connect("fake-b"))
    }

    @Test
    fun connectedStatePublishesTypedDeviceSnapshotAndQueuesFourQuickStatuses() {
        val controller = connectedController()
        val initial = assertIs<CompanionUiState.Connected>(controller.state)
        assertEquals(1, initial.connection.snapshot.revision)
        assertEquals(0, initial.connection.snapshot.queuedActionCount)

        CompanionQuickStatus.entries.forEachIndexed { index, quickStatus ->
            controller.submitAction(CompanionActionRequest(CompanionActionKind.QUICK_STATUS, quickStatus))
            val current = assertIs<CompanionUiState.Connected>(controller.state)
            assertEquals(index + 1, current.connection.snapshot.queuedActionCount)
            assertEquals(index + 2L, current.connection.snapshot.revision)
            assertEquals(quickStatus, current.lastActionResult?.quickStatus)
            assertEquals(CompanionActionDisposition.QUEUED, current.lastActionResult?.disposition)
        }
    }

    @Test
    fun exactPendingAlertAcknowledgementQueuesOnceThenRejectsStaleReplay() {
        val transport = FakeCompanionTransport()
        val connected = assertIs<ConnectionAttempt.Connected>(transport.connect("fake-a")).connection
        val alertId = connected.snapshot.pendingCriticalAlertId
        val request = CompanionActionRequest(
            kind = CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT,
            criticalAlertId = alertId,
        )

        val accepted = assertIs<SemanticActionAttempt.Applied>(transport.submitAction("fake-a", request))
        assertEquals(CompanionActionDisposition.QUEUED, accepted.result.disposition)
        assertEquals(0uL, accepted.snapshot.pendingCriticalAlertId)
        val replay = assertIs<SemanticActionAttempt.Applied>(transport.submitAction("fake-a", request))
        assertEquals(CompanionActionDisposition.REJECTED, replay.result.disposition)
        assertEquals(CompanionActionRejectReason.STALE_ALERT, replay.result.rejectReason)
        assertEquals(1, replay.snapshot.queuedActionCount)
    }

    @Test
    fun explicitPositionStartAndStopAreLocalAdmissions() {
        val controller = connectedController()
        controller.submitAction(CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING))
        var current = assertIs<CompanionUiState.Connected>(controller.state)
        assertEquals(CompanionPositionSharingState.WAITING_FOR_FIX, current.connection.snapshot.positionSharing)
        assertEquals(CompanionActionDisposition.ADMITTED, current.lastActionResult?.disposition)
        assertEquals(0, current.connection.snapshot.queuedActionCount)

        controller.submitAction(CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING))
        current = assertIs<CompanionUiState.Connected>(controller.state)
        assertEquals(CompanionPositionSharingState.STOPPED, current.connection.snapshot.positionSharing)
        assertEquals(CompanionActionDisposition.ADMITTED, current.lastActionResult?.disposition)
    }

    @Test
    fun semanticActionsFailClosedOutsideConnectedStateAndRemainDeviceOwnedAcrossReconnect() {
        val controller = CompanionAppController(FakeCompanionTransport())
        val disconnected = controller.state
        controller.submitAction(
            CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.OK),
        )
        assertEquals(disconnected, controller.state)

        controller.chooseDevice()
        controller.connect("fake-a")
        controller.submitAction(
            CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.OK),
        )
        assertEquals(1, assertIs<CompanionUiState.Connected>(controller.state).connection.snapshot.queuedActionCount)
        controller.disconnect()
        controller.chooseDevice()
        controller.connect("fake-a")
        assertEquals(1, assertIs<CompanionUiState.Connected>(controller.state).connection.snapshot.queuedActionCount)
    }

    @Test
    fun queueAndRevisionBoundsRejectWithoutMutatingDeviceSnapshot() {
        val queueFull = status(revision = 9, queued = 0xffff, alertId = 0x1001uL)
        val queueTransport = FakeCompanionTransport(initialSnapshots = mapOf("fake-a" to queueFull))
        assertIs<ConnectionAttempt.Connected>(queueTransport.connect("fake-a"))
        val quickRejected = assertIs<SemanticActionAttempt.Applied>(
            queueTransport.submitAction(
                "fake-a",
                CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.OK),
            ),
        )
        assertEquals(CompanionActionRejectReason.QUEUE_FULL, quickRejected.result.rejectReason)
        assertEquals(queueFull, quickRejected.snapshot)
        val ackRejected = assertIs<SemanticActionAttempt.Applied>(
            queueTransport.submitAction(
                "fake-a",
                CompanionActionRequest(CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT, criticalAlertId = 0x1001uL),
            ),
        )
        assertEquals(CompanionActionRejectReason.QUEUE_FULL, ackRejected.result.rejectReason)
        assertEquals(queueFull, ackRejected.snapshot)

        val revisionMax = status(revision = 0xffff_ffffL, queued = 3, alertId = 0x1001uL)
        val requests = listOf(
            CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.OK),
            CompanionActionRequest(CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT, criticalAlertId = 0x1001uL),
            CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING),
            CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING),
        )
        requests.forEach { request ->
            val transport = FakeCompanionTransport(initialSnapshots = mapOf("fake-a" to revisionMax))
            assertIs<ConnectionAttempt.Connected>(transport.connect("fake-a"))
            val rejected = assertIs<SemanticActionAttempt.Applied>(transport.submitAction("fake-a", request))
            assertEquals(CompanionActionDisposition.REJECTED, rejected.result.disposition)
            assertEquals(CompanionActionRejectReason.INTERNAL_FAILURE, rejected.result.rejectReason)
            assertEquals(revisionMax, rejected.snapshot)
        }
    }

    @Test
    fun fakeEnvelopeCorrelationIsMonotonicBoundedAndSessionScoped() {
        val transport = FakeCompanionTransport()
        assertIs<ConnectionAttempt.Connected>(transport.connect("fake-a"))
        val first = assertIs<SemanticActionAttempt.Applied>(
            transport.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)),
        )
        val second = assertIs<SemanticActionAttempt.Applied>(
            transport.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING)),
        )
        assertEquals(1L, first.sessionNonce)
        assertEquals(1L, first.exchangeId)
        assertEquals(2L, second.exchangeId)
        assertEquals(first.sessionNonce, second.sessionNonce)
        assertEquals(CompanionFrameKind.ACTION_RESULT, first.responseKind)
        assertEquals(CompanionFrameKind.ACTION_RESULT, second.responseKind)

        transport.disconnect("fake-a")
        assertIs<ConnectionAttempt.Connected>(transport.connect("fake-a"))
        val reopened = assertIs<SemanticActionAttempt.Applied>(
            transport.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING)),
        )
        assertEquals(2L, reopened.sessionNonce)
        assertEquals(1L, reopened.exchangeId)

        val boundary = FakeCompanionTransport(firstExchangeId = 0xffff_ffffL)
        assertIs<ConnectionAttempt.Connected>(boundary.connect("fake-a"))
        val last = assertIs<SemanticActionAttempt.Applied>(
            boundary.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)),
        )
        assertEquals(0xffff_ffffL, last.exchangeId)
        val beforeExhausted = last.snapshot
        assertIs<SemanticActionAttempt.Failed>(
            boundary.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING)),
        )
        boundary.disconnect("fake-a")
        val persisted = assertIs<ConnectionAttempt.Connected>(boundary.connect("fake-a")).connection.snapshot
        assertEquals(beforeExhausted, persisted)
    }

    @Test
    fun invalidActionAndSessionExhaustionDoNotCreateAuthority() {
        val transport = FakeCompanionTransport()
        assertIs<ConnectionAttempt.Connected>(transport.connect("fake-a"))
        assertIs<SemanticActionAttempt.Failed>(
            transport.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT)),
        )
        val firstValid = assertIs<SemanticActionAttempt.Applied>(
            transport.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)),
        )
        assertEquals(1L, firstValid.exchangeId)

        val exhaustedSession = FakeCompanionTransport(initialSessionNonce = 0xffff_ffffL)
        assertIs<ConnectionAttempt.Failed>(exhaustedSession.connect("fake-a"))
        assertIs<SemanticActionAttempt.Failed>(
            exhaustedSession.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)),
        )
        val negativeSession = FakeCompanionTransport(initialSessionNonce = -1)
        assertIs<ConnectionAttempt.Failed>(negativeSession.connect("fake-a"))
        assertIs<SemanticActionAttempt.Failed>(
            negativeSession.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)),
        )
        val invalidSnapshot = FakeCompanionTransport(
            initialSnapshots = mapOf("fake-a" to status(revision = 0, queued = 0, alertId = 0uL)),
        )
        assertIs<ConnectionAttempt.Failed>(invalidSnapshot.connect("fake-a"))
        assertIs<SemanticActionAttempt.Failed>(
            invalidSnapshot.submitAction("fake-a", CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)),
        )
    }

    private fun connectedController(): CompanionAppController =
        CompanionAppController(FakeCompanionTransport()).also { controller ->
            controller.chooseDevice()
            controller.connect("fake-a")
        }

    private fun status(revision: Long, queued: Int, alertId: ULong) = CompanionStatusSnapshot(
        revision = revision,
        radio = CompanionRadioState.READY,
        gnss = CompanionGnssState.SEARCHING,
        power = CompanionPowerState.NORMAL,
        positionSharing = CompanionPositionSharingState.STOPPED,
        queuedActionCount = queued,
        pendingCriticalAlertId = alertId,
    )
}
