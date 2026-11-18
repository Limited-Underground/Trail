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
class V1SetupLaterUiTest {
    @get:Rule val ui = createComposeRule()

    @Test fun setUpLaterOpensMessagesAndReturningStillShowsSetup() {
        val controller = P1UiController(TrailAppUiState.ChooseMode)
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Set up later").performScrollTo().performClick()
        ui.onNodeWithText("Customize").assertExists()
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Find device").performScrollTo().assertExists()
        ui.runOnIdle { assertEquals(0, controller.connectionRequests) }
    }
}
