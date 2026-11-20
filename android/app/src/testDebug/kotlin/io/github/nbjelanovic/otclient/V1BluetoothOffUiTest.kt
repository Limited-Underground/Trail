package io.github.nbjelanovic.otclient

import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp")
class V1BluetoothOffUiTest {
    @get:Rule val ui = createAndroidComposeRule<ComponentActivity>()

    @Test fun opensSettingsThenWaitsForAnExplicitRoutePreservingRetry() {
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_DISABLED)))
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Open Bluetooth settings").performScrollTo().performClick()
        ui.runOnIdle {
            assertEquals(Settings.ACTION_BLUETOOTH_SETTINGS, shadowOf(ui.activity).nextStartedActivity.action)
            assertEquals(0, controller.connectionRequests)
            assertEquals(0, controller.discoveryRetries)
        }
        ui.onNodeWithText("Try again").performScrollTo().performClick()
        ui.runOnIdle {
            assertEquals(1, controller.discoveryRetries)
            assertEquals(0, controller.connectionRequests)
        }
    }

    @Test fun refusedRetryShowsPermissionRecoveryAndClearsAfterAdmittedRetry() {
        val guidance = "MeshTrail needs Nearby Devices access. Open app settings, tap Permissions, and allow Nearby devices."
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_DISABLED)))
        controller.discoveryRetryRefusal = BluetoothRecoveryRefusal.PERMISSION_REQUIRED
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Try again").performScrollTo().performClick()
        ui.onNodeWithText(guidance).assertExists()
        ui.onNodeWithText("Open app settings").performScrollTo().performClick()
        ui.runOnIdle {
            val intent = shadowOf(ui.activity).nextStartedActivity
            assertEquals(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, intent.action)
            assertEquals(ui.activity.packageName, intent.data?.schemeSpecificPart)
            assertEquals(1, controller.discoveryRetries)
            assertEquals(0, controller.connectionRequests)
            controller.discoveryRetryRefusal = null
        }
        ui.onNodeWithText("Try again").performScrollTo().performClick()
        ui.onNodeWithText(guidance).assertDoesNotExist()
        ui.onNodeWithText("Open app settings").assertDoesNotExist()
        ui.runOnIdle { assertEquals(2, controller.discoveryRetries) }
    }

    @Test fun resetRefusalOffersRecoveryWithoutStartingAnotherScanOrReset() {
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_DISABLED)))
        controller.discoveryRetryRefusal = BluetoothRecoveryRefusal.RESET_PENDING
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Try again").performScrollTo().performClick()
        ui.onNodeWithText("The earlier board reset still needs verification.", substring = true).assertExists()
        val appName = ui.activity.getString(R.string.app_name)
        ui.onNodeWithText("choose $appName, and tap Force stop", substring = true).assertExists()
        ui.onNodeWithText("Open app settings").assertExists()
        ui.onNodeWithText("Scan again").assertDoesNotExist()
        ui.runOnIdle {
            assertEquals(1, controller.discoveryRetries)
            assertEquals(0, controller.connectionRequests)
            assertEquals(0, controller.resetConfirmations)
        }
    }

    @Test fun deniedBluetoothStateReadOffersSettingsAndRetry() {
        val controller = P1UiController(
            p1BluetoothState(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_STATE_READ_DENIED)),
        )
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Open Bluetooth settings").assertDoesNotExist()
        ui.onNodeWithText("Open app settings").assertExists()
        ui.onNodeWithText("Try again").performScrollTo().performClick()
        ui.runOnIdle {
            assertEquals(1, controller.discoveryRetries)
            assertEquals(0, controller.connectionRequests)
        }
    }

    @Test fun unrelatedBlockersDoNotOfferBluetoothSettingsAsARecovery() {
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.Blocked(BleRuntimeBlock.PLATFORM_UNSUPPORTED)))
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Open Bluetooth settings").assertDoesNotExist()
        ui.onNodeWithText("Try again").assertDoesNotExist()
        ui.runOnIdle { assertEquals(0, controller.discoveryRetries) }
    }
}
