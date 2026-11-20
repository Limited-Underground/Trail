package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h640dp-port")
class V1DeviceSettingsScrollTest {
    @get:Rule val ui = createComposeRule()

    @Test fun connectedPortraitSettingsAreInTheDeviceScroll() {
        showConnectedDevice()
        assertSettingsScrollWithStatus()
    }

    @Test
    @Config(qualifiers = "w760dp-h360dp-land")
    fun connectedLandscapeSettingsAreInTheDeviceScroll() {
        showConnectedDevice()
        assertSettingsScrollWithStatus()
    }

    @Test fun firstUseSettingsShareTheOnboardingScroll() {
        show(TrailAppUiState.ChooseMode)
        ui.onNodeWithText("Find your MeshTrail device").assertIsDisplayed()
        ui.onNodeWithText("Find device").assertIsDisplayed()
        ui.onNodeWithText("App settings").performScrollTo().assertIsDisplayed()
        ui.onNodeWithText("Find device").performScrollTo().assertIsDisplayed()
    }

    @Test fun verifiedAndEditableRegionCopyDoesNotPromiseActivity() {
        showConnectedDevice()
        ui.onNodeWithText("Your device follows the radio rules for its saved region.")
            .performScrollTo().assertIsDisplayed()
        ui.onNodeWithText("Edit saved name or region").performScrollTo().performClick()
        ui.onNodeWithText("Choose the region where you use your device.")
            .performScrollTo().assertIsDisplayed()
    }

    @Test fun onboardingRegionHelpUsesSavedRegionRules() {
        ui.setContent {
            MaterialTheme {
                V1OnboardingScreen(V1OnboardingScreenState(stage = V1SetupStage.CONFIRM_RADIO_REGION), {})
            }
        }
        ui.onNodeWithText("Choose the region where you use your device.")
            .performScrollTo().assertIsDisplayed()
    }

    private fun assertSettingsScrollWithStatus() {
        val status = ui.onNodeWithText("Device status", substring = true)
        val settings = ui.onNodeWithText("App settings")
        status.performScrollTo().assertIsDisplayed()
        ui.onNodeWithText("radio status not reported", substring = true).assertIsDisplayed()
        settings.assertIsNotDisplayed()
        settings.performScrollTo().assertIsDisplayed()
        status.assertIsNotDisplayed()
        status.performScrollTo().assertIsDisplayed()
        settings.assertIsNotDisplayed()
    }

    private fun showConnectedDevice() {
        val snapshot = CompanionStatusSnapshot(
            1, CompanionRadioState.UNAVAILABLE, CompanionGnssState.UNKNOWN,
            CompanionPowerState.UNKNOWN, CompanionPositionSharingState.STOPPED, 0,
        )
        val session = BleActiveSession(
            BleDiscoveredCompanion("fixture-board", "Fixture board"), 42, snapshot,
            CompanionProtocolInfo(capabilities = 0xff),
            checkNotNull(GroupLocationSnapshot.authoritativeUnavailable(1, snapshot.positionSharing)),
            configuration = BleConfigurationState(
                available = true, editorSessionId = 1, deviceName = "My board", nameRevision = 1u,
                regionAvailable = true, regionSelectionId = 1, regionRevision = 1u,
                publicProfileAvailable = true, publicName = "My board", publiclyDiscoverable = false,
                publicRevision = 1u,
            ),
        )
        show(TrailAppUiState.BluetoothDevice(
            BleRuntimeState.Ready(session), NearbyDevicesPermissionState.GRANTED,
            false, false, DeviceAuthorizationUiState.None,
            serviceState = ConnectedDeviceServiceUiState.BOUND_ONLY,
        ))
        ui.onNodeWithText("Device").performClick()
    }

    private fun show(screen: TrailAppUiState) {
        val owner = TrailActivityController(
            localController = CompanionAppController(UnavailableProductionLocalTransport),
            permissionReader = NearbyDevicesPermissionReader { NearbyDevicesPermissionState.GRANTED },
            notificationPermissionReader = NotificationPermissionReader { NotificationPermissionState.NOT_REQUIRED },
            serviceConnector = object : ConnectedDeviceServiceConnector {
                override fun startFromVisibleUserAction() = error("Unexpected service start")
                override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit) = error("Unexpected bind")
                override fun stopService() = Unit
                override fun close() = Unit
            },
            serviceStartupScheduler = BleRuntimeScheduler { _, _ -> error("Unexpected scheduling") },
        )
        val controller = object : TrailUiController by owner {
            override val state = screen
            override fun observe(observer: ((TrailAppUiState) -> Unit)?) { observer?.invoke(state) }
        }
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
    }
}
