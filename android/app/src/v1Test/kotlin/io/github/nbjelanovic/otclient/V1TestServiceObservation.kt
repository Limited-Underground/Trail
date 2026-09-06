package io.github.nbjelanovic.otclient

/** One additive recorder for the actual service owner, independent of activity bindings. */
internal class V1TestServiceObservation(
    private val generation: Long,
    private val trace: (Long, TrailAppUiState.BluetoothDevice) -> Unit,
    private val record: (V1TestPhoneConnectionState) -> Unit,
    private val release: (Long) -> Unit,
) : ConnectedDeviceSessionLifecycleObserver {
    private var closed = false

    init { require(generation > 0) }

    override fun onState(state: TrailAppUiState.BluetoothDevice) {
        if (closed) return
        runCatching { trace(generation, state) }
        if (!closed) runCatching { record(V1TestConnectionCategory.from(state.runtimeState)) }
    }

    override fun close() {
        if (closed) return
        closed = true
        runCatching { release(generation) }
    }
}
