package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionFragment
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodecError
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot

data class CompanionCandidate(
    val endpointToken: String,
    val publicLabel: String,
)

data class CompanionConnection(
    val endpointToken: String,
    val publicLabel: String,
    val status: String,
    val snapshot: CompanionStatusSnapshot,
)

sealed interface ConnectionAttempt {
    data class Connected(val connection: CompanionConnection) : ConnectionAttempt
    data class Failed(val publicReason: String) : ConnectionAttempt
}

sealed interface SemanticActionAttempt {
    data class Applied(
        val result: CompanionActionResult,
        val snapshot: CompanionStatusSnapshot,
        val sessionNonce: Long,
        val exchangeId: Long,
        val responseKind: CompanionFrameKind,
    ) : SemanticActionAttempt

    data class Failed(val publicReason: String) : SemanticActionAttempt
}

/**
 * Replaceable local companion boundary. OT-036 supplies only a deterministic
 * fake; a Bluetooth implementation requires a separately accepted security
 * and permission/lifecycle increment.
 */
interface CompanionTransport {
    fun candidates(): List<CompanionCandidate>
    fun connect(endpointToken: String): ConnectionAttempt
    fun disconnect(endpointToken: String)
    fun submitAction(endpointToken: String, request: CompanionActionRequest): SemanticActionAttempt
}

internal class FakeCompanionTransport(
    private val available: List<CompanionCandidate> = listOf(
        CompanionCandidate("fake-a", "Bench candidate A"),
        CompanionCandidate("fake-b", "Bench candidate B"),
    ),
    private val failingEndpoint: String? = null,
    initialSnapshots: Map<String, CompanionStatusSnapshot> = emptyMap(),
    initialSessionNonce: Long = 0,
    private val firstExchangeId: Long = 1,
) : CompanionTransport {
    private var connectedEndpoint: String? = null
    private var lastSessionNonce = initialSessionNonce
    private var activeSessionNonce = 0L
    private var nextExchangeId = firstExchangeId
    private val snapshots = available.associate { candidate ->
        candidate.endpointToken to (initialSnapshots[candidate.endpointToken] ?: CompanionStatusSnapshot(
            revision = 1,
            radio = CompanionRadioState.READY,
            gnss = CompanionGnssState.SEARCHING,
            power = CompanionPowerState.NORMAL,
            positionSharing = CompanionPositionSharingState.STOPPED,
            queuedActionCount = 0,
            pendingCriticalAlertId = if (candidate.endpointToken == "fake-a") 0x1001uL else 0x2001uL,
        ))
    }.toMutableMap()

    override fun candidates(): List<CompanionCandidate> = available.toList()

    override fun connect(endpointToken: String): ConnectionAttempt {
        if (connectedEndpoint != null) {
            return ConnectionAttempt.Failed("Disconnect the current test companion first.")
        }
        val candidate = available.singleOrNull { it.endpointToken == endpointToken }
            ?: return ConnectionAttempt.Failed("That test companion is no longer available.")
        if (endpointToken == failingEndpoint) {
            return ConnectionAttempt.Failed("The deterministic test connection was refused.")
        }
        if (lastSessionNonce !in 0 until 0xffff_ffffL || firstExchangeId !in 1..0xffff_ffffL) {
            return ConnectionAttempt.Failed("The deterministic test session counter is exhausted.")
        }
        val initialSnapshot = snapshots[endpointToken]
            ?: return ConnectionAttempt.Failed("The fake device state is unavailable.")
        if (CompanionSemanticCodec.encodeStatusSnapshot(initialSnapshot).value == null) {
            return ConnectionAttempt.Failed("The fake device state failed protocol validation.")
        }
        lastSessionNonce += 1
        activeSessionNonce = lastSessionNonce
        nextExchangeId = firstExchangeId
        connectedEndpoint = endpointToken
        return ConnectionAttempt.Connected(
            CompanionConnection(
                endpointToken = candidate.endpointToken,
                publicLabel = candidate.publicLabel,
                status = "Fake transport connected — no Bluetooth or radio evidence",
                snapshot = initialSnapshot,
            ),
        )
    }

    override fun disconnect(endpointToken: String) {
        if (connectedEndpoint == endpointToken) {
            connectedEndpoint = null
            activeSessionNonce = 0
            nextExchangeId = firstExchangeId
        }
    }

    override fun submitAction(endpointToken: String, request: CompanionActionRequest): SemanticActionAttempt {
        if (connectedEndpoint != endpointToken) {
            return SemanticActionAttempt.Failed("The fake companion session is not active.")
        }
        val encodedRequest = CompanionSemanticCodec.encodeActionRequest(request).value
            ?: return SemanticActionAttempt.Failed("The fake action was rejected before submission.")
        if (activeSessionNonce !in 1..0xffff_ffffL || nextExchangeId !in 1..0xffff_ffffL) {
            return SemanticActionAttempt.Failed("The deterministic test exchange counter is exhausted.")
        }
        val exchangeId = nextExchangeId
        nextExchangeId += 1
        val requestFragment = CompanionFragment(
            kind = CompanionFrameKind.ACTION_REQUEST,
            sessionNonce = activeSessionNonce,
            exchangeId = exchangeId,
            payload = encodedRequest,
        )
        val encodedRequestEnvelope = CompanionProtocolCodec.encodeFragment(requestFragment).value
            ?: return SemanticActionAttempt.Failed("The fake action failed protocol validation.")
        val decodedRequestFragment = CompanionProtocolCodec.decodeFragment(encodedRequestEnvelope).value
            ?: return SemanticActionAttempt.Failed("The fake action failed protocol validation.")
        if (CompanionSemanticCodec.validateSemanticFragment(decodedRequestFragment) != CompanionSemanticCodecError.NONE) {
            return SemanticActionAttempt.Failed("The fake action failed protocol validation.")
        }
        val admittedRequest = CompanionSemanticCodec.decodeActionRequest(decodedRequestFragment.payload).value
            ?: return SemanticActionAttempt.Failed("The fake action failed protocol validation.")
        val current = snapshots[endpointToken]
            ?: return SemanticActionAttempt.Failed("The fake device state is unavailable.")
        val (result, next) = applyDeterministicAction(current, admittedRequest)
        val encodedResult = CompanionSemanticCodec.encodeActionResult(result).value
            ?: return SemanticActionAttempt.Failed("The fake result failed protocol validation.")
        val resultFragment = CompanionFragment(
            kind = CompanionFrameKind.ACTION_RESULT,
            sessionNonce = activeSessionNonce,
            exchangeId = exchangeId,
            payload = encodedResult,
        )
        val encodedResultEnvelope = CompanionProtocolCodec.encodeFragment(resultFragment).value
            ?: return SemanticActionAttempt.Failed("The fake result failed protocol validation.")
        val decodedResultFragment = CompanionProtocolCodec.decodeFragment(encodedResultEnvelope).value
            ?: return SemanticActionAttempt.Failed("The fake result failed protocol validation.")
        if (
            decodedResultFragment.kind != CompanionFrameKind.ACTION_RESULT ||
            decodedResultFragment.sessionNonce != decodedRequestFragment.sessionNonce ||
            decodedResultFragment.exchangeId != decodedRequestFragment.exchangeId ||
            CompanionSemanticCodec.validateSemanticFragment(decodedResultFragment) != CompanionSemanticCodecError.NONE
        ) {
            return SemanticActionAttempt.Failed("The fake result failed protocol validation.")
        }
        val decodedResult = CompanionSemanticCodec.decodeActionResult(decodedResultFragment.payload).value
            ?: return SemanticActionAttempt.Failed("The fake result failed protocol validation.")
        val encodedNextSnapshot = CompanionSemanticCodec.encodeStatusSnapshot(next).value
            ?: return SemanticActionAttempt.Failed("The fake device state failed protocol validation.")
        val decodedNextSnapshot = CompanionSemanticCodec.decodeStatusSnapshot(encodedNextSnapshot).value
            ?: return SemanticActionAttempt.Failed("The fake device state failed protocol validation.")
        snapshots[endpointToken] = decodedNextSnapshot
        return SemanticActionAttempt.Applied(
            decodedResult,
            decodedNextSnapshot,
            decodedResultFragment.sessionNonce,
            decodedResultFragment.exchangeId,
            decodedResultFragment.kind,
        )
    }

    private fun applyDeterministicAction(
        current: CompanionStatusSnapshot,
        request: CompanionActionRequest,
    ): Pair<CompanionActionResult, CompanionStatusSnapshot> {
        if (current.revision >= 0xffff_ffffL) {
            return CompanionActionResult(
                kind = request.kind,
                quickStatus = request.quickStatus,
                criticalAlertId = request.criticalAlertId,
                disposition = CompanionActionDisposition.REJECTED,
                rejectReason = CompanionActionRejectReason.INTERNAL_FAILURE,
            ) to current
        }
        val nextRevision = current.revision + 1
        return when (request.kind) {
            CompanionActionKind.QUICK_STATUS -> {
                if (current.queuedActionCount == 0xffff) {
                    CompanionActionResult(
                        kind = request.kind,
                        quickStatus = request.quickStatus,
                        disposition = CompanionActionDisposition.REJECTED,
                        rejectReason = CompanionActionRejectReason.QUEUE_FULL,
                    ) to current
                } else {
                    CompanionActionResult(
                        kind = request.kind,
                        quickStatus = request.quickStatus,
                        disposition = CompanionActionDisposition.QUEUED,
                    ) to current.copy(revision = nextRevision, queuedActionCount = current.queuedActionCount + 1)
                }
            }
            CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT -> {
                if (current.pendingCriticalAlertId == 0uL || current.pendingCriticalAlertId != request.criticalAlertId) {
                    CompanionActionResult(
                        kind = request.kind,
                        criticalAlertId = request.criticalAlertId,
                        disposition = CompanionActionDisposition.REJECTED,
                        rejectReason = CompanionActionRejectReason.STALE_ALERT,
                    ) to current
                } else if (current.queuedActionCount == 0xffff) {
                    CompanionActionResult(
                        kind = request.kind,
                        criticalAlertId = request.criticalAlertId,
                        disposition = CompanionActionDisposition.REJECTED,
                        rejectReason = CompanionActionRejectReason.QUEUE_FULL,
                    ) to current
                } else {
                    CompanionActionResult(
                        kind = request.kind,
                        criticalAlertId = request.criticalAlertId,
                        disposition = CompanionActionDisposition.QUEUED,
                    ) to current.copy(
                        revision = nextRevision,
                        queuedActionCount = current.queuedActionCount + 1,
                        pendingCriticalAlertId = 0uL,
                    )
                }
            }
            CompanionActionKind.START_POSITION_SHARING -> CompanionActionResult(
                kind = request.kind,
                disposition = CompanionActionDisposition.ADMITTED,
            ) to current.copy(revision = nextRevision, positionSharing = CompanionPositionSharingState.WAITING_FOR_FIX)
            CompanionActionKind.STOP_POSITION_SHARING -> CompanionActionResult(
                kind = request.kind,
                disposition = CompanionActionDisposition.ADMITTED,
            ) to current.copy(revision = nextRevision, positionSharing = CompanionPositionSharingState.STOPPED)
        }
    }
}
