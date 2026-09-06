package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class V1SupportHealthProjectionTest {
    @Test
    fun readyUsesOnlyTheAuthoritativeDeviceSnapshot() {
        val projection = V1SupportHealthProjector.from(
            bluetooth(
                BleRuntimeState.Ready(
                    session(
                        radio = CompanionRadioState.READY,
                        gnss = CompanionGnssState.SEARCHING,
                        power = CompanionPowerState.EXTERNAL,
                    ),
                ),
            ),
        )
        assertEquals(V1SupportPhoneConnection.READY, projection.connection)
        assertEquals(V1SupportRadioHealth.READY, projection.radio)
        assertEquals(V1SupportGpsHealth.SEARCHING, projection.gps)
        assertEquals(V1SupportPowerHealth.EXTERNAL, projection.power)
        assertNull(projection.lastCode)
    }

    @Test
    fun everyAuthoritativeHealthValueMapsWithoutPowerOrChargeInference() {
        assertEquals(
            V1SupportRadioHealth.entries.take(5),
            CompanionRadioState.entries.map { ready(radio = it).radio },
        )
        assertEquals(
            V1SupportGpsHealth.entries.take(6),
            CompanionGnssState.entries.map { ready(gnss = it).gps },
        )
        assertEquals(
            V1SupportPowerHealth.entries.take(6),
            CompanionPowerState.entries.map { ready(power = it).power },
        )
    }

    @Test
    fun nonReadyStatesNeverReuseOrInventDeviceHealth() {
        val states = listOf(
            BleRuntimeState.Idle,
            BleRuntimeState.Scanning(emptyList()),
            BleRuntimeState.AwaitingAuthorization(companion),
            BleRuntimeState.Reconnecting(companion, 2, 3),
            BleRuntimeState.Failed(BleRuntimeFailure.RECONNECT_EXHAUSTED),
        )
        states.forEach { state ->
            val projection = V1SupportHealthProjector.from(bluetooth(state))
            assertEquals(V1SupportRadioHealth.UNKNOWN, projection.radio)
            assertEquals(V1SupportGpsHealth.UNKNOWN, projection.gps)
            assertEquals(V1SupportPowerHealth.UNKNOWN, projection.power)
        }
        assertEquals(
            V1SupportCode.BLE_RECONNECT,
            V1SupportHealthProjector.from(
                bluetooth(BleRuntimeState.Failed(BleRuntimeFailure.RECONNECT_EXHAUSTED)),
            ).lastCode,
        )
    }

    @Test
    fun authorizationAndInternalFailuresUseBoundedPublicCodes() {
        assertEquals(
            V1SupportCode.BLE_AUTHORIZATION,
            V1SupportHealthProjector.from(
                bluetooth(BleRuntimeState.Failed(BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE)),
            ).lastCode,
        )
        assertEquals(
            V1SupportCode.APP,
            V1SupportHealthProjector.from(
                bluetooth(BleRuntimeState.Failed(BleRuntimeFailure.PROTOCOL_VIOLATION)),
            ).lastCode,
        )
    }

    @Test
    fun chooserAndLocalTestDoNotClaimPhysicalHealth() {
        assertEquals(
            V1SupportPhoneConnection.DISCONNECTED,
            V1SupportHealthProjector.from(TrailAppUiState.ChooseMode).connection,
        )
        val local = V1SupportHealthProjector.from(
            TrailAppUiState.LocalTest(CompanionUiState.Disconnected(emptyList())),
        )
        assertEquals(V1SupportPhoneConnection.UNKNOWN, local.connection)
        assertEquals(V1SupportRadioHealth.UNKNOWN, local.radio)
    }

    private fun ready(
        radio: CompanionRadioState = CompanionRadioState.UNKNOWN,
        gnss: CompanionGnssState = CompanionGnssState.UNKNOWN,
        power: CompanionPowerState = CompanionPowerState.UNKNOWN,
    ): V1SupportHealthProjection = V1SupportHealthProjector.from(
        bluetooth(BleRuntimeState.Ready(session(radio, gnss, power))),
    )

    private fun session(
        radio: CompanionRadioState,
        gnss: CompanionGnssState,
        power: CompanionPowerState,
    ) = BleActiveSession(
        companion = companion,
        sessionNonce = 1,
        snapshot = CompanionStatusSnapshot(
            revision = 1,
            radio = radio,
            gnss = gnss,
            power = power,
            positionSharing = CompanionPositionSharingState.STOPPED,
            queuedActionCount = 0,
        ),
        protocolInfo = CompanionProtocolInfo(capabilities = 0),
        groupLocation = requireNotNull(
            GroupLocationSnapshot.authoritativeUnavailable(1, CompanionPositionSharingState.STOPPED),
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
