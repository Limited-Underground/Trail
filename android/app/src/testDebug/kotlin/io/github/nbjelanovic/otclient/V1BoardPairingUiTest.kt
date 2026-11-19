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
import io.github.nbjelanovic.otprotocol.CompanionPairingResult
import io.github.nbjelanovic.otprotocol.CompanionPairingState
import io.github.nbjelanovic.otprotocol.CompanionPairingReason
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
        ui.onNodeWithText("Pairing window: 5:00 left").assertExists()  // D-110
        ui.onNodeWithText("000042").assertExists()
        ui.onNodeWithText("Start pairing").assertDoesNotExist()
        ui.runOnIdle { now += BOARD_PAIRING_WINDOW_MILLIS }
        ui.mainClock.advanceTimeBy(1_100)
        ui.onNodeWithText("Pairing result unavailable.").assertExists()
        ui.onNodeWithText("Check your MeshTrail board screen.").assertExists()
        ui.onNodeWithText("000042").assertDoesNotExist()
        ui.onNodeWithText("Start pairing").assertExists()
    }

    @Test fun boardResultPreemptsCountdownAndShowsConfirmedPeerSuffix() {
        show()
        ui.onNodeWithText("Start pairing").performClick()
        ui.onNodeWithText("Send to my board").performClick()
        ui.runOnIdle { state.value = BoardPairingState.Ready(now + BOARD_PAIRING_WINDOW_MILLIS) }
        val result = CompanionPairingResult(
            CompanionPairingState.PAIRED_LOCAL, peerShortId = 0x000a,
            originExchangeId = 1, originSessionNonce = 2, revision = 1,
        )
        ui.runOnIdle { state.value = BoardPairingState.Result(result) }
        ui.onNodeWithText("Paired with board 000A").assertExists()
        ui.onNodeWithText("Saved on this board. Check the result on the other board.").assertExists()
        ui.onNodeWithText("000042").assertDoesNotExist()
        ui.onNodeWithText("Pairing window: 5:00 left").assertDoesNotExist()
        ui.runOnIdle { state.value = BoardPairingState.Result(result.copy(state = CompanionPairingState.PAIRED_BOTH)) }
        ui.onNodeWithText("Both boards paired").assertExists()
    }

    @Test fun failedNoReplyShowsCodeHintAndRetainedIncompleteBlocksAnotherAttempt() {
        val result = CompanionPairingResult(
            CompanionPairingState.FAILED, CompanionPairingReason.NO_REPLY,
            originExchangeId = 1, originSessionNonce = 2, revision = 1,
        )
        state.value = BoardPairingState.Result(result)
        show()
        ui.onNodeWithText("The other board didn't answer.").assertExists()
        ui.onNodeWithText("Bring the boards closer and check that both phones used the same pairing code.").assertExists()
        ui.runOnIdle { state.value = BoardPairingState.Result(result.copy(
            state = CompanionPairingState.INCOMPLETE, reason = CompanionPairingReason.NONE,
        ), historical = true) }
        ui.onNodeWithText("Last pairing attempt").assertExists()
        ui.onNodeWithText("Pairing incomplete.").assertExists()
        ui.onNodeWithText("Start pairing").assertDoesNotExist()
        ui.onNodeWithText("Join pairing").assertDoesNotExist()
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
