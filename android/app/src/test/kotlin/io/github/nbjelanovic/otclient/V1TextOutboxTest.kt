package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNotSame
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue

class V1TextOutboxTest {
    @Test
    fun savedTextWaitsForItsRecipientAfterProcessRestart() {
        val storage = MemoryStorage()
        val first = V1TextOutbox(storage) { 0L }
        assertTrue(first.bindRecipient(RECIPIENT))
        val recipient = assertNotNull(first.recipient)
        assertTrue(first.enqueue(recipient, "See you at camp"))
        assertEquals(V1TextOutboxPhase.WAITING, first.entries.single().state)

        val restored = V1TextOutbox(storage) { 0L }
        assertEquals("See you at camp", restored.entries.single().text)
        val recoveredRecipient = assertNotNull(restored.recipient)
        assertTrue(restored.bindRecipient(RECIPIENT))
        assertSame(recoveredRecipient, restored.recipient)
        assertFalse(restored.enqueue(recipient, "Old process token"))
        val attempt = assertNotNull(restored.beginAttempt())
        assertEquals(1, attempt.attempts)
        assertEquals(V1TextOutboxPhase.IN_FLIGHT, storage.snapshot?.entries?.single()?.state)
        restored.finishAttempt(attempt.id, V1TextOutboxResult.SENT)
        assertEquals(V1TextOutboxPhase.SENT, restored.entries.single().state)
        assertEquals(restored.entries, storage.snapshot?.entries)
        assertNull(restored.beginAttempt())
    }

    @Test
    fun explicitRetryableRefusalsWaitFiveThenFifteenSecondsAndStopAtThreeAttempts() {
        var now = 10_000L
        val outbox = V1TextOutbox(MemoryStorage()) { now }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        assertTrue(outbox.enqueue(assertNotNull(outbox.recipient), "One message"))

        val first = assertNotNull(outbox.beginAttempt())
        assertEquals(1, first.attempts)
        assertNull(outbox.beginAttempt())
        outbox.finishAttempt(first.id, V1TextOutboxResult.RETRYABLE)
        assertEquals(V1TextOutboxPhase.WAITING, outbox.entries.single().state)
        assertEquals(5_000L, outbox.nextDelayMillis())
        now += 4_999L
        assertNull(outbox.beginAttempt())
        now += 1L

        val second = assertNotNull(outbox.beginAttempt())
        assertEquals(first.id, second.id)
        assertEquals(2, second.attempts)
        outbox.finishAttempt(second.id, V1TextOutboxResult.RETRYABLE)
        assertEquals(15_000L, outbox.nextDelayMillis())
        now += 14_999L
        assertNull(outbox.beginAttempt())
        now += 1L

        val third = assertNotNull(outbox.beginAttempt())
        assertEquals(first.id, third.id)
        assertEquals(3, third.attempts)
        outbox.finishAttempt(third.id, V1TextOutboxResult.RETRYABLE)
        assertEquals(V1TextOutboxPhase.FAILED, outbox.entries.single().state)
        assertNull(outbox.nextDelayMillis())
        assertNull(outbox.beginAttempt())
    }

    @Test
    fun restartPreservesPriorRefusalsAndRestartsTheirBackoff() {
        val storage = MemoryStorage()
        var now = 0L
        val outbox = V1TextOutbox(storage) { now }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        assertTrue(outbox.enqueue(assertNotNull(outbox.recipient), "Still waiting"))
        val first = assertNotNull(outbox.beginAttempt())
        outbox.finishAttempt(first.id, V1TextOutboxResult.RETRYABLE)

        now += 1_000L
        val restored = V1TextOutbox(storage) { now }
        assertTrue(restored.bindRecipient(RECIPIENT))
        assertEquals(5_000L, restored.nextDelayMillis())
        assertNull(restored.beginAttempt())
        now += 5_000L
        val retried = assertNotNull(restored.beginAttempt())
        assertEquals(first.id, retried.id)
        assertEquals(2, retried.attempts)
    }

    @Test
    fun rejectsTextOverTheUtf8LimitWithoutTruncation() {
        val storage = MemoryStorage()
        val outbox = V1TextOutbox(storage) { 0L }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        val recipient = assertNotNull(outbox.recipient)
        assertFalse(outbox.enqueue(recipient, ""))
        assertFalse(outbox.enqueue(recipient, "é".repeat(46)))
        assertTrue(outbox.entries.isEmpty())
        val atLimit = "é".repeat(45)
        assertTrue(outbox.enqueue(recipient, atLimit))
        assertEquals(atLimit, assertNotNull(storage.snapshot).entries.single().text)
    }

    @Test
    fun processLossAfterAttemptStartsLeavesUnconfirmedAndDoesNotResend() {
        val storage = MemoryStorage()
        val first = V1TextOutbox(storage) { 0L }
        assertTrue(first.bindRecipient(RECIPIENT))
        assertTrue(first.enqueue(assertNotNull(first.recipient), "First"))
        assertTrue(first.enqueue(assertNotNull(first.recipient), "Second"))
        val interrupted = assertNotNull(first.beginAttempt())

        val restored = V1TextOutbox(storage) { 0L }
        assertTrue(restored.bindRecipient(RECIPIENT))
        assertEquals(V1TextOutboxPhase.UNCONFIRMED, restored.entries.first().state)
        assertEquals(V1TextOutboxPhase.UNCONFIRMED, storage.snapshot?.entries?.first()?.state)
        val next = assertNotNull(restored.beginAttempt())
        assertEquals("Second", next.text)
        restored.finishAttempt(interrupted.id, V1TextOutboxResult.SENT)
        assertEquals(V1TextOutboxPhase.UNCONFIRMED, restored.entries.first().state)
        restored.finishAttempt(next.id, V1TextOutboxResult.UNCONFIRMED)
        assertEquals(V1TextOutboxPhase.UNCONFIRMED, restored.entries.last().state)
        assertNull(restored.beginAttempt())
    }

    @Test
    fun differentRecipientInvalidatesOldTokenAndDoesNotSendItsWaitingText() {
        val outbox = V1TextOutbox(MemoryStorage()) { 0L }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        val oldRecipient = assertNotNull(outbox.recipient)
        assertTrue(outbox.enqueue(oldRecipient, "Only for the first device"))

        assertTrue(outbox.bindRecipient(RECIPIENT))
        assertSame(oldRecipient, outbox.recipient)
        assertTrue(outbox.bindRecipient(OTHER_RECIPIENT))
        val currentRecipient = assertNotNull(outbox.recipient)
        assertNotSame(oldRecipient, currentRecipient)
        assertFalse(outbox.enqueue(oldRecipient, "Stale composer"))
        assertEquals(V1TextOutboxPhase.FAILED, outbox.entries.single().state)
        assertNull(outbox.beginAttempt())

        assertTrue(outbox.enqueue(currentRecipient, "Only for the second device"))
        val attempt = assertNotNull(outbox.beginAttempt())
        assertEquals("Only for the second device", attempt.text)
        outbox.finishAttempt(attempt.id, V1TextOutboxResult.FAILED)
        assertEquals(V1TextOutboxPhase.FAILED, outbox.entries.last().state)
        assertNull(outbox.beginAttempt())
    }

    @Test
    fun storageRefusalAtAttemptStartPreventsSendingAndLatches() {
        val storage = MemoryStorage()
        val outbox = V1TextOutbox(storage) { 0L }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        val recipient = assertNotNull(outbox.recipient)
        assertTrue(outbox.enqueue(recipient, "Saved while disconnected"))

        storage.accepting = false
        assertNull(outbox.beginAttempt())
        assertTrue(outbox.unavailable)
        val persisted = assertNotNull(storage.snapshot).entries.single()
        assertEquals(V1TextOutboxPhase.WAITING, persisted.state)
        assertEquals(0, persisted.attempts)

        storage.accepting = true
        assertNull(outbox.beginAttempt())
        assertFalse(outbox.enqueue(recipient, "Must fail until reloaded"))
        val restored = V1TextOutbox(storage) { 0L }
        assertTrue(restored.bindRecipient(RECIPIENT))
        assertEquals(persisted.id, assertNotNull(restored.beginAttempt()).id)
    }

    @Test
    fun storageRefusalAtCompletionLeavesDurableAttemptUnconfirmedOnReload() {
        val storage = MemoryStorage()
        val outbox = V1TextOutbox(storage) { 0L }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        assertTrue(outbox.enqueue(assertNotNull(outbox.recipient), "Keep attempt evidence"))
        val sent = assertNotNull(outbox.beginAttempt())

        storage.accepting = false
        outbox.finishAttempt(sent.id, V1TextOutboxResult.SENT)
        assertTrue(outbox.unavailable)
        assertEquals(V1TextOutboxPhase.IN_FLIGHT, storage.snapshot?.entries?.single()?.state)

        storage.accepting = true
        val restored = V1TextOutbox(storage) { 0L }
        assertTrue(restored.bindRecipient(RECIPIENT))
        assertEquals(V1TextOutboxPhase.UNCONFIRMED, restored.entries.single().state)
        assertNull(restored.beginAttempt())
    }

    @Test
    fun fullQueueRejectsNewTextUntilAnOldTerminalEntryCanBeEvicted() {
        val outbox = V1TextOutbox(MemoryStorage()) { 0L }
        assertTrue(outbox.bindRecipient(RECIPIENT))
        val recipient = assertNotNull(outbox.recipient)
        repeat(V1_TEXT_OUTBOX_LIMIT) { index ->
            assertTrue(outbox.enqueue(recipient, "Message $index"))
        }
        assertFalse(outbox.enqueue(recipient, "Rejected while full"))
        val first = assertNotNull(outbox.beginAttempt())
        assertFalse(outbox.enqueue(recipient, "Rejected while in flight"))
        outbox.finishAttempt(first.id, V1TextOutboxResult.SENT)
        val second = assertNotNull(outbox.beginAttempt())
        outbox.finishAttempt(second.id, V1TextOutboxResult.FAILED)

        assertTrue(outbox.enqueue(recipient, "Message 20"))
        assertEquals(20, outbox.entries.size)
        assertEquals((1..20).map { "Message $it" }, outbox.entries.map { it.text })
        assertEquals(V1TextOutboxPhase.FAILED, outbox.entries.first().state)
        assertEquals(V1TextOutboxPhase.WAITING, outbox.entries.last().state)
    }

    private class MemoryStorage : V1TextOutboxStorage {
        var snapshot: V1TextOutboxSnapshot? = V1TextOutboxSnapshot()
            private set
        var accepting = true

        override fun load(): V1TextOutboxSnapshot? = snapshot

        override fun save(snapshot: V1TextOutboxSnapshot): Boolean {
            if (!accepting) return false
            this.snapshot = snapshot
            return true
        }
    }

    private companion object {
        val RECIPIENT = "01".repeat(32)
        val OTHER_RECIPIENT = "23".repeat(32)
    }
}
