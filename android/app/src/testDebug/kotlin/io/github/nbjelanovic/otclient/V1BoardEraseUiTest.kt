package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp")
class V1BoardEraseUiTest {
    @get:Rule val ui = createComposeRule()

    @Test fun namesTheCurrentBoardAndCancelDoesNotErase() {
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.Ready(p1Session())))
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Device settings").performScrollTo().performClick()
        ui.onNodeWithText("Review factory reset").performClick()
        ui.onNodeWithText("Erase Camp board?").assertExists()
        ui.onNodeWithText(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT).assertExists()
        ui.onNodeWithText("Erase this device").assertExists()
        ui.runOnIdle { assertEquals(0, controller.resetConfirmations) }
        ui.onNodeWithText("Cancel").performClick()
        ui.onNodeWithText("Erase Camp board?").assertDoesNotExist()
        ui.runOnIdle { assertEquals(0, controller.resetConfirmations) }
    }
}
