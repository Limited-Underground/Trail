package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue

class V1GroupConfirmationTest {
    @Test
    fun productionAdapterIsExplicitlyUnavailable() {
        assertNull(DisabledV1GroupConfirmationAdapter.open(V1GroupConfirmationAuthority()))
    }

    @Test
    fun eitherRoleCanSubmitExactlyItsDeviceOfferWithoutActivation() {
        V1GroupConfirmationRole.entries.forEach { role ->
            val f = Fixture(role)
            assertTrue(f.coordinator.refresh())
            assertSame(f.offer, assertIs<V1GroupConfirmationState.Review>(f.coordinator.state).offer)
            assertTrue(f.coordinator.confirm(f.offer))
            assertEquals(V1GroupConfirmationState.Submitted(V1GroupConfirmationDecision.CONFIRM), f.coordinator.state)
            assertEquals(listOf(Pair(f.offer, V1GroupConfirmationDecision.CONFIRM)), f.source.calls)
            assertFalse(f.coordinator.confirm(f.offer))
            assertFalse(f.coordinator.cancel(f.offer))
            assertTrue(f.coordinator.refresh())
            assertEquals(1, f.source.polls)
            assertEquals(1, f.source.calls.size)
        }
    }

    @Test
    fun cancellationConsumesTheExactHandleBeforeCallingTheDevice() {
        val f = Fixture()
        assertTrue(f.coordinator.refresh())
        assertTrue(f.coordinator.cancel(f.offer))
        assertEquals(V1GroupConfirmationState.Submitted(V1GroupConfirmationDecision.CANCEL), f.coordinator.state)
        assertFalse(f.coordinator.confirm(f.offer))
        assertEquals(1, f.source.calls.size)
    }

    @Test
    fun equalLookingReconstructedHandleCannotConfirmTheOffer() {
        val f = Fixture()
        assertTrue(f.coordinator.refresh())
        assertFalse(f.coordinator.confirm(offer(f.authority)))
        f.closed(V1GroupConfirmationCloseReason.STALE_HANDLE)
        assertFalse(f.coordinator.confirm(f.offer))
        assertTrue(f.source.calls.isEmpty())
    }

    @Test
    fun foreignSessionOfferAndReconnectInvalidateWithoutSubmission() {
        val foreign = Fixture()
        foreign.source.next = offer(V1GroupConfirmationAuthority())
        assertFalse(foreign.coordinator.refresh())
        foreign.closed(V1GroupConfirmationCloseReason.INVALID_OFFER)
        val f = Fixture()
        assertTrue(f.coordinator.refresh())
        f.current = V1GroupConfirmationAuthority()
        assertFalse(f.coordinator.confirm(f.offer))
        f.closed(V1GroupConfirmationCloseReason.SESSION_ENDED)
        f.current = f.authority
        assertFalse(f.coordinator.refresh())
        assertTrue(f.source.calls.isEmpty())
    }

    @Test
    fun repeatedOrBusyOfferBurnsTheLocalAttempt() {
        listOf(1L, 2L).forEach { attempt ->
            val f = Fixture()
            assertTrue(f.coordinator.refresh())
            f.source.next = offer(f.authority, attempt = attempt)
            assertFalse(f.coordinator.refresh())
            f.closed(V1GroupConfirmationCloseReason.DUPLICATE_OFFER)
            assertFalse(f.coordinator.confirm(f.offer))
            assertTrue(f.source.calls.isEmpty())
        }
    }

    @Test
    fun exactExpiryAndClockRollbackNeverResurrectAnOffer() {
        listOf(1_000L to V1GroupConfirmationCloseReason.EXPIRED,
            99L to V1GroupConfirmationCloseReason.CLOCK_INVALID).forEach { (time, reason) ->
            val f = Fixture()
            assertTrue(f.coordinator.refresh())
            f.now = time
            assertFalse(f.coordinator.confirm(f.offer))
            f.closed(reason)
            f.now = 101L
            assertFalse(f.coordinator.refresh())
            assertTrue(f.source.calls.isEmpty())
        }
    }

    @Test
    fun invalidOrThrowingClockFailsBeforeAnyOfferPoll() {
        listOf(-1L, Long.MAX_VALUE).forEach { time ->
            val f = Fixture()
            f.now = time
            assertFalse(f.coordinator.refresh())
            f.closed(V1GroupConfirmationCloseReason.CLOCK_INVALID)
            assertEquals(0, f.source.polls)
        }
        val f = Fixture()
        f.clockThrows = true
        assertFalse(f.coordinator.refresh())
        f.closed(V1GroupConfirmationCloseReason.CLOCK_INVALID)
        assertEquals(0, f.source.polls)
    }

    @Test
    fun expiryOrAuthorityLossInsidePollCannotPublishReview() {
        listOf(false, true).forEach { disconnect ->
            val f = Fixture()
            f.source.onPoll = { if (disconnect) f.current = null else f.now = 1_000L }
            assertFalse(f.coordinator.refresh())
            f.closed(if (disconnect) V1GroupConfirmationCloseReason.SESSION_ENDED else V1GroupConfirmationCloseReason.EXPIRED)
            assertTrue(f.source.calls.isEmpty())
        }
    }

    @Test
    fun uncertainOrLateSubmissionCannotRetryOrClaimAnAcceptedResult() {
        repeat(4) { failure ->
            val f = Fixture()
            assertTrue(f.coordinator.refresh())
            f.source.onSubmit = {
                when (failure) {
                    0 -> false
                    1 -> throw IllegalStateException("private failure")
                    2 -> { f.now = 1_000L; true }
                    else -> { f.current = null; true }
                }
            }
            assertFalse(f.coordinator.confirm(f.offer))
            assertIs<V1GroupConfirmationState.Closed>(f.coordinator.state)
            assertFalse(f.coordinator.confirm(f.offer))
            assertEquals(1, f.source.calls.size)
        }
    }

    @Test
    fun submittedRequestRetainsOriginalDeadlineWithoutPollingOrRetry() {
        val f = Fixture()
        assertTrue(f.coordinator.refresh())
        assertEquals(900L, f.coordinator.remainingMillis())
        assertTrue(f.coordinator.confirm(f.offer))
        f.now = 1_000L
        assertFalse(f.coordinator.checkFreshness())
        f.closed(V1GroupConfirmationCloseReason.EXPIRED)
        assertNull(f.coordinator.remainingMillis())
        assertEquals(1, f.source.polls)
        assertEquals(1, f.source.calls.size)
    }

    @Test
    fun reentrantSourceCannotResurrectOrSubmitTwice() {
        val polling = Fixture()
        polling.source.onPoll = { assertFalse(polling.coordinator.refresh()) }
        assertFalse(polling.coordinator.refresh())
        polling.closed(V1GroupConfirmationCloseReason.REENTRANT)
        val submitting = Fixture()
        assertTrue(submitting.coordinator.refresh())
        submitting.source.onSubmit = { assertFalse(submitting.coordinator.confirm(submitting.offer)); true }
        assertFalse(submitting.coordinator.confirm(submitting.offer))
        submitting.closed(V1GroupConfirmationCloseReason.REENTRANT)
        assertEquals(1, submitting.source.calls.size)
    }

    @Test
    fun sourceFailureAndCloseAreStickyAndCloseIsIdempotent() {
        val f = Fixture()
        f.source.onPoll = { throw IllegalStateException("private detail") }
        assertFalse(f.coordinator.refresh())
        f.closed(V1GroupConfirmationCloseReason.SOURCE_UNAVAILABLE)
        f.coordinator.close()
        f.coordinator.close()
        assertEquals(1, f.source.closes)
        assertFalse(f.coordinator.refresh())
        assertFalse(f.coordinator.confirm(f.offer))
    }

    @Test
    fun malformedOfferFieldsRefuseAndDiagnosticStringsRedactValues() {
        val authority = V1GroupConfirmationAuthority()
        assertFailsWith<IllegalArgumentException> { offer(authority, attempt = 0) }
        assertFailsWith<IllegalArgumentException> { offer(authority, epoch = 0u) }
        assertFailsWith<IllegalArgumentException> { offer(authority, group = "name") }
        assertFailsWith<IllegalArgumentException> { offer(authority, fingerprint = "00") }
        assertFailsWith<IllegalArgumentException> { offer(authority, confirmation = "123\n456") }
        assertFailsWith<IllegalArgumentException> { offer(authority, deadline = Long.MAX_VALUE) }
        val f = Fixture()
        assertTrue(f.coordinator.refresh())
        listOf(f.offer.toString(), f.authority.toString(), f.coordinator.state.toString()).forEach {
            assertFalse(it.contains(f.offer.group))
            assertFalse(it.contains(f.offer.peerFingerprint))
            assertFalse(it.contains(f.offer.confirmation))
        }
    }

    @Test
    fun exactDeviceResultsAreCategoricalAndTerminal() {
        V1GroupConfirmationDecision.entries.forEach { decision ->
            val f = Fixture()
            assertTrue(f.coordinator.refresh())
            assertTrue(if (decision == V1GroupConfirmationDecision.CONFIRM) f.coordinator.confirm(f.offer)
                else f.coordinator.cancel(f.offer))
            val outcome = if (decision == V1GroupConfirmationDecision.CONFIRM)
                V1GroupConfirmationDeviceOutcome.LOCAL_CONFIRMED else V1GroupConfirmationDeviceOutcome.CANCELLED
            f.source.result = V1GroupConfirmationResult(f.offer, decision, outcome)
            assertTrue(f.coordinator.refresh())
            assertEquals(V1GroupConfirmationState.DeviceResult(decision, outcome), f.coordinator.state)
            assertNull(f.coordinator.deadlineMillis)
            assertFalse(f.coordinator.refresh())
            assertFalse(f.coordinator.confirm(f.offer))
            assertEquals(1, f.source.calls.size)
        }
    }

    @Test
    fun ForeignWrongDecisionAndExpiredResultsCannotClaimDeviceAcceptance() {
        repeat(4) { failure ->
            val f = Fixture()
            assertTrue(f.coordinator.refresh())
            assertTrue(f.coordinator.confirm(f.offer))
            f.source.result = V1GroupConfirmationResult(
                if (failure == 0) offer(f.authority) else f.offer,
                if (failure == 1) V1GroupConfirmationDecision.CANCEL else V1GroupConfirmationDecision.CONFIRM,
                if (failure == 2) V1GroupConfirmationDeviceOutcome.CANCELLED else V1GroupConfirmationDeviceOutcome.LOCAL_CONFIRMED)
            if (failure == 3) f.now = 1_000L
            assertFalse(f.coordinator.refresh())
            f.closed(if (failure == 3) V1GroupConfirmationCloseReason.EXPIRED else V1GroupConfirmationCloseReason.INVALID_RESULT)
            assertEquals(1, f.source.calls.size)
        }
    }

    private class Source : V1GroupConfirmationSource {
        var next: V1GroupConfirmationOffer? = null
        var result: V1GroupConfirmationResult? = null
        var polls = 0
        var closes = 0
        val calls = mutableListOf<Pair<V1GroupConfirmationOffer, V1GroupConfirmationDecision>>()
        var onPoll: () -> Unit = {}
        var onSubmit: () -> Boolean = { true }
        override fun pollOffer(): V1GroupConfirmationOffer? {
            polls++
            onPoll()
            return next.also { next = null }
        }
        override fun submit(offer: V1GroupConfirmationOffer, decision: V1GroupConfirmationDecision): Boolean {
            calls += Pair(offer, decision)
            return onSubmit()
        }
        override fun pollResult(): V1GroupConfirmationResult? = result.also { result = null }
        override fun close() { closes++ }
    }

    private class Fixture(role: V1GroupConfirmationRole = V1GroupConfirmationRole.JOINER) {
        val authority = V1GroupConfirmationAuthority()
        var current: V1GroupConfirmationAuthority? = authority
        var now = 100L
        var clockThrows = false
        val offer = offer(authority, role = role)
        val source = Source().apply { next = offer }
        val coordinator = V1GroupConfirmationCoordinator(authority, source, { current }, {
            if (clockThrows) error("private clock failure")
            now
        })
        fun closed(reason: V1GroupConfirmationCloseReason) {
            assertEquals(V1GroupConfirmationState.Closed(reason), coordinator.state)
        }
    }

    companion object {
        private fun offer(
            authority: V1GroupConfirmationAuthority,
            attempt: Long = 1,
            role: V1GroupConfirmationRole = V1GroupConfirmationRole.JOINER,
            group: String = "a1".repeat(16),
            epoch: UInt = 1u,
            fingerprint: String = "b2".repeat(32),
            confirmation: String = "1234 5678",
            deadline: Long = 1_000,
        ) = V1GroupConfirmationOffer(authority, attempt, role, group, epoch, fingerprint, confirmation, deadline)
    }
}
