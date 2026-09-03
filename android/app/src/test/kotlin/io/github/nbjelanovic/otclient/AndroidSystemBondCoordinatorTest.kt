package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs

class AndroidSystemBondCoordinatorTest {
    @Test
    fun unbondedCandidateGetsOneSystemAttemptAndExactBondedCallbackProceeds() {
        val coordinator = AndroidSystemBondCoordinator()
        assertIs<AndroidSystemBondAction.RequestSystemBond>(coordinator.start("opaque-a", 7, false))
        assertIs<AndroidSystemBondAction.Await>(
            coordinator.onBondState("opaque-a", 7, AndroidSystemBondState.BONDING),
        )
        assertIs<AndroidSystemBondAction.ProceedToGatt>(
            coordinator.onBondState("opaque-a", 7, AndroidSystemBondState.BONDED),
        )
        assertEquals(
            AndroidSystemBondFailure.START_REJECTED,
            assertIs<AndroidSystemBondAction.Failed>(coordinator.start("opaque-a", 8, false)).reason,
        )
    }

    @Test
    fun alreadyBondedCandidateProceedsWithoutRequestingAnotherBond() {
        assertIs<AndroidSystemBondAction.ProceedToGatt>(
            AndroidSystemBondCoordinator().start("opaque-a", 1, true),
        )
    }

    @Test
    fun freshAttemptProofBridgesBondStatePublicationLagAndClearsWithLease() {
        val prerequisite = AndroidLeaseBondPrerequisite()
        assertEquals(false, prerequisite.isSatisfied(AndroidSystemBondState.BONDING))

        prerequisite.latchFreshAttemptProof()

        assertEquals(true, prerequisite.isSatisfied(AndroidSystemBondState.BONDING))
        prerequisite.clear()
        assertEquals(false, prerequisite.isSatisfied(AndroidSystemBondState.BONDING))
    }

    @Test
    fun alreadyBondedPathRemainsBoundToLiveReportedState() {
        val prerequisite = AndroidLeaseBondPrerequisite()
        assertEquals(true, prerequisite.isSatisfied(AndroidSystemBondState.BONDED))
        assertEquals(false, prerequisite.isSatisfied(AndroidSystemBondState.BONDING))
        assertEquals(false, prerequisite.isSatisfied(AndroidSystemBondState.NONE))
    }

    @Test
    fun deferredProfileReadyGateBeginsAndDeliversExactlyOnce() {
        val gate = AndroidDeferredProfileReadyGate()
        assertEquals(AndroidDeferredProfileReadyState.IDLE, gate.state)
        assertEquals(false, gate.hasStarted())

        assertEquals(true, gate.begin())
        assertEquals(AndroidDeferredProfileReadyState.PENDING, gate.state)
        assertEquals(true, gate.hasStarted())
        assertEquals(false, gate.begin())

        assertEquals(true, gate.deliver())
        assertEquals(AndroidDeferredProfileReadyState.DELIVERED, gate.state)
        assertEquals(false, gate.deliver())
        assertEquals(false, gate.rejectPost())
        assertEquals(false, gate.begin())
    }

    @Test
    fun deferredProfileReadyGateCloseSuppressesPendingAndFutureDelivery() {
        val gate = AndroidDeferredProfileReadyGate()
        assertEquals(true, gate.begin())

        gate.close()

        assertEquals(AndroidDeferredProfileReadyState.CLOSED, gate.state)
        assertEquals(false, gate.deliver())
        assertEquals(false, gate.rejectPost())
        assertEquals(false, gate.begin())
        gate.close()
        assertEquals(AndroidDeferredProfileReadyState.CLOSED, gate.state)
    }

    @Test
    fun deferredProfileReadyGatePostRejectionIsTerminalAndReportedOnce() {
        val gate = AndroidDeferredProfileReadyGate()
        assertEquals(true, gate.begin())

        assertEquals(true, gate.rejectPost())
        assertEquals(AndroidDeferredProfileReadyState.POST_REJECTED, gate.state)
        assertEquals(false, gate.rejectPost())
        assertEquals(false, gate.deliver())
        assertEquals(false, gate.begin())
    }

    @Test
    fun consumedAttemptGateRejectsQueuedDuplicateAndWrongGeneration() {
        val gate = AndroidSystemBondAttemptGate(7)
        assertEquals(true, gate.begin())
        assertEquals(false, gate.begin())
        assertEquals(true, gate.allows(7))
        assertEquals(false, gate.allows(8))
        gate.end()
        assertEquals(false, gate.allows(7))
    }

    @Test
    fun terminalBondObservationIsDeliveredOnceAcrossReceiverAndPollOrdering() {
        repeat(2) {
            val coordinator = AndroidSystemBondCoordinator()
            val gate = AndroidSystemBondAttemptGate(7)
            assertIs<AndroidSystemBondAction.RequestSystemBond>(coordinator.start("opaque-a", 7, false))
            assertEquals(true, gate.begin())
            var proceeds = 0
            var failures = 0

            fun observe(state: AndroidSystemBondState) {
                if (!gate.allows(7)) return
                when (coordinator.onBondState("opaque-a", 7, state)) {
                    AndroidSystemBondAction.ProceedToGatt -> {
                        gate.end()
                        proceeds += 1
                    }
                    is AndroidSystemBondAction.Failed -> {
                        gate.end()
                        failures += 1
                    }
                    else -> Unit
                }
            }

            observe(AndroidSystemBondState.BONDING)
            observe(AndroidSystemBondState.BONDED)
            observe(AndroidSystemBondState.BONDED)
            assertEquals(1, proceeds)
            assertEquals(0, failures)
        }
    }

    @Test
    fun polledStateRequiresObservedBondingBeforeTerminalForwarding() {
        assertEquals(
            false,
            AndroidSystemBondPollingPolicy.shouldForward(AndroidSystemBondState.NONE, bondingObserved = false),
        )
        assertEquals(
            false,
            AndroidSystemBondPollingPolicy.shouldForward(AndroidSystemBondState.BONDED, bondingObserved = false),
        )
        assertEquals(
            true,
            AndroidSystemBondPollingPolicy.shouldForward(AndroidSystemBondState.BONDING, bondingObserved = false),
        )
        assertEquals(
            true,
            AndroidSystemBondPollingPolicy.shouldForward(AndroidSystemBondState.NONE, bondingObserved = true),
        )
        assertEquals(
            true,
            AndroidSystemBondPollingPolicy.shouldForward(AndroidSystemBondState.BONDED, bondingObserved = true),
        )
    }

    @Test
    fun duplicateExactBondingObservationsFromBroadcastAndPollRemainAwaiting() {
        val coordinator = AndroidSystemBondCoordinator()
        assertIs<AndroidSystemBondAction.RequestSystemBond>(coordinator.start("opaque-a", 1, false))
        assertIs<AndroidSystemBondAction.Await>(
            coordinator.onBondState("opaque-a", 1, AndroidSystemBondState.BONDING),
        )
        assertIs<AndroidSystemBondAction.Await>(
            coordinator.onBondState("opaque-a", 1, AndroidSystemBondState.BONDING),
        )
        assertIs<AndroidSystemBondAction.ProceedToGatt>(
            coordinator.onBondState("opaque-a", 1, AndroidSystemBondState.BONDED),
        )
    }

    @Test
    fun bondedCallbackWithoutThisAttemptsBondingTransitionIsRejected() {
        val coordinator = AndroidSystemBondCoordinator()
        assertIs<AndroidSystemBondAction.RequestSystemBond>(coordinator.start("opaque-a", 1, false))
        assertEquals(
            AndroidSystemBondFailure.CALLBACK_MISMATCH,
            assertIs<AndroidSystemBondAction.Failed>(
                coordinator.onBondState("opaque-a", 1, AndroidSystemBondState.BONDED),
            ).reason,
        )
    }

    @Test
    fun wrongCandidateOrGenerationTerminatesFailClosed() {
        for ((token, generation) in listOf("opaque-b" to 1L, "opaque-a" to 2L)) {
            val coordinator = AndroidSystemBondCoordinator()
            coordinator.start("opaque-a", 1, false)
            assertEquals(
                AndroidSystemBondFailure.CALLBACK_MISMATCH,
                assertIs<AndroidSystemBondAction.Failed>(
                    coordinator.onBondState(token, generation, AndroidSystemBondState.BONDED),
                ).reason,
            )
            assertEquals(
                AndroidSystemBondFailure.CALLBACK_MISMATCH,
                assertIs<AndroidSystemBondAction.Failed>(
                    coordinator.onBondState("opaque-a", 1, AndroidSystemBondState.BONDED),
                ).reason,
            )
        }
    }

    @Test
    fun cancellationFailurePermissionLifecycleAndDisconnectAreTerminalWithoutRetry() {
        val cases = listOf(
            AndroidSystemBondFailure.BOND_CANCELLED_OR_FAILED,
            AndroidSystemBondFailure.PERMISSION_LOST,
            AndroidSystemBondFailure.TIMEOUT,
            AndroidSystemBondFailure.LIFECYCLE_ENDED,
            AndroidSystemBondFailure.DISCONNECTED,
        )
        for (reason in cases) {
            val coordinator = AndroidSystemBondCoordinator()
            coordinator.start("opaque-a", 9, false)
            val result = if (reason == AndroidSystemBondFailure.BOND_CANCELLED_OR_FAILED) {
                coordinator.onBondState("opaque-a", 9, AndroidSystemBondState.NONE)
            } else {
                coordinator.fail(reason)
            }
            assertEquals(reason, assertIs<AndroidSystemBondAction.Failed>(result).reason)
            assertEquals(
                AndroidSystemBondFailure.CALLBACK_MISMATCH,
                assertIs<AndroidSystemBondAction.Failed>(
                    coordinator.onBondState("opaque-a", 9, AndroidSystemBondState.BONDED),
                ).reason,
            )
        }
    }

    @Test
    fun invalidStartCannotExposeOrAcceptSecretMaterial() {
        val coordinator = AndroidSystemBondCoordinator()
        assertEquals(
            AndroidSystemBondFailure.START_REJECTED,
            assertIs<AndroidSystemBondAction.Failed>(coordinator.start("", 1, false)).reason,
        )
        val publicNames = AndroidSystemBondFailure.entries.joinToString("|") { it.name }
        check(!publicNames.contains("PIN", ignoreCase = true))
        check(!publicNames.contains("PASSKEY", ignoreCase = true))
    }
}
