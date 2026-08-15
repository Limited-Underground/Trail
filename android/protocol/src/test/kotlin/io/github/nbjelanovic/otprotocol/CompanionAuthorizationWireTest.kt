package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class CompanionAuthorizationWireTest {
    private val vectors by lazy {
        checkNotNull(javaClass.classLoader.getResourceAsStream("companion_authorization_wire_v0_vectors.csv"))
            .bufferedReader().readLines().drop(1).filter(String::isNotBlank)
            .associate { line ->
                val fields = line.split(',', limit = 2)
                fields[0] to fields[1].decodeHex()
            }
    }
    private val correlation = CompanionAuthorizationCorrelation(ByteArray(16) { (0xa0 + it).toByte() })

    @Test
    fun fixedPayloadsAndOtc0EnvelopesMatchCppGeneratedVectors() {
        assertContentEquals(
            vectors.getValue("claim_start_authorize"),
            CompanionAuthorizationWireCodec.encodeClaimStart(start()),
        )
        assertContentEquals(
            vectors.getValue("claim_start_replace"),
            CompanionAuthorizationWireCodec.encodeClaimStart(start(CompanionAuthorizationPurpose.REPLACE_CONTROLLER)),
        )
        assertContentEquals(
            vectors.getValue("claim_pending_authorize"),
            CompanionAuthorizationWireCodec.encodeClaimStatus(status()).value,
        )
        assertContentEquals(
            vectors.getValue("claim_pending_replace"),
            CompanionAuthorizationWireCodec.encodeClaimStatus(
                status(CompanionAuthorizationPurpose.REPLACE_CONTROLLER),
            ).value,
        )
        assertContentEquals(
            vectors.getValue("claim_accepted"),
            CompanionAuthorizationWireCodec.encodeClaimResult(result()).value,
        )
        assertContentEquals(
            vectors.getValue("claim_denied_unsupported"),
            CompanionAuthorizationWireCodec.encodeClaimResult(
                result(
                    outcome = CompanionAuthorizationClaimOutcome.DENIED,
                    reason = CompanionAuthorizationDenyReason.UNSUPPORTED,
                ),
            ).value,
        )
        assertContentEquals(
            vectors.getValue("claim_replaced"),
            CompanionAuthorizationWireCodec.encodeClaimResult(
                result(
                    purpose = CompanionAuthorizationPurpose.REPLACE_CONTROLLER,
                    outcome = CompanionAuthorizationClaimOutcome.REPLACED,
                ),
            ).value,
        )

        listOf(
            "otc0_claim_start_authorize_11223344_55667788",
            "otc0_claim_pending_authorize_11223344_55667788",
            "otc0_claim_accepted_11223344_55667788",
        ).forEach { name ->
            val decoded = CompanionProtocolCodec.decodeFragment(vectors.getValue(name))
            assertTrue(decoded.isSuccess, name)
            assertContentEquals(
                vectors.getValue(name),
                CompanionProtocolCodec.encodeFragment(decoded.value!!).value,
                name,
            )
            assertEquals(
                CompanionAuthorizationWireError.NONE,
                CompanionAuthorizationWireCodec.validateAuthorizationFragment(decoded.value!!),
                name,
            )
        }
    }

    @Test
    fun payloadsRoundTripEveryClosedEnumAndRemainExactSize() {
        CompanionAuthorizationPurpose.entries.forEach { purpose ->
            val start = start(purpose)
            assertEquals(start, CompanionAuthorizationWireCodec.decodeClaimStart(
                CompanionAuthorizationWireCodec.encodeClaimStart(start),
            ).value)
            val pending = status(purpose)
            assertEquals(pending, CompanionAuthorizationWireCodec.decodeClaimStatus(
                CompanionAuthorizationWireCodec.encodeClaimStatus(pending).value!!,
            ).value)
        }
        CompanionAuthorizationDenyReason.entries.filter { it != CompanionAuthorizationDenyReason.NONE }.forEach { reason ->
            val denied = result(outcome = CompanionAuthorizationClaimOutcome.DENIED, reason = reason)
            assertEquals(denied, CompanionAuthorizationWireCodec.decodeClaimResult(
                CompanionAuthorizationWireCodec.encodeClaimResult(denied).value!!,
            ).value)
        }
        assertEquals(COMPANION_AUTHORIZATION_CLAIM_START_BYTES, vectors.getValue("claim_start_authorize").size)
        assertEquals(COMPANION_AUTHORIZATION_CLAIM_STATUS_BYTES, vectors.getValue("claim_pending_authorize").size)
        assertEquals(COMPANION_AUTHORIZATION_CLAIM_RESULT_BYTES, vectors.getValue("claim_accepted").size)
    }

    @Test
    fun decodersRejectMalformedVersionsEnumsReservesZeroCorrelationAndIncoherence() {
        val start = vectors.getValue("claim_start_authorize")
        assertError(CompanionAuthorizationWireError.MALFORMED) {
            CompanionAuthorizationWireCodec.decodeClaimStart(start.copyOf(7))
        }
        assertError(CompanionAuthorizationWireError.MALFORMED) {
            CompanionAuthorizationWireCodec.decodeClaimStart(start.changed(0, 0))
        }
        assertError(CompanionAuthorizationWireError.UNSUPPORTED_VERSION) {
            CompanionAuthorizationWireCodec.decodeClaimStart(start.changed(4, 1))
        }
        assertError(CompanionAuthorizationWireError.UNKNOWN_PURPOSE) {
            CompanionAuthorizationWireCodec.decodeClaimStart(start.changed(6, 0xff))
        }
        assertError(CompanionAuthorizationWireError.RESERVED_BITS_SET) {
            CompanionAuthorizationWireCodec.decodeClaimStart(start.changed(7, 1))
        }

        val pending = vectors.getValue("claim_pending_authorize")
        assertError(CompanionAuthorizationWireError.UNKNOWN_STATE) {
            CompanionAuthorizationWireCodec.decodeClaimStatus(pending.changed(7, 2))
        }
        assertError(CompanionAuthorizationWireError.INVALID_CORRELATION) {
            CompanionAuthorizationWireCodec.decodeClaimStatus(pending.copyOf().also { bytes -> (8..23).forEach { bytes[it] = 0 } })
        }

        val accepted = vectors.getValue("claim_accepted")
        assertError(CompanionAuthorizationWireError.UNKNOWN_OUTCOME) {
            CompanionAuthorizationWireCodec.decodeClaimResult(accepted.changed(7, 0xff))
        }
        assertError(CompanionAuthorizationWireError.UNKNOWN_REASON) {
            CompanionAuthorizationWireCodec.decodeClaimResult(accepted.changed(8, 0xff))
        }
        assertError(CompanionAuthorizationWireError.RESERVED_BITS_SET) {
            CompanionAuthorizationWireCodec.decodeClaimResult(accepted.changed(9, 1))
        }
        assertError(CompanionAuthorizationWireError.INCOHERENT_RESULT) {
            CompanionAuthorizationWireCodec.decodeClaimResult(accepted.changed(8, 1))
        }
        assertError(CompanionAuthorizationWireError.INCOHERENT_RESULT) {
            CompanionAuthorizationWireCodec.decodeClaimResult(
                vectors.getValue("claim_denied_unsupported").changed(8, 0),
            )
        }
        assertError(CompanionAuthorizationWireError.INCOHERENT_RESULT) {
            CompanionAuthorizationWireCodec.decodeClaimResult(accepted.changed(6, 2))
        }
    }

    @Test
    fun correlationIngressIsFixedBoundCopiedAndAlwaysRedacted() {
        val source = ByteArray(16) { (it + 1).toByte() }
        val copied = CompanionAuthorizationCorrelation(source)
        source.fill(0)
        assertTrue(CompanionAuthorizationWireCodec.encodeClaimStatus(status(correlation = copied)).isSuccess)
        assertFalse(copied.toString().contains("1"))
        assertTrue(copied.toString().contains("redacted"))

        listOf(ByteArray(0), ByteArray(16), ByteArray(17), ByteArray(1_000_000) { 1 }).forEach { invalid ->
            val bounded = CompanionAuthorizationCorrelation(invalid)
            assertEquals(
                CompanionAuthorizationWireError.INVALID_CORRELATION,
                CompanionAuthorizationWireCodec.encodeClaimStatus(status(correlation = bounded)).error,
            )
            assertEquals("CompanionAuthorizationCorrelation(redacted)", bounded.toString())
        }
    }

    @Test
    fun authorizationKindsBindOnlyToExactSingleFragments() {
        val cases = listOf(
            fragment(CompanionFrameKind.AUTHORIZATION_CLAIM_START, vectors.getValue("claim_start_authorize")),
            fragment(CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS, vectors.getValue("claim_pending_authorize")),
            fragment(CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT, vectors.getValue("claim_accepted")),
        )
        cases.forEach { candidate ->
            assertEquals(CompanionAuthorizationWireError.NONE, CompanionAuthorizationWireCodec.validateAuthorizationFragment(candidate))
            CompanionFrameKind.entries.filter { it != candidate.kind }.forEach { wrongKind ->
                assertNotEquals(
                    CompanionAuthorizationWireError.NONE,
                    CompanionAuthorizationWireCodec.validateAuthorizationFragment(candidate.copy(kind = wrongKind)),
                )
            }
            assertEquals(
                CompanionAuthorizationWireError.MALFORMED,
                CompanionAuthorizationWireCodec.validateAuthorizationFragment(candidate.copy(fragmentCount = 2)),
            )
        }
    }

    @Test
    fun provisionalAdmissionRequiresEveryExplicitFutureEvidence() {
        val tracker = CompanionAuthorizationResponseTracker()
        assertEquals(CompanionAuthorizationWireError.NO_PROVISIONAL_SESSION, tracker.begin(7uL, startFragment()))
        assertEquals(
            CompanionAuthorizationWireError.INVALID_TRANSPORT_GENERATION,
            tracker.openProvisionalSession(evidence(0uL), SESSION),
        )
        assertEquals(
            CompanionAuthorizationWireError.INVALID_TRANSPORT_GENERATION,
            tracker.openProvisionalSession(evidence(), 0),
        )
        assertEquals(
            CompanionAuthorizationWireError.LINK_NOT_ENCRYPTED,
            tracker.openProvisionalSession(evidence(encrypted = false), SESSION),
        )
        assertEquals(
            CompanionAuthorizationWireError.BOND_NOT_AUTHENTICATED,
            tracker.openProvisionalSession(evidence(authenticated = false), SESSION),
        )
        assertEquals(
            CompanionAuthorizationWireError.CLAIM_CAPABILITY_NOT_NEGOTIATED,
            tracker.openProvisionalSession(evidence(supported = false), SESSION),
        )
        assertEquals(CompanionAuthorizationWireError.NONE, tracker.openProvisionalSession(evidence(), SESSION))
        assertFalse(tracker.allowsNormalCompanionTraffic(GENERATION))
        assertEquals(CompanionAuthorizationWireError.CLAIM_IN_PROGRESS, tracker.openProvisionalSession(evidence(8uL), SESSION))
    }

    @Test
    fun exactPendingThenAcceptedOrReplacedAloneOpensClientObservedTraffic() {
        val accepted = openedTracker()
        assertEquals(CompanionAuthorizationWireError.NONE, accepted.begin(GENERATION, startFragment()))
        assertTrue(accepted.observe(GENERATION, pendingFragment()).accepted)
        assertFalse(accepted.allowsNormalCompanionTraffic(GENERATION))
        val terminal = accepted.observe(GENERATION, resultFragment())
        assertTrue(terminal.terminal)
        assertEquals(CompanionAuthorizationClaimOutcome.ACCEPTED, terminal.outcome)
        assertTrue(accepted.allowsNormalCompanionTraffic(GENERATION))
        assertFalse(accepted.status().provisionalSessionOpen)
        assertEquals(CompanionAuthorizationWireError.DUPLICATE_RESPONSE, accepted.cancel(GENERATION))
        assertTrue(accepted.allowsNormalCompanionTraffic(GENERATION))

        val replacement = openedTracker()
        assertEquals(
            CompanionAuthorizationWireError.NONE,
            replacement.begin(GENERATION, startFragment(CompanionAuthorizationPurpose.REPLACE_CONTROLLER)),
        )
        assertTrue(replacement.observe(
            GENERATION,
            pendingFragment(CompanionAuthorizationPurpose.REPLACE_CONTROLLER),
        ).accepted)
        val replaced = replacement.observe(
            GENERATION,
            resultFragment(
                purpose = CompanionAuthorizationPurpose.REPLACE_CONTROLLER,
                outcome = CompanionAuthorizationClaimOutcome.REPLACED,
            ),
        )
        assertEquals(CompanionAuthorizationClaimOutcome.REPLACED, replaced.outcome)
        assertTrue(replacement.allowsNormalCompanionTraffic(GENERATION))
    }

    @Test
    fun authoritativeDeniedNeverOpensTrafficAndLocalTimeoutIsNotFabricated() {
        val tracker = openedTracker()
        tracker.begin(GENERATION, startFragment())
        tracker.observe(GENERATION, pendingFragment())
        val denied = tracker.observe(
            GENERATION,
            resultFragment(
                outcome = CompanionAuthorizationClaimOutcome.DENIED,
                reason = CompanionAuthorizationDenyReason.UNKNOWN,
            ),
        )
        assertTrue(denied.terminal)
        assertEquals(CompanionAuthorizationDenyReason.UNKNOWN, denied.reason)
        assertFalse(tracker.allowsNormalCompanionTraffic(GENERATION))
        assertFalse(tracker.status().provisionalSessionOpen)
        val terminalStatus = tracker.status()
        assertEquals(
            CompanionAuthorizationWireError.CLAIM_IN_PROGRESS,
            tracker.openProvisionalSession(evidence(GENERATION + 1u), SESSION),
        )
        assertEquals(terminalStatus, tracker.status())
        assertEquals(CompanionAuthorizationWireError.NONE, tracker.closeTransportGeneration(GENERATION))
        assertEquals(
            CompanionAuthorizationWireError.NONE,
            tracker.openProvisionalSession(evidence(GENERATION + 1u), SESSION),
        )
    }

    @Test
    fun wrongReplayDuplicateAndOutOfOrderResponsesRejectWithoutMutation() {
        val tracker = openedTracker()
        tracker.begin(GENERATION, startFragment())
        val before = tracker.status()
        assertEquals(
            CompanionAuthorizationWireError.RESPONSE_OUT_OF_ORDER,
            tracker.observe(GENERATION, resultFragment()).error,
        )
        assertEquals(before, tracker.status())
        assertEquals(
            CompanionAuthorizationWireError.WRONG_TRANSPORT_GENERATION,
            tracker.observe(GENERATION + 1u, pendingFragment()).error,
        )
        assertEquals(CompanionAuthorizationWireError.WRONG_SESSION, tracker.observe(
            GENERATION,
            pendingFragment().copy(sessionNonce = SESSION + 1),
        ).error)
        assertEquals(CompanionAuthorizationWireError.WRONG_EXCHANGE, tracker.observe(
            GENERATION,
            pendingFragment().copy(exchangeId = EXCHANGE + 1),
        ).error)
        assertEquals(CompanionAuthorizationWireError.PURPOSE_MISMATCH, tracker.observe(
            GENERATION,
            pendingFragment(CompanionAuthorizationPurpose.REPLACE_CONTROLLER),
        ).error)
        assertEquals(before, tracker.status())

        assertTrue(tracker.observe(GENERATION, pendingFragment()).accepted)
        val pending = tracker.status()
        assertEquals(CompanionAuthorizationWireError.DUPLICATE_RESPONSE, tracker.observe(GENERATION, pendingFragment()).error)
        assertEquals(CompanionAuthorizationWireError.CORRELATION_MISMATCH, tracker.observe(
            GENERATION,
            resultFragment(correlation = CompanionAuthorizationCorrelation(ByteArray(16) { 1 })),
        ).error)
        assertEquals(pending, tracker.status())
    }

    @Test
    fun exchangeBoundsCancelCloseAndGenerationReplayFailClosed() {
        listOf(-1L, 0L, 0x1_0000_0000L, Long.MAX_VALUE).forEach { invalid ->
            val tracker = openedTracker()
            val before = tracker.status()
            assertEquals(
                CompanionAuthorizationWireError.WRONG_EXCHANGE,
                tracker.begin(GENERATION, startFragment().copy(exchangeId = invalid)),
            )
            assertEquals(before, tracker.status())
        }
        val maximum = openedTracker()
        assertEquals(
            CompanionAuthorizationWireError.NONE,
            maximum.begin(GENERATION, startFragment().copy(exchangeId = 0xffff_ffffL)),
        )

        val cancelled = openedTracker()
        cancelled.begin(GENERATION, startFragment())
        assertEquals(CompanionAuthorizationWireError.WRONG_TRANSPORT_GENERATION, cancelled.cancel(GENERATION + 1u))
        assertEquals(CompanionAuthorizationWireError.NONE, cancelled.cancel(GENERATION))
        assertEquals(CompanionAuthorizationResponsePhase.IDLE, cancelled.status().phase)
        assertFalse(cancelled.status().provisionalSessionOpen)
        assertEquals(CompanionAuthorizationWireError.NO_CLAIM_IN_PROGRESS, cancelled.observe(GENERATION, pendingFragment()).error)
        val cancelledStatus = cancelled.status()
        assertEquals(
            CompanionAuthorizationWireError.CLAIM_IN_PROGRESS,
            cancelled.openProvisionalSession(evidence(GENERATION + 1u), SESSION),
        )
        assertEquals(cancelledStatus, cancelled.status())
        assertEquals(CompanionAuthorizationWireError.NONE, cancelled.closeTransportGeneration(GENERATION))
        assertEquals(CompanionAuthorizationWireError.STALE_START, cancelled.openProvisionalSession(evidence(), SESSION))
        assertEquals(CompanionAuthorizationWireError.NONE, cancelled.openProvisionalSession(evidence(GENERATION + 1u), SESSION))
    }

    private fun openedTracker(): CompanionAuthorizationResponseTracker = CompanionAuthorizationResponseTracker().also {
        assertEquals(CompanionAuthorizationWireError.NONE, it.openProvisionalSession(evidence(), SESSION))
    }

    private fun evidence(
        generation: ULong = GENERATION,
        encrypted: Boolean = true,
        authenticated: Boolean = true,
        supported: Boolean = true,
    ) = CompanionAuthorizationProvisionalEvidence(generation, encrypted, authenticated, supported)

    private fun start(purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER) =
        CompanionAuthorizationClaimStart(purpose)

    private fun status(
        purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
        correlation: CompanionAuthorizationCorrelation = this.correlation,
    ) = CompanionAuthorizationClaimStatus(purpose, CompanionAuthorizationClaimState.PENDING, correlation)

    private fun result(
        purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
        outcome: CompanionAuthorizationClaimOutcome = CompanionAuthorizationClaimOutcome.ACCEPTED,
        reason: CompanionAuthorizationDenyReason = CompanionAuthorizationDenyReason.NONE,
        correlation: CompanionAuthorizationCorrelation = this.correlation,
    ) = CompanionAuthorizationClaimResult(purpose, outcome, reason, correlation)

    private fun startFragment(
        purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
    ) = fragment(CompanionFrameKind.AUTHORIZATION_CLAIM_START, CompanionAuthorizationWireCodec.encodeClaimStart(start(purpose)))

    private fun pendingFragment(
        purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
    ) = fragment(
        CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS,
        CompanionAuthorizationWireCodec.encodeClaimStatus(status(purpose)).value!!,
    )

    private fun resultFragment(
        purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
        outcome: CompanionAuthorizationClaimOutcome = CompanionAuthorizationClaimOutcome.ACCEPTED,
        reason: CompanionAuthorizationDenyReason = CompanionAuthorizationDenyReason.NONE,
        correlation: CompanionAuthorizationCorrelation = this.correlation,
    ) = fragment(
        CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT,
        CompanionAuthorizationWireCodec.encodeClaimResult(result(purpose, outcome, reason, correlation)).value!!,
    )

    private fun fragment(kind: CompanionFrameKind, payload: ByteArray) =
        CompanionFragment(kind, SESSION, EXCHANGE, payload = payload)

    private fun <T> assertError(
        expected: CompanionAuthorizationWireError,
        operation: () -> CompanionAuthorizationCodecResult<T>,
    ) = assertEquals(expected, operation().error)

    private fun ByteArray.changed(offset: Int, value: Int): ByteArray = copyOf().also { it[offset] = value.toByte() }
    private fun String.decodeHex(): ByteArray = chunked(2).map { it.toInt(16).toByte() }.toByteArray()

    companion object {
        private const val SESSION = 0x11223344L
        private const val EXCHANGE = 0x55667788L
        private val GENERATION = 7uL
    }
}
