package io.github.nbjelanovic.otclient

import kotlin.test.*

class V1PendingDirectChatTest {
    @Test fun outboundRequestExcludesTypedContentAndAllStringFormsRedact() {
        val pending = pending()
        assertEquals(V1DirectContactState.OUTGOING_REQUEST_PENDING, pending.state)
        assertTrue(pending.hasLocalFirstMessage)
        val request = assertNotNull(pending.outboundRequest)
        assertEquals(peer(), request.peerToken)
        assertEquals(token(), request.requestToken)
        assertFalse(request.toString().contains("Meet at the trailhead"))
        assertFalse(pending.toString().contains("Meet at the trailhead"))
        assertFalse(pending.locationSharingEnabled)
    }

    @Test fun exactAcceptanceReleasesOneQueueCandidateAndNoDeliveryClaim() {
        val result = assertIs<V1PendingDirectChatResult.Applied>(pending().receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED)))
        val item = assertNotNull(result.messageToQueue)
        assertEquals("Meet at the trailhead", item.text)
        assertEquals(peer(), item.peerToken)
        assertEquals(token(), item.requestToken)
        assertFalse(item.toString().contains("trailhead"))
        assertEquals(V1DirectContactState.ACCEPTED, result.next.state)
        assertNull(result.next.outboundRequest)
        assertFalse(result.next.hasLocalFirstMessage)
        assertFalse(result.next.locationSharingEnabled)
        assertIs<V1PendingDirectChatResult.Rejected>(result.next.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED)))
    }

    @Test fun wrongPeerOrRequestNeverReleasesContent() {
        val pending = pending()
        val wrongPeer = V1ChatDecisionReceipt(peer(2), token(), V1ChatRequestDecision.ACCEPTED)
        val wrongRequest = V1ChatDecisionReceipt(peer(), token(2), V1ChatRequestDecision.ACCEPTED)
        for (receipt in listOf(wrongPeer, wrongRequest)) {
            assertEquals(V1PendingDirectChatFailure.RECEIPT_MISMATCH,
                assertIs<V1PendingDirectChatResult.Rejected>(pending.receiveDecision(receipt)).failure)
        }
        assertTrue(pending.hasLocalFirstMessage)
        assertFalse(pending.locationSharingEnabled)
    }

    @Test fun declineBlockAndLocalCancelDiscardDraftAndRejectLateAcceptance() {
        val pending = pending()
        val declined = assertIs<V1PendingDirectChatResult.Applied>(pending.receiveDecision(receipt(V1ChatRequestDecision.DECLINED)))
        val blocked = assertIs<V1PendingDirectChatResult.Applied>(pending.receiveDecision(receipt(V1ChatRequestDecision.BLOCKED)))
        assertNull(declined.messageToQueue)
        assertNull(blocked.messageToQueue)
        assertEquals(V1DirectContactState.BLOCKED, blocked.next.cancel().state)
        for (terminal in listOf(declined.next, blocked.next, pending.cancel(), pending.block())) {
            assertFalse(terminal.hasLocalFirstMessage)
            assertNull(terminal.outboundRequest)
            assertFalse(terminal.locationSharingEnabled)
            assertIs<V1PendingDirectChatResult.Rejected>(terminal.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED)))
        }
    }

    @Test fun invalidContentAndIneligibleContactsCannotStartRequests() {
        for (text in listOf("", "  ", "x".repeat(501), "bad\ttext", "bad\rtext", "bad\u0000", "bad\u0085", "\uD800")) {
            assertNull(V1PendingDirectChat.begin(contact(), token(), text))
        }
        assertNotNull(V1PendingDirectChat.begin(contact(), token(), "x".repeat(500)))
        assertNotNull(V1PendingDirectChat.begin(contact(), token(), "First line\nSecond line"))
        val blocked = assertIs<V1DirectContactResult.Accepted>(contact().block()).contact
        val requested = assertIs<V1DirectContactResult.Accepted>(contact().requestChat()).contact
        assertNull(V1PendingDirectChat.begin(blocked, token(), "Hello"))
        assertNull(V1PendingDirectChat.begin(requested, token(), "Hello"))
    }

    @Test fun requestTokenIsBoundedCopiedAndRedacted() {
        assertNull(V1ChatRequestToken.create(ByteArray(16)))
        assertNull(V1ChatRequestToken.create(ByteArray(15) { 1 }))
        val bytes = ByteArray(16) { 1 }
        val request = assertNotNull(V1ChatRequestToken.create(bytes))
        bytes.fill(2)
        assertEquals(token(), request)
        assertEquals("V1ChatRequestToken(redacted)", request.toString())
    }

    @Test fun receiptCannotBeReusedEvenAgainstAnOldPendingSnapshot() {
        val pending = pending()
        val receipt = receipt(V1ChatRequestDecision.ACCEPTED)
        assertIs<V1PendingDirectChatResult.Applied>(pending.receiveDecision(receipt))
        assertEquals(V1PendingDirectChatFailure.DECISION_ALREADY_USED,
            assertIs<V1PendingDirectChatResult.Rejected>(pending.receiveDecision(receipt)).failure)
    }

    @Test fun bothDirectChoicesAreIndependentAndWaitForFreshConversationReadback() {
        for (share in listOf(false, true)) {
            for (shareBack in listOf(false, true)) {
                val outgoingInvite = V1DirectLocationInvite(peer(), token())
                val incomingInvite = V1DirectLocationInvite(peer(), token())
                val outgoing = pending(share, outgoingInvite)
                val incoming = assertNotNull(V1PendingDirectChat.receive(contact(), incomingInvite))
                assertFalse(outgoing.takeLocationConsent(V1DirectConversationReady(outgoingInvite)))
                assertFalse(incoming.takeLocationConsent(V1DirectConversationReady(incomingInvite)))
                assertIs<V1PendingDirectChatResult.Rejected>(
                    incoming.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED, incomingInvite)),
                )

                val outgoingAccepted = assertIs<V1PendingDirectChatResult.Applied>(
                    outgoing.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED, outgoingInvite)),
                )
                val incomingAccepted = assertIs<V1PendingDirectChatResult.Applied>(
                    assertNotNull(incoming.acceptIncoming(shareBack))
                        .receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED, incomingInvite)),
                )
                assertNotNull(outgoingAccepted.messageToQueue)
                assertNull(incomingAccepted.messageToQueue)
                assertFalse(outgoingAccepted.next.locationSharingEnabled)
                assertFalse(incomingAccepted.next.locationSharingEnabled)
                assertFalse(outgoingAccepted.next.takeLocationConsent(V1DirectConversationReady(incomingInvite)))
                assertEquals(share, outgoingAccepted.next.takeLocationConsent(V1DirectConversationReady(outgoingInvite)))
                assertEquals(shareBack, incomingAccepted.next.takeLocationConsent(V1DirectConversationReady(incomingInvite)))
                assertFalse(outgoingAccepted.next.takeLocationConsent(V1DirectConversationReady(outgoingInvite)))
                assertFalse(incomingAccepted.next.takeLocationConsent(V1DirectConversationReady(incomingInvite)))
            }
        }
    }

    @Test fun retainedAcceptanceAndAnotherAttachmentCannotRestoreOrReleaseIntent() {
        val invite = V1DirectLocationInvite(peer(), token())
        val otherAttachment = V1DirectLocationInvite(peer(), token())
        val pending = pending(true, invite)
        for (different in listOf(otherAttachment, null)) {
            val accepted = assertIs<V1PendingDirectChatResult.Applied>(
                pending.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED, different)),
            )
            assertNotNull(accepted.messageToQueue)
            assertFalse(accepted.next.takeLocationConsent(V1DirectConversationReady(invite)))
        }
        val accepted = assertIs<V1PendingDirectChatResult.Applied>(
            pending.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED, invite)),
        )
        assertFalse(accepted.next.takeLocationConsent(V1DirectConversationReady(invite)))
    }

    @Test fun cancelBlockDeclineAndContextLossClearChoicesEvenFromOlderSnapshots() {
        for (end in listOf<(V1PendingDirectChat) -> Unit>(
            { it.cancel() }, { it.block() }, { it.clearLocationConsent() }, { pending ->
                pending.receiveDecision(receipt(V1ChatRequestDecision.DECLINED))
            },
        )) {
            val invite = V1DirectLocationInvite(peer(), token())
            val old = pending(true, invite)
            end(old)
            val late = assertIs<V1PendingDirectChatResult.Applied>(
                old.receiveDecision(receipt(V1ChatRequestDecision.ACCEPTED, invite)),
            )
            assertFalse(late.next.takeLocationConsent(V1DirectConversationReady(invite)))
        }
    }

    private fun peer(value: Byte = 1) = requireNotNull(V1DirectPeerToken.create(ByteArray(16) { value }))
    private fun token(value: Byte = 1) = requireNotNull(V1ChatRequestToken.create(ByteArray(16) { value }))
    private fun contact() = V1DirectContact.available(peer(), requireNotNull(V1PublicName.create("Alex")))
    private fun pending(share: Boolean = false, invite: V1DirectLocationInvite? = null) =
        requireNotNull(V1PendingDirectChat.begin(contact(), token(), "Meet at the trailhead", share, invite))
    private fun receipt(decision: V1ChatRequestDecision, invite: V1DirectLocationInvite? = null) =
        V1ChatDecisionReceipt(peer(), token(), decision, invite)
}
