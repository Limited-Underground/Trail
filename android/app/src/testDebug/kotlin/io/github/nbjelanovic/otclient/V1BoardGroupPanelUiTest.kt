package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertIsNotEnabled
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
class V1BoardGroupPanelUiTest {
    @get:Rule val ui = createComposeRule()

    @Test fun refreshDiscoversSupportAndCreateForwardsChosenLocationRule() {
        val state = mutableStateOf(V1BoardGroupState())
        val actions = mutableListOf<V1BoardGroupAction>()
        ui.setContent {
            MaterialTheme { V1BoardGroupPanel(state.value, connected = true, onAction = actions::add) }
        }
        ui.onNodeWithText("Create group").assertDoesNotExist()
        ui.onNodeWithText("Refresh groups").performClick()
        ui.runOnIdle {
            assertEquals(listOf(V1BoardGroupAction.Refresh), actions)
            state.value = V1BoardGroupState(supported = true, available = true, canCreate = true)
        }
        ui.onNodeWithText("Required").performScrollTo().performClick()
        ui.onNodeWithText("Create group").performScrollTo().performClick()
        ui.runOnIdle {
            assertEquals(listOf(V1BoardGroupAction.Refresh, V1BoardGroupAction.Create(2)), actions)
        }
    }

    @Test fun signedRosterAndStopTrustUseTheCurrentGroup() {
        val initial = sampleRoster(4u)
        val current = sampleRoster(5u)
        val state = mutableStateOf(readState(initial))
        val actions = mutableListOf<V1BoardGroupAction>()
        ui.setContent {
            MaterialTheme { V1BoardGroupPanel(state.value, connected = true, onAction = actions::add) }
        }
        ui.onNodeWithText("Signed roster · Version 4").assertExists()
        ui.onNodeWithText("Location sharing: Preferred").assertExists()
        ui.onNodeWithText("Admin · Administrator").assertExists()
        ui.onNodeWithText("Scout · Member").assertExists()
        ui.onNodeWithText("Stop trusting this group").performScrollTo().performClick()
        ui.onNodeWithText("Stop trusting Group 0000000000000107?").assertExists()
        ui.runOnIdle {
            assertEquals(emptyList<V1BoardGroupAction>(), actions)
            state.value = readState(current)
        }
        ui.onNodeWithText("Stop trusting").assertDoesNotExist()
        ui.onNodeWithText("Signed roster · Version 5").assertExists()
        ui.onNodeWithText("Stop trusting this group").performScrollTo().performClick()
        ui.onNodeWithText("Stop trusting").performClick()
        ui.runOnIdle { assertEquals(listOf(V1BoardGroupAction.StopTrust(current)), actions) }
    }

    @Test fun busyRosterBlocksMutationAndDistrustedGroupKeepsItsSignedReadback() {
        val state = mutableStateOf(readState(sampleRoster(4u), busy = true))
        ui.setContent {
            MaterialTheme { V1BoardGroupPanel(state.value, connected = true, onAction = { false }) }
        }
        ui.onNodeWithText("Refresh groups").assertIsNotEnabled()
        ui.onNodeWithText("Stop trusting this group").assertIsNotEnabled()
        ui.runOnIdle { state.value = readState(sampleRoster(4u, distrusted = true)) }
        ui.onNodeWithText("Trust stopped").assertExists()
        ui.onNodeWithText("Signed roster · Version 4").assertExists()
        ui.onNodeWithText("Stop trusting this group").assertDoesNotExist()
    }

    @Test fun debugInviteAndJoinUseOnlyTheSampleFlow() {
        val actions = mutableListOf<V1BoardGroupAction>()
        ui.setContent {
            MaterialTheme {
                V1BoardGroupPanel(V1BoardGroupState(), connected = false, onAction = actions::add)
            }
        }
        ui.onNodeWithText("Practice inviting and joining (debug)").performScrollTo().performClick()
        ui.onNodeWithText("Create sample invitation").performScrollTo().performClick()
        ui.onNodeWithText("Review sample invitation").performScrollTo().performClick()
        ui.onNodeWithText("Join sample group").performScrollTo().performClick()
        ui.onNodeWithText("Joined Camp group in practice mode").assertExists()
        ui.onNodeWithText("Create group").assertDoesNotExist()
        ui.runOnIdle { assertEquals(emptyList<V1BoardGroupAction>(), actions) }
    }

    private fun sampleRoster(version: UInt, distrusted: Boolean = false) =
        V1BoardGroupSummary(
            slot = 0,
            groupId = 0x107uL,
            version = version,
            locationRule = 1,
            members = listOf(
                V1BoardGroupMember("Admin", administrator = true),
                V1BoardGroupMember("Scout", administrator = false),
            ),
            distrusted = distrusted,
        )

    private fun readState(roster: V1BoardGroupSummary, busy: Boolean = false) =
        V1BoardGroupState(supported = true, available = true, busy = busy, groups = listOf(roster))
}
