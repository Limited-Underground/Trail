package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextReplacement
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionEnrollmentRole
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w411dp-h891dp")
class V1BoardPairingUiTest {
    @get:Rule val ui = createComposeRule()
    private val state = mutableStateOf<BoardPairingState?>(BoardPairingState.Available)
    private var submitted: CompanionActionRequest? = null
    private var now = 100L

    private fun show() {
        ui.setContent {
            MaterialTheme {
                Column {
                    state.value?.let { status ->
                        V1BoardPairingPanel(status, generateCode = { 42 }, clockMillis = { now }) {
                            submitted = it
                            state.value = BoardPairingState.Waiting
                            true
                        }
                    }
                }
            }
        }
    }

    @Test fun startCodeStaysVisibleUntilThePairingWindowEnds() {
        show()
        ui.onNodeWithText("Start pairing").performClick()
        ui.onNodeWithText("000042").assertExists()
        ui.runOnIdle { assertNull(submitted) }
        ui.onNodeWithText("Send to my board").performClick()
        ui.onNodeWithText("000042").assertExists()
        ui.onNodeWithText("Waiting for your board to answer.").assertExists()
        ui.runOnIdle {
            assertEquals(CompanionEnrollmentRole.INITIATOR, submitted?.enrollmentRole)
            assertEquals(42, submitted?.pairingCode)
        }

        ui.runOnIdle { state.value = BoardPairingState.Ready(now + BOARD_PAIRING_WINDOW_MILLIS) }
        ui.onNodeWithText("Pairing window: 2:00 left").assertExists()
        ui.onNodeWithText("000042").assertExists()
        ui.onNodeWithText("Start pairing").assertDoesNotExist()
        ui.runOnIdle { now += BOARD_PAIRING_WINDOW_MILLIS }
        ui.mainClock.advanceTimeBy(1_100)
        ui.onNodeWithText("The two-minute pairing window ended.").assertExists()
        ui.onNodeWithText("000042").assertDoesNotExist()
        ui.onNodeWithText("Start pairing").assertExists()
    }

    @Test fun starterCodeClearsOnRefusalOrCancel() {
        show()
        ui.onNodeWithText("Start pairing").performClick()
        ui.onNodeWithText("Cancel").performClick()
        ui.onNodeWithText("000042").assertDoesNotExist()
        ui.runOnIdle { assertNull(submitted) }

        ui.onNodeWithText("Start pairing").performClick()
        ui.onNodeWithText("Send to my board").performClick()
        ui.onNodeWithText("000042").assertExists()
        ui.runOnIdle { state.value = BoardPairingState.Rejected(CompanionActionRejectReason.POLICY_DENIED) }
        ui.onNodeWithText("000042").assertDoesNotExist()

        ui.onNodeWithText("Start pairing").performClick()
        ui.onNodeWithText("Send to my board").performClick()
        ui.onNodeWithText("000042").assertExists()
        ui.runOnIdle { state.value = null }
        ui.onNodeWithText("000042").assertDoesNotExist()
    }

    @Test fun joinRequiresSixDigitsAndShowsExplicitRefusalOrDisconnect() {
        show()
        ui.onNodeWithText("Join pairing").performClick()
        val field = ui.onNodeWithText("6-digit pairing code")
        val send = ui.onNodeWithText("Send to my board")
        field.performTextReplacement("12345")
        send.assertIsNotEnabled()
        field.performTextReplacement("000000")
        send.assertIsNotEnabled()
        field.performTextReplacement("000019")
        send.assertIsEnabled().performClick()
        ui.runOnIdle {
            assertEquals(CompanionEnrollmentRole.RESPONDER, submitted?.enrollmentRole)
            assertEquals(19, submitted?.pairingCode)
            state.value = BoardPairingState.Rejected(CompanionActionRejectReason.POLICY_DENIED)
        }
        ui.onNodeWithText("Your board did not allow pairing.").assertExists()
        ui.runOnIdle { state.value = BoardPairingState.Rejected(CompanionActionRejectReason.BUSY) }
        ui.onNodeWithText("Your board already has a pairing request. Wait before trying again.").assertExists()
        ui.runOnIdle { state.value = BoardPairingState.Rejected(CompanionActionRejectReason.UNAVAILABLE) }
        ui.onNodeWithText("Your board is unavailable for pairing right now.").assertExists()
        ui.onNodeWithText("000019").assertDoesNotExist()
        ui.runOnIdle { state.value = null }
        ui.onNodeWithText("Join pairing").assertDoesNotExist()
    }
}
