package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1DeviceStatusStripStateTest {
    @Test
    fun readyMapsOnlyObservedConnectionAndGnss() {
        val state = V1DeviceStatusStripProjector.from(bluetooth(ready(CompanionGnssState.CURRENT)))
        assertTrue(state.bluetoothConnected)
        assertEquals(V1DeviceGpsStatus.CURRENT, state.gps)
        assertNull(state.satellites)
        assertNull(state.batteryPercent)
        assertEquals(true, state.externalPower)
        assertNull(state.transmitting)
        assertNull(state.receiving)
    }

    @Test
    fun everyAuthoritativeGnssStateMapsExactly() {
        assertEquals(
            V1DeviceGpsStatus.entries,
            CompanionGnssState.entries.map {
                V1DeviceStatusStripProjector.from(bluetooth(ready(it))).gps
            },
        )
    }

    @Test
    fun onlyExplicitExternalPowerProjectsAPlugIndicator() {
        assertEquals(
            true,
            V1DeviceStatusStripProjector.from(
                bluetooth(ready(CompanionGnssState.UNKNOWN, CompanionPowerState.EXTERNAL)),
            ).externalPower,
        )
        listOf(
            CompanionPowerState.UNKNOWN,
            CompanionPowerState.NORMAL,
            CompanionPowerState.LOW,
            CompanionPowerState.CRITICAL,
            CompanionPowerState.FAULT,
        ).forEach { power ->
            assertNull(
                V1DeviceStatusStripProjector.from(
                    bluetooth(ready(CompanionGnssState.UNKNOWN, power)),
                ).externalPower,
            )
        }
    }

    @Test
    fun reconnectAndOtherNonReadyStatesDiscardOldMetrics() {
        listOf(
            TrailAppUiState.ChooseMode,
            bluetooth(BleRuntimeState.Reconnecting(companion, 1, 3)),
            bluetooth(BleRuntimeState.Failed(BleRuntimeFailure.RECONNECT_EXHAUSTED)),
        ).forEach { appState ->
            val state = V1DeviceStatusStripProjector.from(appState)
            assertFalse(state.bluetoothConnected)
            assertEquals(V1DeviceGpsStatus.UNKNOWN, state.gps)
            assertNull(state.satellites)
            assertNull(state.batteryPercent)
            assertNull(state.externalPower)
            assertNull(state.transmitting)
            assertNull(state.receiving)
        }
    }

    @Test
    fun metricRangesFailClosed() {
        listOf(-1, 100).forEach { invalid ->
            assertFailsWith<IllegalArgumentException> { connected(satellites = invalid) }
        }
        listOf(-1, 101).forEach { invalid ->
            assertFailsWith<IllegalArgumentException> { connected(batteryPercent = invalid) }
        }
        connected(satellites = 0, batteryPercent = 0)
        connected(satellites = 99, batteryPercent = 100)
    }

    @Test
    fun disconnectedStateCannotCarryStaleTelemetry() {
        assertFailsWith<IllegalArgumentException> {
            V1DeviceStatusStripState.disconnected().copy(gps = V1DeviceGpsStatus.STALE)
        }
        assertFailsWith<IllegalArgumentException> {
            V1DeviceStatusStripState.disconnected().copy(batteryPercent = 50)
        }
    }

    private fun connected(
        satellites: Int? = null,
        batteryPercent: Int? = null,
    ) = V1DeviceStatusStripState(
        bluetoothConnected = true,
        gps = V1DeviceGpsStatus.UNKNOWN,
        satellites = satellites,
        batteryPercent = batteryPercent,
        externalPower = null,
        transmitting = null,
        receiving = null,
    )

    private fun ready(
        gnss: CompanionGnssState,
        power: CompanionPowerState = CompanionPowerState.EXTERNAL,
    ) = BleRuntimeState.Ready(
        BleActiveSession(
            companion = companion,
            sessionNonce = 1,
            snapshot = CompanionStatusSnapshot(
                revision = 1,
                radio = CompanionRadioState.READY,
                gnss = gnss,
                power = power,
                positionSharing = CompanionPositionSharingState.STOPPED,
                queuedActionCount = 99,
            ),
            protocolInfo = CompanionProtocolInfo(capabilities = 0),
            groupLocation = requireNotNull(
                GroupLocationSnapshot.authoritativeUnavailable(
                    1,
                    CompanionPositionSharingState.STOPPED,
                ),
            ),
        ),
    )

    private fun bluetooth(runtime: BleRuntimeState) = TrailAppUiState.BluetoothDevice(
        runtimeState = runtime,
        permissionState = NearbyDevicesPermissionState.GRANTED,
        permissionRequestInFlight = false,
        permissionWasDenied = false,
        authorizationState = DeviceAuthorizationUiState.None,
    )

    private companion object {
        val companion = BleDiscoveredCompanion("opaque-test-token", "Test companion")
    }
}
