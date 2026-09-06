package io.github.nbjelanovic.otclient

/** Optional observation owned by one service session, independent of UI bind leases.
 * Calls are serialized on the service owner thread. Failure never grants or revokes authority.
 */
interface ConnectedDeviceSessionLifecycleObserver : AutoCloseable {
    fun onState(state: TrailAppUiState.BluetoothDevice)
    override fun close()
}

/** Implemented only by an application variant that supplies additional observation. */
interface ConnectedDeviceSessionObservationProvider {
    fun createObservation(generation: Long): ConnectedDeviceSessionLifecycleObserver?
}
