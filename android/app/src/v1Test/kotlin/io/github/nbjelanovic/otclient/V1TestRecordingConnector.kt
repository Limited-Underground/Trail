package io.github.nbjelanovic.otclient

/** Additional observation only: never consumes the controller's observer or stops service on close. */
internal class V1TestRecordingConnector(
    private val delegate: ConnectedDeviceServiceConnector,
    private val record: (V1TestPhoneConnectionState) -> Unit,
    /** Typed trace observation: exact service generation plus the published runtime state. */
    private val trace: (Long, TrailAppUiState.BluetoothDevice) -> Unit = { _, _ -> },
    /** The bound service connection was released; any open transaction must be closed. */
    private val traceReleased: () -> Unit = {},
) : ConnectedDeviceServiceConnector {
    private val registrations = mutableSetOf<RecordingBinding>()
    private var closed = false
    override fun startFromVisibleUserAction() = delegate.startFromVisibleUserAction()
    override fun stopService() {
        // Android stop closes its binding without a disconnected callback. Detach
        // our additional observation first, but leave this connector reusable.
        registrations.toList().forEach { runCatching { it.close() } }
        delegate.stopService()
    }
    override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? {
        if (closed) return null
        val registration = RecordingBinding(observer)
        registrations += registration
        val binding = try {
            delegate.bind(registration::onConnection)
        } catch (failure: Exception) {
            registration.close()
            throw failure
        }
        if (binding == null) { registration.close(); return null }
        registration.attach(binding)
        return registration
    }
    override fun close() {
        if (closed) return
        closed = true
        // One broken lease cannot prevent other leases or the delegate from closing.
        registrations.toList().forEach { runCatching { it.close() } }
        delegate.close()
    }

    private inner class RecordingBinding(
        private val observer: (ConnectedDeviceServiceConnection) -> Unit,
    ) : ConnectedDeviceServiceBinding {
        private var binding: ConnectedDeviceServiceBinding? = null
        private var observation: ConnectedDeviceStateObservation? = null
        private var active = true
        private var epoch: Any = Any()
        private var traced = false

        fun attach(value: ConnectedDeviceServiceBinding) {
            if (active) binding = value else value.close()
        }

        fun onConnection(connection: ConnectedDeviceServiceConnection) {
            if (!active) return
            val nextEpoch = Any()
            epoch = nextEpoch
            closeObservation()
            if (connection is ConnectedDeviceServiceConnection.Connected) {
                val generation = connection.port.generation
                val acquired = runCatching {
                    connection.port.observe { state ->
                        if (active && epoch === nextEpoch) {
                            // The typed trace runs first so a coarse-category failure cannot hide it.
                            runCatching { traced = true; trace(generation, state) }
                            runCatching {
                                val event = V1TestConnectionCategory.from(state.runtimeState)

                                record(event)
                            }
                        }
                    }
                }.getOrNull()
                // observe can call back synchronously; close/reconnect during that callback wins.
                if (active && epoch === nextEpoch) observation = acquired
                else runCatching { acquired?.close() }
            } else {
                releaseTrace()
            }
            if (active && epoch === nextEpoch) observer(connection)
        }

        private fun releaseTrace() {
            if (!traced) return
            traced = false
            runCatching { traceReleased() }
        }

        private fun closeObservation() {
            val previous = observation
            observation = null
            runCatching { previous?.close() }
        }

        override fun close() {
            if (!active) return
            active = false
            epoch = Any()
            registrations.remove(this)
            closeObservation()
            releaseTrace()
            val previous = binding
            binding = null
            previous?.close()
        }
    }
}
