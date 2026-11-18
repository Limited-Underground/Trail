package io.github.nbjelanovic.otclient

import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.lifecycle.ViewModelProvider
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp")
@LooperMode(LooperMode.Mode.PAUSED)
class V1PairingCodeNavigationUiTest {
    @get:Rule val ui = createAndroidComposeRule<ComponentActivity>()
    private val host = P1UiController(p1BluetoothState(BleRuntimeState.Ready(
        p1Session().copy(boardPairing = BoardPairingState.Available),
    )))
    private var pairingRequests = 0
    private val controller = object : TrailUiController by host {
        override fun submitBluetoothAction(request: CompanionActionRequest): Boolean {
            pairingRequests++
            val screen = host.state as TrailAppUiState.BluetoothDevice
            val ready = screen.runtimeState as BleRuntimeState.Ready
            host.publish(screen.copy(runtimeState = BleRuntimeState.Ready(
                ready.session.copy(boardPairing = BoardPairingState.Waiting),
            )))
            return true
        }
    }

    private fun start(): String {
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText("Start pairing").performScrollTo().performClick()
        val matcher = SemanticsMatcher("six-digit starter code") { node ->
            node.config.getOrNull(SemanticsProperties.Text)?.any {
                it.text.matches(Regex("[0-9]{6}"))
            } == true
        }
        val code = ui.onNode(matcher).fetchSemanticsNode()
            .config[SemanticsProperties.Text].single().text
        ui.onNodeWithText("Send to my board").performScrollTo().performClick()
        ui.onNodeWithText(code).assertExists()
        return code
    }

    @Test fun actualDeviceTabRetainsTheCodeWithoutAnotherRequestAndClearsOnDisconnect() {
        val code = start()
        ui.onNodeWithText("Messages").performClick()
        ui.onNode(hasText("Group") and SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Tab)).performClick()
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText(code).assertExists()
        ui.onNodeWithText("Messages").performClick()
        ui.runOnIdle {
            val screen = host.state as TrailAppUiState.BluetoothDevice
            host.publish(screen.copy(runtimeState = BleRuntimeState.Idle))
        }
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText(code).assertDoesNotExist()
        ui.runOnIdle { assertEquals(1, pairingRequests) }
    }

    @Test fun recreatedActivityReattachesTheActualUiToTheSameAttempt() {
        val code = start()
        val before = ViewModelProvider(ui.activity)[BoardPairingCodeRetention::class.java]
        ui.activityRule.scenario.recreate()
        ui.activityRule.scenario.onActivity { activity ->
            activity.setContent { MaterialTheme { TrailApp(controller) } }
        }
        ui.mainClock.advanceTimeBy(2_000)
        ui.onNodeWithText("Device").performClick()
        ui.onNodeWithText(code).assertExists()
        ui.runOnIdle {
            assertSame(before, ViewModelProvider(ui.activity)[BoardPairingCodeRetention::class.java])
            assertEquals(1, pairingRequests)
        }
    }
}
