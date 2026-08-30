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
