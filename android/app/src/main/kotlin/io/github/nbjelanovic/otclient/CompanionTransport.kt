package io.github.nbjelanovic.otclient

data class CompanionCandidate(
    val endpointToken: String,
    val publicLabel: String,
)

data class CompanionConnection(
    val endpointToken: String,
    val publicLabel: String,
    val status: String,
)

sealed interface ConnectionAttempt {
    data class Connected(val connection: CompanionConnection) : ConnectionAttempt
    data class Failed(val publicReason: String) : ConnectionAttempt
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
}

class FakeCompanionTransport(
    private val available: List<CompanionCandidate> = listOf(
        CompanionCandidate("fake-a", "Bench candidate A"),
        CompanionCandidate("fake-b", "Bench candidate B"),
    ),
    private val failingEndpoint: String? = null,
) : CompanionTransport {
    private var connectedEndpoint: String? = null

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
        connectedEndpoint = endpointToken
        return ConnectionAttempt.Connected(
            CompanionConnection(
                endpointToken = candidate.endpointToken,
                publicLabel = candidate.publicLabel,
                status = "Fake transport connected — no Bluetooth or radio evidence",
            ),
        )
    }

    override fun disconnect(endpointToken: String) {
        if (connectedEndpoint == endpointToken) connectedEndpoint = null
    }
}
