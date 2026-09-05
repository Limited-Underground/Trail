package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1DirectContactTest {
    @Test
    fun incomingRequestCarriesOnlyGeneratedConsentPromptUntilAccepted() {
        val pending = accept(contact().receiveRequest())
        assertEquals(V1DirectContactState.INCOMING_REQUEST_PENDING, pending.state)
        assertEquals("Alex is requesting to chat with you", pending.generatedIncomingRequestText)
        assertFalse(pending.maySendTypedMessage)
        val accepted = accept(pending.acceptRequest())
        assertTrue(accepted.maySendTypedMessage)
        assertNull(accepted.generatedIncomingRequestText)
        assertFalse(accepted.locationSharingEnabled)
    }

    @Test
    fun directLocationDefaultsOffAndCannotStartBeforeConsent() {
        val available = contact()
        reject(V1DirectContactFailure.CHAT_NOT_ACCEPTED, available.setLocationSharing(true))
        val accepted = accept(accept(available.receiveRequest()).acceptRequest())
        val sharing = accept(accepted.setLocationSharing(true))
        assertTrue(sharing.locationSharingEnabled)
        assertFalse(accept(sharing.setLocationSharing(false)).locationSharingEnabled)
    }

    @Test
    fun declineDoesNotCreateChatAndBlockSuppressesNewRequests() {
        val declined = accept(accept(contact().receiveRequest()).declineRequest())
        assertFalse(declined.maySendTypedMessage)
        val blocked = accept(declined.block())
        reject(V1DirectContactFailure.CONTACT_BLOCKED, blocked.receiveRequest())
        reject(V1DirectContactFailure.CONTACT_BLOCKED, blocked.requestChat())
        assertEquals(V1DirectContactState.AVAILABLE, accept(blocked.unblock()).state)
    }

    @Test
    fun outgoingRequestNeedsPeerAcceptanceAndCannotBeAcceptedLocally() {
        val outgoing = accept(contact().requestChat())
        assertEquals(V1DirectContactState.OUTGOING_REQUEST_PENDING, outgoing.state)
        reject(V1DirectContactFailure.INVALID_TRANSITION, outgoing.acceptRequest())
        assertTrue(accept(outgoing.markAcceptedByPeer()).maySendTypedMessage)
    }

    @Test
    fun opaqueIdentityIsCopiedValidatedAndRedacted() {
        assertNull(V1DirectPeerToken.create(ByteArray(V1DirectPeerToken.SIZE_BYTES)))
        val bytes = ByteArray(V1DirectPeerToken.SIZE_BYTES) { 7 }
        val token = assertNotNull(V1DirectPeerToken.create(bytes))
        bytes.fill(1)
        assertEquals(token, V1DirectPeerToken.create(ByteArray(V1DirectPeerToken.SIZE_BYTES) { 7 }))
        val direct = V1DirectContact.available(token, requireNotNull(V1PublicName.create("Alex")))
        assertFalse(direct.toString().contains("Alex"))
        assertEquals("V1DirectPeerToken(redacted)", token.toString())
    }

    private fun contact(): V1DirectContact {
        val token = requireNotNull(V1DirectPeerToken.create(ByteArray(V1DirectPeerToken.SIZE_BYTES) { 4 }))
        return V1DirectContact.available(token, requireNotNull(V1PublicName.create("Alex")))
    }

    private fun accept(result: V1DirectContactResult): V1DirectContact =
        assertIs<V1DirectContactResult.Accepted>(result).contact

    private fun reject(expected: V1DirectContactFailure, result: V1DirectContactResult) {
        assertEquals(expected, assertIs<V1DirectContactResult.Rejected>(result).failure)
    }
}
