package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult

sealed interface CompanionUiState {
    data class Disconnected(val candidates: List<CompanionCandidate>) : CompanionUiState
    data class Selecting(val candidates: List<CompanionCandidate>) : CompanionUiState
    data class Connecting(val candidate: CompanionCandidate) : CompanionUiState
    data class Connected(
        val connection: CompanionConnection,
        val lastActionResult: CompanionActionResult? = null,
        val publicNotice: String? = null,
    ) : CompanionUiState
    data class Failed(
        val publicReason: String,
        val candidates: List<CompanionCandidate>,
    ) : CompanionUiState
}

class CompanionAppController(private val transport: CompanionTransport) {
    var state: CompanionUiState = CompanionUiState.Disconnected(transport.candidates())
        private set

    private var observer: ((CompanionUiState) -> Unit)? = null

    fun observe(observer: ((CompanionUiState) -> Unit)?) {
        this.observer = observer
        observer?.invoke(state)
    }

    fun chooseDevice() {
        if (state is CompanionUiState.Disconnected || state is CompanionUiState.Failed) {
            publish(CompanionUiState.Selecting(transport.candidates()))
        }
    }

    fun cancelSelection() {
        if (state is CompanionUiState.Selecting) {
            publish(CompanionUiState.Disconnected(transport.candidates()))
        }
    }

    fun connect(endpointToken: String) {
        val selection = state as? CompanionUiState.Selecting ?: return
        val candidate = selection.candidates.singleOrNull { it.endpointToken == endpointToken }
        if (candidate == null) {
            publish(CompanionUiState.Failed("That test companion is no longer available.", transport.candidates()))
            return
        }
        publish(CompanionUiState.Connecting(candidate))
        when (val result = transport.connect(endpointToken)) {
            is ConnectionAttempt.Connected -> publish(CompanionUiState.Connected(result.connection))
            is ConnectionAttempt.Failed -> publish(CompanionUiState.Failed(result.publicReason, transport.candidates()))
        }
    }

    fun disconnect() {
        val connection = (state as? CompanionUiState.Connected)?.connection ?: return
        transport.disconnect(connection.endpointToken)
        publish(CompanionUiState.Disconnected(transport.candidates()))
    }

    fun submitAction(request: CompanionActionRequest) {
        val connected = state as? CompanionUiState.Connected ?: return
        when (val result = transport.submitAction(connected.connection.endpointToken, request)) {
            is SemanticActionAttempt.Applied -> publish(
                connected.copy(
                    connection = connected.connection.copy(
                        snapshot = result.snapshot,
                        groupLocation = result.groupLocation,
                    ),
                    lastActionResult = result.result,
                    publicNotice = null,
                ),
            )
            is SemanticActionAttempt.Failed -> publish(connected.copy(publicNotice = result.publicReason))
        }
    }

    fun retrySelection() = chooseDevice()

    private fun publish(next: CompanionUiState) {
        state = next
        observer?.invoke(next)
    }
}
