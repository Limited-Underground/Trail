package io.github.nbjelanovic.otclient

internal enum class AndroidSystemBondState {
    NONE,
    BONDING,
    BONDED,
}

internal class AndroidSystemBondAttemptGate(private val generation: Long) {
    private var active = false

    fun begin(): Boolean {
        if (active || generation <= 0) return false
        active = true
        return true
    }

    fun allows(generation: Long): Boolean = active && this.generation == generation

    fun end() {
        active = false
    }
}

internal class AndroidLeaseBondPrerequisite {
    private var freshAttemptProof = false

    fun latchFreshAttemptProof() {
        freshAttemptProof = true
    }

    fun isSatisfied(reportedState: AndroidSystemBondState?): Boolean =
        freshAttemptProof || reportedState == AndroidSystemBondState.BONDED

    fun clear() {
        freshAttemptProof = false
    }
}

internal enum class AndroidDeferredProfileReadyState {
    IDLE,
    PENDING,
    DELIVERED,
    POST_REJECTED,
    CLOSED,
}

/** One-shot authority for deferring ProfileReady beyond the service-discovery callback turn. */
internal class AndroidDeferredProfileReadyGate {
    var state: AndroidDeferredProfileReadyState = AndroidDeferredProfileReadyState.IDLE
        private set

    fun hasStarted(): Boolean = state != AndroidDeferredProfileReadyState.IDLE

    fun begin(): Boolean {
        if (state != AndroidDeferredProfileReadyState.IDLE) return false
        state = AndroidDeferredProfileReadyState.PENDING
        return true
    }

    fun deliver(): Boolean {
        if (state != AndroidDeferredProfileReadyState.PENDING) return false
        state = AndroidDeferredProfileReadyState.DELIVERED
        return true
    }

    fun rejectPost(): Boolean {
        if (state != AndroidDeferredProfileReadyState.PENDING) return false
        state = AndroidDeferredProfileReadyState.POST_REJECTED
        return true
    }

    fun close() {
        state = AndroidDeferredProfileReadyState.CLOSED
    }
}

internal object AndroidSystemBondPollingPolicy {
    fun shouldForward(state: AndroidSystemBondState, bondingObserved: Boolean): Boolean = when (state) {
        AndroidSystemBondState.BONDING -> true
        AndroidSystemBondState.BONDED,
        AndroidSystemBondState.NONE,
        -> bondingObserved
    }
}

internal enum class AndroidSystemBondFailure {
    START_REJECTED,
    BOND_CANCELLED_OR_FAILED,
    PERMISSION_LOST,
    TIMEOUT,
    LIFECYCLE_ENDED,
    DISCONNECTED,
    CALLBACK_MISMATCH,
}

internal sealed interface AndroidSystemBondAction {
    data object RequestSystemBond : AndroidSystemBondAction
    data object ProceedToGatt : AndroidSystemBondAction
    data object Await : AndroidSystemBondAction
    data class Failed(val reason: AndroidSystemBondFailure) : AndroidSystemBondAction
}

/**
 * One-attempt owner for Android's system pairing surface. It never receives a passkey and accepts
 * a bond result only for the exact opaque candidate and generation that opened the attempt.
 */
internal class AndroidSystemBondCoordinator {
    private var endpointToken: String? = null
    private var generation: Long = 0
    private var active = false
    private var terminal = false
    private var sawBonding = false

    fun start(
        endpointToken: String,
        generation: Long,
        alreadyBonded: Boolean,
    ): AndroidSystemBondAction {
        if (active || terminal || endpointToken.isEmpty() || generation <= 0) {
            terminal = true
            return AndroidSystemBondAction.Failed(AndroidSystemBondFailure.START_REJECTED)
        }
        this.endpointToken = endpointToken
        this.generation = generation
        active = true
        return if (alreadyBonded) {
            finish(AndroidSystemBondAction.ProceedToGatt)
        } else {
            AndroidSystemBondAction.RequestSystemBond
        }
    }

    fun onBondState(
        endpointToken: String,
        generation: Long,
        state: AndroidSystemBondState,
    ): AndroidSystemBondAction {
        if (!active || terminal) return AndroidSystemBondAction.Failed(AndroidSystemBondFailure.CALLBACK_MISMATCH)
        if (this.endpointToken != endpointToken || this.generation != generation) {
            return finish(AndroidSystemBondAction.Failed(AndroidSystemBondFailure.CALLBACK_MISMATCH))
        }
        return when (state) {
            AndroidSystemBondState.BONDING -> {
                sawBonding = true
                AndroidSystemBondAction.Await
            }
            AndroidSystemBondState.BONDED -> if (sawBonding) {
                finish(AndroidSystemBondAction.ProceedToGatt)
            } else {
                finish(AndroidSystemBondAction.Failed(AndroidSystemBondFailure.CALLBACK_MISMATCH))
            }
            AndroidSystemBondState.NONE -> finish(
                AndroidSystemBondAction.Failed(AndroidSystemBondFailure.BOND_CANCELLED_OR_FAILED),
            )
        }
    }

    fun fail(reason: AndroidSystemBondFailure): AndroidSystemBondAction {
        if (!active || terminal) return AndroidSystemBondAction.Failed(reason)
        return finish(AndroidSystemBondAction.Failed(reason))
    }

    private fun finish(action: AndroidSystemBondAction): AndroidSystemBondAction {
        active = false
        terminal = true
        endpointToken = null
        generation = 0
        sawBonding = false
        return action
    }
}
