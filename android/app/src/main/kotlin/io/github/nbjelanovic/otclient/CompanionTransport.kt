package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
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
    val groupLocation: GroupLocationSnapshot,
)

sealed interface ConnectionAttempt {
    data class Connected(val connection: CompanionConnection) : ConnectionAttempt
    data class Failed(val publicReason: String) : ConnectionAttempt
}

sealed interface SemanticActionAttempt {
    data class Applied(
        val result: CompanionActionResult,
        val snapshot: CompanionStatusSnapshot,
        val groupLocation: GroupLocationSnapshot,
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

/** Production service placeholder: structurally incapable of providing local or fake candidates. */
internal object UnavailableProductionLocalTransport : CompanionTransport {
    override fun candidates(): List<CompanionCandidate> = emptyList()

    override fun connect(endpointToken: String): ConnectionAttempt =
        ConnectionAttempt.Failed("Local test transport is not available in the Bluetooth service.")

    override fun disconnect(endpointToken: String) = Unit

    override fun submitAction(endpointToken: String, request: CompanionActionRequest): SemanticActionAttempt =
        SemanticActionAttempt.Failed("Local test transport is not available in the Bluetooth service.")
}
