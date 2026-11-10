package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import org.junit.*
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Composed host observations only; injected display hints are not authentication evidence. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[35],qualifiers="w411dp-h891dp")
class V1QuickStatusUiTest {
    @get:Rule val ui=createComposeRule()
    private val prefs get()=RuntimeEnvironment.getApplication().getSharedPreferences("v1-local-templates",0)
    @Before fun isolatedPreferences() {
        val data=java.io.File(requireNotNull(RuntimeEnvironment.getApplication().applicationInfo.dataDir)).canonicalFile.toPath()
        check(data.startsWith(java.io.File(requireNotNull(System.getProperty("java.io.tmpdir"))).canonicalFile.toPath()))
        check(prefs.edit().clear().commit())
    }
    @Test fun absentAuthorityAndFreeTextAreDisabledWithVisibleReasons() {
        check(prefs.edit().putString("template-0","Free text fixture").commit())
        ui.setContent { MaterialTheme { V1MessagesScreen("No device connected",{}) } }
        ui.onNodeWithText("Send: I'm OK").performScrollTo().assertIsNotEnabled()
        ui.onNodeWithText("Send: Free text fixture").performScrollTo().assertIsNotEnabled()
        ui.onAllNodesWithText(V1QuickStatusComposer.TEXT_UNAVAILABLE).assertCountEquals(4)
        ui.onNodeWithText(V1QuickStatusComposer.AUTHORITY_UNAVAILABLE).assertExists()
        assertEquals("Free text fixture",prefs.getString("template-0",null))
    }
    @Test fun mappedSavedTemplateSubmitsOnlyItsExactStatusAndShowsHonestRefusal() {
        check(prefs.edit().putString("template-0","Need assistance").commit())
        val recipient=V1QuickStatusRecipient();val calls=mutableListOf<Pair<V1QuickStatusRecipient,CompanionQuickStatus>>()
        ui.setContent { MaterialTheme { V1MessagesScreen("Device connected",{},quickStatus=V1QuickStatusState(recipient),
            onSubmitQuickStatus={r,s->calls.add(r to s);false}) } }
        ui.onAllNodesWithText("Send: Need assistance")[1].performScrollTo().performClick()
        ui.runOnIdle { assertEquals(listOf(recipient to CompanionQuickStatus.NEED_ASSISTANCE),calls) }
        ui.onNodeWithText("Request refused or outcome uncertain. Check device evidence; do not resend automatically.").assertExists()
        assertEquals("Need assistance",prefs.getString("template-0",null))
    }
    @Test fun receivedAndDeliveryEvidenceStayCurrentAndQueueIsNeverAck() {
        val recipient=V1QuickStatusRecipient()
        val state=mutableStateOf(V1QuickStatusState(recipient,CompanionQuickStatus.OK,BlePacketDeliveryPhase.QUEUE_ACCEPTED))
        ui.setContent { MaterialTheme { V1MessagesScreen("Device connected",{},quickStatus=state.value) } }
        ui.onNodeWithText("Received status: I'm OK").assertExists()
        ui.onNodeWithText("Queued on device; delivery not confirmed").assertExists()
        ui.onAllNodesWithText(V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.PROTECTED_PEER_ACK)).assertCountEquals(0)
        ui.runOnIdle { state.value=V1QuickStatusState(null,CompanionQuickStatus.OK,BlePacketDeliveryPhase.PROTECTED_PEER_ACK) }
        ui.onAllNodesWithText("Received status: I'm OK").assertCountEquals(0)
        ui.onAllNodesWithText(V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.PROTECTED_PEER_ACK)).assertCountEquals(0)
    }
}
