package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp")
class V1UnavailableGroupUiTest {
    @get:Rule val ui = createComposeRule()

    @Test fun liveGroupRouteHidesUnavailableSetupAndKeepsTabNavigation() {
        val controller = P1UiController(TrailAppUiState.ChooseMode)
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Group").performClick()
        ui.onNodeWithText("Creating and joining groups, including QR invitations, is coming in a future update.").assertExists()
        for (label in listOf("Create", "Create ✓", "Join", "Join ✓", "Group name",
            "Invitation PIN", "Create private group", "Review invitation", "Scan invitation QR",
            "Confirm matching details")) {
            ui.onNodeWithText(label).assertDoesNotExist()
        }
        ui.onNodeWithText("Messages").performClick()
        ui.onNodeWithText("Customize").assertExists()
    }

    @Test fun supportedDeviceConfirmationRemainsReachableWithoutGroupForms() {
        ui.setContent {
            MaterialTheme {
                V1GroupScreen(v1LiveGroupScreenState(true, V1GroupConfirmationState.Idle), {})
            }
        }
        ui.onNodeWithText("Check for device offer").assertExists()
        ui.onNodeWithText("Group name").assertDoesNotExist()
    }
}
