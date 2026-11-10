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
    @Test fun absentAuthorityKeepsFixedAndTypedSendsDisabledWithVisibleReasons() {
        check(prefs.edit().putString("template-0","Free text fixture").commit())
        ui.setContent { MaterialTheme { V1MessagesScreen("No device connected",{}) } }
        ui.onNodeWithText("Send: I'm OK").performScrollTo().assertIsNotEnabled()
        ui.onNodeWithText("Send: Free text fixture").performScrollTo().assertIsNotEnabled()
        ui.onAllNodesWithText(V1QuickStatusComposer.TEXT_UNAVAILABLE).assertCountEquals(0)
        ui.onNodeWithText(V1TextComposer.AUTHORITY_UNAVAILABLE).assertExists()
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

    @Test fun composedTextAndFreeTemplateUseTheRealOtyOwnerGraphAndDeliveryEvidence() {
        check(prefs.edit().putString("template-0","Free text fixture").commit())
        val state=mutableStateOf(V1TextState())
        lateinit var h: TextCarrierHostFixture
        ui.setContent { MaterialTheme { V1MessagesScreen("Device connected",{},textMessages=state.value,
            onSubmitText={r,t->h.owner.submitText(r,t)}) } }
        ui.runOnIdle {
            h=BleCompanionRuntimeTest().textCarrierHostFixture()
            checkNotNull(h.owner.observe { state.value=it.textMessages })
        }
        ui.onNodeWithText("Write a message").performScrollTo().performTextInput("Café 界")
        ui.onNodeWithText("Send text").performScrollTo().performClick()
        ui.runOnIdle { assertEquals("Café 界",h.command().text) }
        ui.onNodeWithText("Text delivery: Awaiting device acceptance").assertExists()
        ui.runOnIdle {
            val r=h.command()
            h.emit(io.github.nbjelanovic.otprotocol.CompanionTextResult(r.reference,r.exchange,r.operation,1,0,100uL,4097u,1uL))
        }
        ui.onNodeWithText("Text delivery: Queued on device; delivery not confirmed").assertExists()
        ui.runOnIdle {
            val r=h.command()
            h.emit(io.github.nbjelanovic.otprotocol.CompanionTextDelivery(r.reference,1u,r.exchange,r.operation,100uL,4097u,1uL,2,0))
        }
        ui.onNodeWithText("Text delivery: Transmission complete; peer receipt not confirmed").assertExists()
        ui.runOnIdle {
            val r=h.command()
            h.emit(io.github.nbjelanovic.otprotocol.CompanionTextDelivery(r.reference,2u,r.exchange,r.operation,100uL,4097u,1uL,3,0))
            h.emit(io.github.nbjelanovic.otprotocol.CompanionTextReceived(r.reference,3u,4098u,2uL,"real host event"))
        }
        ui.onNodeWithText("Text delivery: " + V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.PROTECTED_PEER_ACK)).assertExists()
        ui.onNodeWithText("Received text: real host event").assertExists()
        ui.onNodeWithText("Send: Free text fixture").performScrollTo().performClick()
        ui.runOnIdle {
            assertEquals("Free text fixture",h.command().text)
            assertEquals(2uL,h.command().operation)
            assertEquals("Free text fixture",prefs.getString("template-0",null))
            h.close()
        }
    }
    @Test fun oversizedTemplateAndDraftHaveVisibleRefusalWithoutSubmission() {
        val tooLong="界".repeat(31);check(prefs.edit().putString("template-0",tooLong).commit())
        val recipient=V1QuickStatusRecipient();var calls=0
        ui.setContent { MaterialTheme { V1MessagesScreen("Device connected",{},textMessages=V1TextState(recipient),
            onSubmitText={_,_->calls++;true}) } }
        ui.onNodeWithText("Send: $tooLong").performScrollTo().assertIsNotEnabled()
        ui.onNodeWithText("Write a message").performScrollTo().performTextInput("界".repeat(30)+"x")
        ui.onNodeWithText("Send text").performScrollTo().assertIsNotEnabled()
        ui.onAllNodesWithText("Message exceeds 90 UTF-8 bytes; it was not truncated.").assertCountEquals(2)
        ui.runOnIdle { assertEquals(0,calls);assertEquals(tooLong,prefs.getString("template-0",null)) }
    }
}
