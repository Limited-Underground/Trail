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
class I023NearbyDevicesPermissionUiTest {
    @get:Rule val ui = createAndroidComposeRule<ComponentActivity>()

    @Test fun firstFindDeviceTapShowsWhyAccessIsNeededBeforeASeparateRequest() {
        val controller = i023PermissionController { NearbyDevicesPermissionState.MISSING }
        var requestAttempts = 0
        val observed = object : TrailUiController by controller {
            override fun beginNearbyDevicesPermissionRequest(): Boolean {
                requestAttempts++
                // Observe the UI admission without starting Android's permission sheet in this test.
                return false
            }
        }
        ui.setContent { MaterialTheme { TrailApp(observed) } }
        ui.mainClock.advanceTimeBy(2_000)

        ui.onNodeWithText("Find device").performScrollTo().performClick()

        ui.runOnIdle { assertEquals(0, requestAttempts) }
        ui.onNodeWithText("Allow Nearby Devices").performScrollTo().performClick()
        ui.runOnIdle { assertEquals(1, requestAttempts) }
    }

    @Test fun settingsAfterAuthorizationOpenThePageNamedOnTheButton() {
        val controller = P1UiController(
            p1BluetoothState(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_STATE_READ_DENIED)).copy(
                authorizationState = DeviceAuthorizationUiState.Accepted(
                    BleDiscoveredCompanion("fixture-board", "Test device"),
                    DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE,
                ),
            ),
        )
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Open app settings").performScrollTo().performClick()

        ui.runOnIdle {
            val intent = shadowOf(ui.activity).nextStartedActivity
            assertEquals(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, intent.action)
            assertEquals(ui.activity.packageName, intent.data?.schemeSpecificPart)
        }
    }
}
