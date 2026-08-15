package io.github.nbjelanovic.otclient

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
}
