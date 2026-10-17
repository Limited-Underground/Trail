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

    private fun peer(value: Byte = 1) = requireNotNull(V1DirectPeerToken.create(ByteArray(16) { value }))
    private fun token(value: Byte = 1) = requireNotNull(V1ChatRequestToken.create(ByteArray(16) { value }))
    private fun contact() = V1DirectContact.available(peer(), requireNotNull(V1PublicName.create("Alex")))
    private fun pending() = requireNotNull(V1PendingDirectChat.begin(contact(), token(), "Meet at the trailhead"))
    private fun receipt(decision: V1ChatRequestDecision) = V1ChatDecisionReceipt(peer(), token(), decision)
}
