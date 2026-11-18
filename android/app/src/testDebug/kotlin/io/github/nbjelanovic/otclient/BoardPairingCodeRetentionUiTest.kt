package io.github.nbjelanovic.otclient

import androidx.activity.ComponentActivity
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.lifecycle.ViewModelProvider
import kotlin.test.assertEquals
import kotlin.test.assertNotSame
import kotlin.test.assertNull
import kotlin.test.assertSame
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp-mdpi")
@LooperMode(LooperMode.Mode.PAUSED)
class BoardPairingCodeRetentionUiTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()
    private lateinit var retention: BoardPairingCodeRetention
    private val scope = BoardPairingCodeScope("endpoint", 1L)
    private var pairing by mutableStateOf<BoardPairingState>(BoardPairingState.Available)
    private var now = 1000L
    private var accept = true
    private var submits = 0

    private fun render() {
        compose.activityRule.scenario.onActivity { owner ->
            retention = ViewModelProvider(owner)[BoardPairingCodeRetention::class.java]
        }
        compose.setContent {
            MaterialTheme {
                ExpireBoardPairingCode(retention, clockMillis = { now })
                // The actual home shell owns the viewport and first-frame lifecycle.
                V1HomeScreen(TrailAppUiState.ChooseMode, liveRuntime = { null }) { _, _ ->
                    Column(Modifier.fillMaxSize()) {
                        V1BoardPairingPanel(
                            state = pairing,
                            generateCode = { 42 },
                            clockMillis = { now },
                            retainedInitiatorCode = retention.current(scope, pairing, now),
                            onInitiatorCodeSubmitted = { code ->
                                if (code == null) retention.clear() else retention.retain(scope, code, now)
                            },
                            submit = {
                                submits += 1
                                if (accept) pairing = BoardPairingState.Waiting
                                accept
                            },
                        )
                    }
                }
            }
        }
        compose.mainClock.advanceTimeBy(2_000)
    }

    private fun start() {
        compose.onNodeWithText("Start pairing").performClick()
        compose.onNodeWithText("000042").assertIsDisplayed()
        compose.onNodeWithText("Send to my board").performClick()
    }

    @Test
    fun submittedStarterCodeSurvivesLeavingAndReopeningThePanelWithoutResending() {
        render()
        start()
        compose.onNodeWithText("000042").assertIsDisplayed()
        compose.onNodeWithText("Messages").performClick()
        compose.onNodeWithText("000042").assertDoesNotExist()
        compose.onNodeWithText("Device").performClick()
        compose.onNodeWithText("000042").assertIsDisplayed()
        compose.runOnIdle { assertEquals(1, submits) }
    }

    @Test
    fun codeExpiringWhilePanelIsClosedIsAbsentWhenItReturns() {
        render()
        start()
        compose.onNodeWithText("Messages").performClick()
        compose.runOnIdle {
            now += BOARD_PAIRING_WINDOW_MILLIS
            retention.expire(now)
        }
        compose.onNodeWithText("Device").performClick()
        compose.onNodeWithText("000042").assertDoesNotExist()
        compose.runOnIdle {
            assertNull(retention.expiresAtMillis)
            assertEquals(1, submits)
        }
    }

    @Test
    fun refusedSubmissionDoesNotRetainOrDisplayTheCode() {
        accept = false
        render()
        start()
        compose.onNodeWithText("000042").assertDoesNotExist()
        compose.onNodeWithText("Couldn't send the pairing request. Check the connection and try again.")
            .assertIsDisplayed()
        compose.runOnIdle { assertNull(retention.expiresAtMillis) }
    }
}

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp-mdpi")
@LooperMode(LooperMode.Mode.PAUSED)
class BoardPairingCodeActivityTest {
    @get:Rule val activity = createAndroidComposeRule<ComponentActivity>()

    @Test
    fun activityRecreationRetainsOnlyTheLiveInMemoryViewModel() {
        val firstActivity = activity.activity
        val first = ViewModelProvider(firstActivity)[BoardPairingCodeRetention::class.java]
        val scope = BoardPairingCodeScope("endpoint", 1L)
        first.retain(scope, "000042", 100L)

        activity.activityRule.scenario.recreate()

        val secondActivity = activity.activity
        val restored = ViewModelProvider(secondActivity)[BoardPairingCodeRetention::class.java]
        assertNotSame(firstActivity, secondActivity)
        assertSame(first, restored)
        assertEquals("000042", restored.current(scope, BoardPairingState.Waiting, 101L))
        restored.expire(300100L)
        assertNull(restored.expiresAtMillis)
    }
}
