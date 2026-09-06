package io.github.nbjelanovic.otclient

import kotlin.test.*

class V1TestServiceObservationTest {
    @Test fun recordsWithoutUiBindingAndClosesExactlyOnce() {
        val traces = mutableListOf<Pair<Long, TrailAppUiState.BluetoothDevice>>()
        val categories = mutableListOf<V1TestPhoneConnectionState>()
        val releases = mutableListOf<Long>()
        val observer = V1TestServiceObservation(7, { generation, state -> traces += generation to state },
            categories::add, releases::add)
        val initial = state(BleRuntimeState.Idle)
        val connecting = state(BleRuntimeState.Connecting(BleDiscoveredCompanion("test-token", "Test")))
        observer.onState(initial)
        observer.onState(connecting)
        assertEquals(listOf(7L to initial, 7L to connecting), traces)
        assertEquals(2, categories.size)
        assertTrue(releases.isEmpty())
        observer.close()
        observer.close()
        observer.onState(initial)
        assertEquals(listOf(7L), releases)
        assertEquals(2, traces.size)
        assertEquals(2, categories.size)
    }

    @Test fun independentDiagnosticFailuresDoNotEscapeOrSuppressCleanup() {
        var categories = 0
        var releases = 0
        val observer = V1TestServiceObservation(1, { _, _ -> error("trace unavailable") },
            { categories++; error("log unavailable") }, { releases++; error("release unavailable") })
        observer.onState(state(BleRuntimeState.Idle))
        observer.close()
        observer.close()
        observer.onState(state(BleRuntimeState.Idle))
        assertEquals(1, categories)
        assertEquals(1, releases)
    }

    @Test fun staleOwnerCloseCannotTerminateNewOwnerTransaction() {
        val machine = V1TestConnectionTraceMachine()
        val connecting = state(BleRuntimeState.Connecting(BleDiscoveredCompanion("test-token", "Test")))
        machine.observe(7, 10, connecting)
        machine.observe(8, 20, connecting)
        assertTrue(machine.observeServiceDisconnected(7, 1).isEmpty())
        assertEquals(1L, machine.staleObservations)
        assertNull(machine.failure)
        assertTrue(machine.observeServiceDisconnected(9, 21).isEmpty())
        assertTrue(machine.observeServiceDisconnected(8, 22).isNotEmpty())
        assertTrue(machine.observeServiceDisconnected(8, 23).isEmpty())
    }

    private fun state(runtime: BleRuntimeState) = TrailAppUiState.BluetoothDevice(
        runtimeState = runtime,
        permissionState = NearbyDevicesPermissionState.GRANTED,
        permissionRequestInFlight = false,
        permissionWasDenied = false,
        authorizationState = DeviceAuthorizationUiState.None,
    )
}
