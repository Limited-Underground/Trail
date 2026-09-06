package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class V1PeopleScreenStateTest {
    @Test fun defaultIsUnavailableWithLocationOff() {
        val state = V1PeopleScreenState()
        assertFalse(state.canSubmit)
        assertFalse(state.discoveryAvailable)
        assertTrue(state.publiclyVisible)
        assertFalse(state.effectiveLocationSharing)
    }

    @Test fun unacceptedStatesNeverExposeSuppliedContentOrLocation() {
        V1DirectContactState.entries.filter { it != V1DirectContactState.ACCEPTED }.forEach { contact ->
            val state = V1PeopleScreenState(contactState = contact, locationSharingEnabled = true,
                messages = listOf(V1DirectMessageUi("Hidden first message", V1DirectMessageStatus.QUEUED)))
            assertTrue(state.visibleMessages.isEmpty())
            assertFalse(state.effectiveLocationSharing)
        }
    }

    @Test fun acceptedChatShowsMessagesWithoutEnablingLocation() {
        val messages = listOf(V1DirectMessageUi("Hello", V1DirectMessageStatus.RECEIVED))
        val state = V1PeopleScreenState(contactState = V1DirectContactState.ACCEPTED, messages = messages)
        assertEquals(messages, state.visibleMessages)
        assertFalse(state.effectiveLocationSharing)
        assertTrue(state.copy(locationSharingEnabled = true).effectiveLocationSharing)
    }

    @Test fun draftsAreBoundedAndRejectControls() {
        assertFalse(V1DirectDraftPolicy.valid("  "))
        assertFalse(V1DirectDraftPolicy.valid("x".repeat(501)))
        assertFalse(V1DirectDraftPolicy.valid("Hello\u0000"))
        assertTrue(V1DirectDraftPolicy.valid("Hello\nthere"))
        assertTrue(V1DirectDraftPolicy.valid("x".repeat(500)))
    }
}
