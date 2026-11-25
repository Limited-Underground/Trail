package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import io.github.nbjelanovic.otprotocol.CompanionGroupJoinStep
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextClearance
import androidx.compose.ui.test.performTextInput
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

    @Test fun inviteIsLiveAndShowsTheBoardIssuedCode() {
        val group = sampleRoster(4u)
        val state = mutableStateOf(readState(group))
        val actions = mutableListOf<V1BoardGroupAction>()
        ui.setContent {
            MaterialTheme { V1BoardGroupPanel(state.value, connected = true, onAction = actions::add) }
        }
        ui.onNodeWithText("Practice inviting and joining (debug)").assertDoesNotExist()
        ui.onNodeWithText("Invite someone").performScrollTo().performClick()
        ui.runOnIdle {
            assertEquals(listOf<V1BoardGroupAction>(V1BoardGroupAction.Invite(group, "Group 0000000000000107")), actions)
            state.value = readState(group).copy(
                invite = V1BoardGroupInvite(0x107uL, "Group 0000000000000107", "MESHTRAIL-INVITE-1:AAAA", 0L),
            )
        }
        ui.onNodeWithText("Invitation to Group 0000000000000107").assertExists()
        ui.onNodeWithText("MESHTRAIL-INVITE-1:AAAA").assertExists()
        // Not connected: no invite card and no Invite button.
        ui.runOnIdle { state.value = state.value.copy(available = false) }
        ui.onNodeWithText("Invitation to Group 0000000000000107").assertDoesNotExist()
    }

    @Test fun joinReviewsThePastedInviteBeforeTheBoardSeesIt() {
        val invite = sampleInvite()
        val actions = mutableListOf<V1BoardGroupAction>()
        ui.setContent {
            MaterialTheme {
                V1BoardGroupPanel(
                    V1BoardGroupState(supported = true, available = true, canCreate = true),
                    connected = true,
                    onAction = actions::add,
                )
            }
        }
        ui.onNodeWithText("Invite code").performScrollTo().performTextInput("not a code")
        ui.onNodeWithText("Review invitation").performScrollTo().performClick()
        ui.onNodeWithText("That isn't a MeshTrail invite code.").assertExists()
        ui.onNodeWithText("Invite code").performTextClearance()
        ui.onNodeWithText("Invite code").performTextInput(V1InviteCodeText.encode(invite))
        ui.onNodeWithText("Review invitation").performScrollTo().performClick()
        ui.onNodeWithText("Join Crew?").assertExists()
        ui.runOnIdle { assertEquals(emptyList<V1BoardGroupAction>(), actions) }
        ui.onNodeWithText("Join").performClick()
        // The Join goes first, exactly once. After it the panel follows the
        // join with read-only Join Progress questions only (F8 step 2e).
        ui.runOnIdle {
            assertEquals(V1BoardGroupAction.Join(invite), actions.first())
            assertEquals(1, actions.count { it is V1BoardGroupAction.Join })
            assertEquals(true, actions.drop(1).all { it == V1BoardGroupAction.JoinProgress })
        }
    }

    @Test fun joinProgressShowsPlainInstructionsAndNeverTheValues() {
        val state = mutableStateOf(V1BoardGroupState(supported = true, available = true, canCreate = true))
        val actions = mutableListOf<V1BoardGroupAction>()
        ui.setContent {
            MaterialTheme { V1BoardGroupPanel(state.value, connected = true, onAction = actions::add) }
        }
        ui.onNodeWithText("Joining a group").assertDoesNotExist()
        ui.runOnIdle { assertEquals(emptyList<V1BoardGroupAction>(), actions) }  // nothing to follow
        ui.runOnIdle {
            state.value = state.value.copy(
                joinProgress = V1BoardGroupJoinProgress(CompanionGroupJoinStep.REVIEW_CODE, admin = false, groupId = 7uL),
            )
        }
        ui.onNodeWithText("Joining a group").assertExists()
        ui.onNodeWithText(joinProgressText(V1BoardGroupJoinProgress(CompanionGroupJoinStep.REVIEW_CODE, false, 7uL)))
            .assertExists()
        ui.runOnIdle { assertEquals(V1BoardGroupAction.JoinProgress, actions.firstOrNull()) }
        ui.runOnIdle {
            state.value = state.value.copy(
                joinProgress = V1BoardGroupJoinProgress(CompanionGroupJoinStep.JOINED, admin = true, groupId = 7uL),
            )
        }
        ui.onNodeWithText("Adding a member").assertExists()
        ui.onNodeWithText("The new member was added to the group.").assertExists()
    }

    private fun sampleInvite(): ByteArray = (
        "4f54475101010200" + "23".repeat(32) + "f102030405060708" + "00000001" + "86".repeat(32) +
            "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf" + "0101" + "04" + "43726577" + "5c".repeat(64)
        ).chunked(2).map { it.toInt(16).toByte() }.toByteArray()

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
