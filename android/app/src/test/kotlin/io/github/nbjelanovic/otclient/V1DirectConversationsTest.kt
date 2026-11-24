package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionDirectBinding
import io.github.nbjelanovic.otprotocol.CompanionDirectCommand
import io.github.nbjelanovic.otprotocol.CompanionDirectDelivery
import io.github.nbjelanovic.otprotocol.CompanionDirectOperation
import io.github.nbjelanovic.otprotocol.CompanionDirectResult
import io.github.nbjelanovic.otprotocol.CompanionDirectRole
import io.github.nbjelanovic.otprotocol.CompanionDirectState
import io.github.nbjelanovic.otprotocol.CompanionDirectStatus
import io.github.nbjelanovic.otprotocol.CompanionDirectText
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1DirectConversationsTest {
    private fun binding(seed: Int) = CompanionDirectBinding(ByteArray(32) { (seed + it).toByte() }, ByteArray(32) { (seed + 0x40 + it).toByte() })
    private val alice = binding(1)
    private val bob = binding(9)
    private fun plain(op: CompanionDirectOperation, status: CompanionDirectStatus = CompanionDirectStatus.OK) =
        CompanionDirectResult.Plain(op, status)
    private fun read(slot: Int, state: CompanionDirectState, b: CompanionDirectBinding?, generation: ULong = 0uL) =
        CompanionDirectResult.Conversation(slot, state, generation, b)

    private fun connect(model: V1DirectConversations, b: CompanionDirectBinding = alice) {
        assertNotNull(model.start(CompanionDirectRole.START, 123456))
        model.onResult(plain(CompanionDirectOperation.BEGIN))
        model.onResult(plain(CompanionDirectOperation.REVIEW, CompanionDirectStatus.BUSY))
        assertTrue(model.invite is V1DirectInvite.Waiting)
        model.onResult(CompanionDirectResult.Reviewed(b))
        assertEquals(CompanionDirectCommand.Accept(b), model.accept())
        assertEquals(CompanionDirectCommand.Finish(b), model.onResult(plain(CompanionDirectOperation.ACCEPT)))
    }

    @Test
    fun inviteAcceptFinishThenComposerOpensOnlyWhenReady() {
        val model = V1DirectConversations()
        connect(model)
        assertTrue(model.invite is V1DirectInvite.Finishing)
        model.onResult(plain(CompanionDirectOperation.FINISH, CompanionDirectStatus.BUSY))
        assertTrue(model.invite is V1DirectInvite.Finishing, "busy keeps Finishing connection")
        model.onResult(plain(CompanionDirectOperation.FINISH))
        assertEquals(V1DirectInvite.Ended(V1DirectOutcome.CONNECTED), model.invite)

        model.keepDraft(alice, "hello")
        model.onResult(read(0, CompanionDirectState.ACCEPTED, alice))
        assertFalse(model.conversation(alice)!!.composerEnabled)
        assertNull(model.draft(alice), "draft held until Ready")
        assertNull(model.send(alice, "hello"))

        model.onResult(read(0, CompanionDirectState.READY, alice, 5uL))
        assertEquals("hello", model.draft(alice))
        val send = model.send(alice, "hello") as CompanionDirectCommand.Send
        assertEquals(0, send.text.slot)
        assertEquals(5uL, send.text.generation)
        assertNull(model.draft(alice), "sent draft is released")
    }

    @Test
    fun checkCodeIsShownForReview() {
        val model = V1DirectConversations()
        model.start(CompanionDirectRole.JOIN, 42)
        model.onResult(CompanionDirectResult.Reviewed(alice))
        assertEquals("0102 0304 0506", (model.invite as V1DirectInvite.Review).checkCode)
    }

    @Test
    fun declineBlockedFullAndExpiredEndTheInvite() {
        val model = V1DirectConversations()
        model.start(CompanionDirectRole.JOIN, 42)
        model.onResult(CompanionDirectResult.Reviewed(alice))
        assertEquals(CompanionDirectCommand.Decline, model.decline())
        model.onResult(plain(CompanionDirectOperation.DECLINE))
        assertEquals(V1DirectInvite.Ended(V1DirectOutcome.DECLINED), model.invite)

        for ((status, outcome) in listOf(
            CompanionDirectStatus.BLOCKED to V1DirectOutcome.BLOCKED,
            CompanionDirectStatus.FULL to V1DirectOutcome.FULL,
            CompanionDirectStatus.EXPIRED to V1DirectOutcome.EXPIRED,
        )) {
            model.clearOutcome()
            model.start(CompanionDirectRole.START, 7)
            model.onResult(CompanionDirectResult.Reviewed(bob))
            model.accept()
            assertNull(model.onResult(plain(CompanionDirectOperation.ACCEPT, status)))
            assertEquals(V1DirectInvite.Ended(outcome), model.invite)
            assertNull(model.conversation(bob), "no conversation without consent")
        }
    }

    @Test
    fun noSecondInviteWhileOneIsOpenAndBadCodesAreRefused() {
        val model = V1DirectConversations()
        assertNull(model.start(CompanionDirectRole.START, 0))
        assertNull(model.start(CompanionDirectRole.START, 1_000_000))
        assertNotNull(model.start(CompanionDirectRole.START, 1))
        assertNull(model.start(CompanionDirectRole.START, 2))
        assertNull(model.accept(), "accept needs a reviewed peer")
    }

    @Test
    fun aDifferentPeerInTheSlotDropsDraftsAndHistory() {
        val model = V1DirectConversations()
        model.onResult(read(2, CompanionDirectState.READY, alice, 3uL))
        model.keepDraft(alice, "draft")
        model.send(alice, "sent")
        model.keepDraft(alice, "draft")
        model.onResult(read(2, CompanionDirectState.WAITING_FOR_PEER, bob))
        assertNull(model.conversation(alice))
        model.onResult(read(2, CompanionDirectState.READY, alice, 4uL))
        assertNull(model.draft(alice))
        assertTrue(model.messages(alice).isEmpty())
    }

    @Test
    fun endedSlotClearsAndBlockOrEndNeedTheExactBinding() {
        val model = V1DirectConversations()
        model.onResult(read(1, CompanionDirectState.READY, alice, 3uL))
        assertEquals(CompanionDirectCommand.End(alice), model.end(alice))
        assertNull(model.end(bob))
        assertEquals(CompanionDirectCommand.Block(alice), model.block(alice))
        model.onResult(read(1, CompanionDirectState.EMPTY, null))
        assertTrue(model.conversations.isEmpty())
        assertNull(model.send(alice, "late"))
    }

    @Test
    fun receivedAndDeliveryOnlyCountForTheCurrentGeneration() {
        val model = V1DirectConversations()
        model.onResult(read(0, CompanionDirectState.READY, alice, 8uL))
        val sent = model.send(alice, "out") as CompanionDirectCommand.Send
        model.onResult(CompanionDirectResult.Received(CompanionDirectText(0, 7uL, 1u, "old session")))
        model.onResult(CompanionDirectResult.Received(CompanionDirectText(0, 8uL, 2u, "in")))
        model.onResult(CompanionDirectResult.Delivery(0, 7uL, sent.text.messageId, CompanionDirectDelivery.FAILED))
        model.onResult(CompanionDirectResult.Delivery(0, 8uL, sent.text.messageId, CompanionDirectDelivery.DELIVERED))
        val list = model.messages(alice)
        assertEquals(listOf("out", "in"), list.map { it.text })
        assertEquals(CompanionDirectDelivery.DELIVERED, list[0].delivery)
    }

    @Test
    fun disconnectForgetsBoardStateAndOpenInvites() {
        val model = V1DirectConversations()
        model.onResult(read(0, CompanionDirectState.READY, alice, 8uL))
        model.start(CompanionDirectRole.START, 5)
        model.disconnected()
        assertTrue(model.conversations.isEmpty())
        assertEquals(V1DirectInvite.None, model.invite)
        assertNull(model.send(alice, "x"))
    }

    @Test
    fun oversizedTextIsNotQueued() {
        val model = V1DirectConversations()
        model.onResult(read(0, CompanionDirectState.READY, alice, 8uL))
        assertNull(model.send(alice, "x".repeat(91)))
        assertTrue(model.messages(alice).isEmpty())
    }
}
