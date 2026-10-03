package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import io.github.nbjelanovic.otprotocol.RegionSelectionCatalog
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1SupportRegionProjectionTest {
    @Test
    fun currentReadyPositiveReadbackUsesOnlyTheRepresentedCatalogRegion() {
        assertEquals(V1RadioRegion.US915, project(configuration))
        assertEquals(V1RadioRegion.US915, project(configuration.copy(regionRevision = ULong.MAX_VALUE)))
        RegionSelectionCatalog.entries.filter { it.code != "US915" }.forEach {
            assertNull(project(configuration.copy(regionSelectionId = it.id)), it.code)
        }
        listOf(null, 0, -1, 13, 65535).forEach {
            assertNull(project(configuration.copy(regionSelectionId = it)))
        }
    }

    @Test
    fun availabilityAndPositiveRevisionAreRequiredEvenWithRetainedChoiceAndSuccessNotice() {
        listOf(
            configuration.copy(available = false),
            configuration.copy(regionAvailable = false),
            configuration.copy(regionRevision = null),
            configuration.copy(regionRevision = 0uL),
            configuration.copy(busy = true, regionRevision = null, notice = "Waiting for region readback..."),
            configuration.copy(regionRevision = null, notice = "Region saved and read back. Radio TX remains disabled."),
        ).forEach { assertNull(project(it)) }
    }

    @Test
    fun everyNonReadyRuntimeStateIncludingResetRequestRemovesTheRegion() {
        val session = session(configuration)
        val states = listOf(
            BleRuntimeState.Inactive, BleRuntimeState.Idle, BleRuntimeState.Closed,
            BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_UNAVAILABLE),
            BleRuntimeState.Scanning(emptyList()), BleRuntimeState.ScanComplete(emptyList()),
            BleRuntimeState.FindingReturningOwner, BleRuntimeState.AwaitingAuthorization(companion),
            BleRuntimeState.Connecting(companion),
            BleRuntimeState.Negotiating(companion, BleNegotiationPhase.INITIAL_SNAPSHOT),
            BleRuntimeState.Reconnecting(companion, 1, 3),
            BleRuntimeState.Failed(BleRuntimeFailure.RECONNECT_EXHAUSTED),
            BleRuntimeState.FactoryResetRequesting(session), BleRuntimeState.FactoryResetErasing(companion),
            BleRuntimeState.FactoryResetVerifying(companion),
            BleRuntimeState.FactoryResetNotVerified(companion, FactoryResetNotVerifiedReason.RESPONSE_TIMEOUT, true),
            BleRuntimeState.FactoryResetComplete(false),
        )
        states.forEach { assertNull(V1SupportRegionProjector.from(bluetooth(it)), it.toString()) }
        assertEquals(V1SupportPhoneConnection.READY,
            V1SupportHealthProjector.from(bluetooth(BleRuntimeState.FactoryResetRequesting(session))).connection)
    }

    @Test
    fun chooserLocalModeAndNextSessionNeverReuseThePreviousReadback() {
        assertEquals(V1RadioRegion.US915, project(configuration))
        assertNull(V1SupportRegionProjector.from(TrailAppUiState.ChooseMode))
        assertNull(V1SupportRegionProjector.from(TrailAppUiState.LocalTest(CompanionUiState.Disconnected(emptyList()))))
        assertNull(project(BleConfigurationState(available = true, regionAvailable = true, editorSessionId = 999)))
    }

    @Test
    fun regionBindingCannotAddSessionSecretsToTheAutomaticReport() {
        val privateConfiguration = configuration.copy(
            deviceName = "private-device-name-sentinel", publicName = "private-public-name-sentinel",
            suggestedDeviceName = "Trail-23ABCD", publicRevision = 31337uL, nameRevision = 8181uL,
            publiclyDiscoverable = false, notice = "private-notice-sentinel",
        )
        val state = bluetooth(BleRuntimeState.Ready(session(privateConfiguration)))
        val health = V1SupportHealthProjector.from(state)
        fun label(value: String) = requireNotNull(V1PublicDiagnosticLabel.create(value))
        val snapshot = V1SafeDiagnosticSnapshot(
            label("1.0.0"), label("Not available"), label("Test phone"), label("13"), label("Not available"),
            V1SupportRegionProjector.from(state), health.connection, health.radio, health.gps, health.power,
            null, health.lastCode,
        )
        val report = requireNotNull(V1SupportReportGenerator.create(snapshot, ""))
        assertTrue(report.text.contains("Radio region: US915"))
        assertTrue(report.text.contains("Firmware: Not available"))
        assertTrue(report.text.contains("Device: Not available"))
        listOf("private-device-name-sentinel", "private-public-name-sentinel", "private-endpoint-sentinel",
            "private-notice-sentinel", "private-advertisement-sentinel", "Trail-23ABCD", "515151", "424242",
            "31337", "8181", "626262", "727272").forEach { assertFalse(report.text.contains(it), it) }
        assertEquals("V1SafeDiagnosticSnapshot(redacted)", snapshot.toString())
        assertEquals(report, V1SupportReportGenerator.create(snapshot, ""))
    }

    private fun project(value: BleConfigurationState) =
        V1SupportRegionProjector.from(bluetooth(BleRuntimeState.Ready(session(value))))

    private fun session(value: BleConfigurationState) = BleActiveSession(
        companion, 424242,
        CompanionStatusSnapshot(626262, CompanionRadioState.READY, CompanionGnssState.SEARCHING,
            CompanionPowerState.NORMAL, CompanionPositionSharingState.STOPPED, 0),
        CompanionProtocolInfo(capabilities = 0),
        requireNotNull(GroupLocationSnapshot.authoritativeUnavailable(424242, CompanionPositionSharingState.STOPPED)),
        configuration = value,
    )

    private fun bluetooth(runtime: BleRuntimeState) = TrailAppUiState.BluetoothDevice(
        runtime, NearbyDevicesPermissionState.GRANTED, false, false, DeviceAuthorizationUiState.None,
    )

    private companion object {
        val companion = BleDiscoveredCompanion("private-endpoint-sentinel", "private-advertisement-sentinel")
        val configuration = BleConfigurationState(available = true, editorSessionId = 515151,
            regionAvailable = true, regionSelectionId = 1, regionRevision = 727272uL)
    }
}
