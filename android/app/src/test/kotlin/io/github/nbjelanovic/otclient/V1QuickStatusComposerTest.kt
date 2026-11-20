package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import kotlin.test.*

class V1QuickStatusComposerTest {
    @Test fun allFourWireMeaningsRoundTripWithoutInferringSynonyms() {
        assertEquals(listOf("I'm OK", "Need assistance", "Anyone online?", "Available to help"),
            CompanionQuickStatus.entries.map(V1QuickStatusComposer::label))
        for (status in CompanionQuickStatus.entries)
            assertEquals(status, V1QuickStatusComposer.fromTemplate(V1QuickStatusComposer.label(status)))
        for (text in listOf("Stopping here", "On my way", "Please check in", "i'm ok", "I'm OK ", "I'm OK\n"))
            assertNull(V1QuickStatusComposer.fromTemplate(text))
    }
    @Test fun unsupportedTextAtStorageBoundariesIsNeverTruncatedOrMapped() {
        for (size in listOf(0, 159, 160, 161)) assertNull(V1QuickStatusComposer.fromTemplate("x".repeat(size)))
        val before = listOf("I'm OK", "On my way")
        val edited = assertNotNull(V1QuickMessages.edit(before, 1, "On my way", "Need assistance"))
        assertEquals(listOf("I'm OK", "Need assistance"), edited)
        assertEquals(CompanionQuickStatus.NEED_ASSISTANCE, V1QuickStatusComposer.fromTemplate(edited[1]))
        assertEquals(listOf("Need assistance", "I'm OK"), V1QuickMessages.move(edited, 1, -1))
        assertEquals(listOf("I'm OK", "On my way"), before)
    }
    @Test fun onlyPeerAckHasPeerReceiptLanguageAndNeverHumanReadClaims() {
        assertEquals("Your device is waiting to send it.",
            V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.QUEUE_ACCEPTED))
        assertEquals("Your device sent it and hasn't confirmed delivery.",
            V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.TRANSMISSION_COMPLETED))
        assertEquals("The other device received it.",
            V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.PROTECTED_PEER_ACK))
        assertEquals("Delivery is uncertain.\nCheck before sending again.",
            V1QuickStatusComposer.evidence(BlePacketDeliveryPhase.OUTCOME_UNKNOWN))
        assertEquals(listOf(BlePacketDeliveryPhase.PROTECTED_PEER_ACK),
            BlePacketDeliveryPhase.entries.filter { "received" in V1QuickStatusComposer.evidence(it) })
        assertNull(V1QuickStatusState().recipient)
        assertEquals("V1QuickStatusRecipient(redacted)", V1QuickStatusRecipient().toString())
    }
}
