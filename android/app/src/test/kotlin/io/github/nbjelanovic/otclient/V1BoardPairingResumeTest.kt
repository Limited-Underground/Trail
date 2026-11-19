package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import io.github.nbjelanovic.otprotocol.*
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode

/** Host Compose interaction; no phone, board gesture or RF acceptance. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp-mdpi")
@LooperMode(LooperMode.Mode.PAUSED)
class V1BoardPairingResumeTest {
    @get:Rule val compose = createComposeRule()
    private val sent = mutableListOf<CompanionActionRequest>()
    private val incomplete = BoardPairingState.Result(CompanionPairingResult(
        CompanionPairingState.INCOMPLETE, originExchangeId = 900,
        originSessionNonce = 888, revision = 2,
    ), historical = true)

    private fun render() {
        compose.setContent {
            MaterialTheme {
                Column {
                    V1BoardPairingPanel(incomplete, generateCode = { 123456 }, clockMillis = { 100 }) {
                        sent += it
                        true
                    }
                }
            }
        }
    }

    @Test fun resumeRequiresAnExplicitTapAndCancelPreservesTheIncompleteResult() {
        render()
        compose.onNodeWithText("Pairing incomplete.").assertIsDisplayed()
        compose.onNodeWithText("Start pairing").assertDoesNotExist()
        compose.onNodeWithText("Join pairing").assertDoesNotExist()
        assertTrue(sent.isEmpty())
        compose.onNodeWithText("Resume pairing").performClick()
        compose.onNodeWithText("Start pairing").performClick()
        compose.onNodeWithText("Cancel").performClick()
        compose.onNodeWithText("Pairing incomplete.").assertIsDisplayed()
        assertTrue(sent.isEmpty())
        compose.onNodeWithText("Start pairing").performClick()
        compose.onNodeWithText("Send to my board").performClick()
        assertEquals(listOf(CompanionActionRequest(
            CompanionActionKind.START_ENROLLMENT,
            enrollmentRole = CompanionEnrollmentRole.INITIATOR, pairingCode = 123456,
        )), sent)
    }

    @Test fun resumeJoinUsesTheNewMatchingCodeOnlyAfterExplicitSend() {
        render()
        compose.onNodeWithText("On both phones, tap Resume pairing. Use Start pairing on the phone that started it before, and Join pairing on the other with the same new code.")
            .assertIsDisplayed()
        compose.onNodeWithText("Resume pairing").performClick()
        compose.onNodeWithText("Join pairing").performClick()
        compose.onNodeWithText("6-digit pairing code").performTextInput("123456")
        assertTrue(sent.isEmpty())
        compose.onNodeWithText("Send to my board").performClick()
        assertEquals(listOf(CompanionActionRequest(
            CompanionActionKind.START_ENROLLMENT,
            enrollmentRole = CompanionEnrollmentRole.RESPONDER, pairingCode = 123456,
        )), sent)
    }
}
