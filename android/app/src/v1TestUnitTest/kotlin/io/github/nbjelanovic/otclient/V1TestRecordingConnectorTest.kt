package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import kotlin.test.*

class V1TestRecordingConnectorTest {
    @Test fun observationAndRecorderFailuresNeverSuppressUiConnections() {
        val delegate = Connector()
        val received = mutableListOf<ConnectedDeviceServiceConnection>()
        val connector = V1TestRecordingConnector(delegate, record = { error("recorder failed") })
        assertNotNull(connector.bind(received::add))
        val broken = Port().apply { failObserve = true }
        val first = ConnectedDeviceServiceConnection.Connected(broken)
        delegate.emit(0, first)
        val healthy = Port()
        val second = ConnectedDeviceServiceConnection.Connected(healthy)
        delegate.emit(0, second)
        assertEquals(listOf<ConnectedDeviceServiceConnection>(first, second), received)
        assertEquals(1, healthy.observers.size)
    }

    @Test fun throwingObservationCloseDoesNotBlockDisconnectOrDelegateBindingClose() {
        val delegate = Connector()
        val received = mutableListOf<ConnectedDeviceServiceConnection>()
        val connector = V1TestRecordingConnector(delegate, record = {})
        val binding = assertNotNull(connector.bind(received::add))
        val port = Port().apply { failObservationClose = true }
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(port))
        delegate.emit(0, ConnectedDeviceServiceConnection.Disconnected)
        assertEquals(ConnectedDeviceServiceConnection.Disconnected, received.last())
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(port))
        binding.close()
        binding.close()
        assertEquals(2, port.observationCloses)
        assertEquals(1, delegate.bindings.single().closes)
        assertEquals(0, delegate.stops)
    }

    @Test fun multipleBindingsOwnIndependentObserversAndIgnoreStaleCallbacks() {
        val delegate = Connector()
        val records = mutableListOf<V1TestPhoneConnectionState>()
        val connector = V1TestRecordingConnector(delegate, record = records::add)
        val first = assertNotNull(connector.bind {})
        val second = assertNotNull(connector.bind {})
        val a = Port()
        val b = Port()
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(a))
        delegate.emit(1, ConnectedDeviceServiceConnection.Connected(b))
        first.close()
        assertEquals(1, a.observationCloses)
        assertEquals(0, b.observationCloses)
        val count = records.size
        a.observers.single()(a.state)
        assertEquals(count, records.size)
        b.observers.single()(b.state)
        assertEquals(count + 1, records.size)
        second.close()
        assertEquals(1, b.observationCloses)
    }

    @Test fun replacingPortInvalidatesPreviousStateCallbackEvenIfCloseThrows() {
        val delegate = Connector()
        val records = mutableListOf<V1TestPhoneConnectionState>()
        val connector = V1TestRecordingConnector(delegate, record = records::add)
        assertNotNull(connector.bind {})
        val a = Port().apply { failObservationClose = true }
        val b = Port()
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(a))
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(b))
        val count = records.size
        a.observers.single()(a.state)
        assertEquals(count, records.size)
    }

    @Test fun synchronousReentrantCloseReleasesLateAcquiredObservationAndBinding() {
        val delegate = Connector()
        val port = Port()
        delegate.initial = ConnectedDeviceServiceConnection.Connected(port)
        lateinit var connector: V1TestRecordingConnector
        connector = V1TestRecordingConnector(delegate, record = { connector.close() })
        var delivered = 0
        val binding = assertNotNull(connector.bind { delivered++ })
        assertEquals(0, delivered)
        assertEquals(1, port.observationCloses)
        assertEquals(1, delegate.bindings.single().closes)
        assertEquals(1, delegate.closes)
        binding.close()
        assertNull(connector.bind {})
    }

    @Test fun failedBindReleasesAnySynchronouslyAcquiredObserver() {
        val delegate = Connector()
        val port = Port()
        delegate.initial = ConnectedDeviceServiceConnection.Connected(port)
        delegate.returnNull = true
        val connector = V1TestRecordingConnector(delegate, record = {})
        assertNull(connector.bind {})
        assertEquals(1, port.observationCloses)
        connector.close()
        assertEquals(1, delegate.closes)
    }

    @Test fun closeAlwaysReachesDelegateDespiteThrowingBindingAndNeverStopsService() {
        val delegate = Connector()
        val connector = V1TestRecordingConnector(delegate, record = {})
        assertNull(connector.startFromVisibleUserAction())
        assertNotNull(connector.bind {})
        delegate.bindings.single().failClose = true
        connector.close()
        connector.close()
        assertEquals(1, delegate.starts)
        assertEquals(1, delegate.closes)
        assertEquals(0, delegate.stops)
    }

    @Test fun explicitStopDetachesDiagnosticsSuppressesLateEventsAndAllowsLaterBind() {
        val delegate = Connector()
        val records = mutableListOf<V1TestPhoneConnectionState>()
        val connector = V1TestRecordingConnector(delegate, record = records::add)
        val oldBinding = assertNotNull(connector.bind {})
        val oldPort = Port().apply { failObservationClose = true }
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(oldPort))
        val count = records.size
        connector.stopService()
        assertEquals(1, delegate.stops)
        assertEquals(1, oldPort.observationCloses)
        assertEquals(1, delegate.bindings.first().closes)
        oldPort.observers.single()(oldPort.state)
        assertEquals(count, records.size)
        oldBinding.close()
        assertNotNull(connector.bind {})
        val newPort = Port()
        delegate.emit(1, ConnectedDeviceServiceConnection.Connected(newPort))
        assertEquals(count + 1, records.size)
        connector.close()
        assertEquals(1, delegate.stops)
    }

    @Test fun theTypedTraceIsAdditiveAndNeverStealsTheProductionObserver() {
        val delegate = Connector()
        val ui = mutableListOf<ConnectedDeviceServiceConnection>()
        val traced = mutableListOf<Pair<Long, BleRuntimeState>>()
        var released = 0
        val connector = V1TestRecordingConnector(
            delegate,
            record = { error("coarse recorder failed") },
            trace = { generation, state -> traced += generation to state.runtimeState },
            traceReleased = { released++ },
        )
        assertNotNull(connector.bind(ui::add))
        val port = Port()
        val connection = ConnectedDeviceServiceConnection.Connected(port)
        delegate.emit(0, connection)
        // The production observer still receives the connection, and the exact service
        // generation plus the typed runtime state reach the trace.
        assertEquals(listOf<ConnectedDeviceServiceConnection>(connection), ui)
        assertEquals(listOf<Pair<Long, BleRuntimeState>>(port.generation to BleRuntimeState.Idle), traced)
        assertEquals(1, port.observers.size)
        // A failing coarse recorder cannot suppress the typed trace or the UI.
        port.observers.single()(port.state)
        assertEquals(2, traced.size)
        // Releasing the connection closes the trace transaction exactly once.
        delegate.emit(0, ConnectedDeviceServiceConnection.Disconnected)
        assertEquals(1, released)
        delegate.emit(0, ConnectedDeviceServiceConnection.Disconnected)
        assertEquals(1, released)
        // Nothing here stops the service or touches BLE ownership.
        connector.close()
        assertEquals(0, delegate.stops)
        assertEquals(0, port.scans)
        assertEquals(0, port.authorizations)
        assertEquals(0, port.disconnects)
        assertEquals(0, port.actions)
    }

    @Test fun aThrowingTraceObserverCannotBreakTheUiOrTheCoarseRecorder() {
        val delegate = Connector()
        val ui = mutableListOf<ConnectedDeviceServiceConnection>()
        val records = mutableListOf<V1TestPhoneConnectionState>()
        val connector = V1TestRecordingConnector(
            delegate,
            record = records::add,
            trace = { _, _ -> error("trace failed") },
            traceReleased = { error("release failed") },
        )
        val binding = assertNotNull(connector.bind(ui::add))
        val port = Port()
        delegate.emit(0, ConnectedDeviceServiceConnection.Connected(port))
        assertEquals(1, ui.size)
        assertEquals(1, records.size)
        binding.close()
        connector.close()
        assertEquals(0, delegate.stops)
        assertEquals(1, delegate.closes)
    }

    private class Connector : ConnectedDeviceServiceConnector {
        val observers = mutableListOf<(ConnectedDeviceServiceConnection) -> Unit>()
        val bindings = mutableListOf<Binding>()
        var initial: ConnectedDeviceServiceConnection? = null
        var returnNull = false
        var starts = 0
        var stops = 0
        var closes = 0
        override fun startFromVisibleUserAction(): ConnectedDeviceServiceStartFailure? { starts++; return null }
        override fun stopService() { stops++ }
        override fun close() { closes++ }
        override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? {
            observers += observer
            initial?.let(observer)
            if (returnNull) return null
            return Binding().also { bindings += it }
        }
        fun emit(index: Int, connection: ConnectedDeviceServiceConnection) = observers[index](connection)
    }

    private class Binding : ConnectedDeviceServiceBinding {
        var closes = 0
        var failClose = false
        override fun close() { closes++; check(!failClose) }
    }

    private class Port : ConnectedDeviceSessionPort {
        override val generation = 1L
        override val state = TrailAppUiState.BluetoothDevice(BleRuntimeState.Idle,
            NearbyDevicesPermissionState.GRANTED, false, false, DeviceAuthorizationUiState.None)
        val observers = mutableListOf<(TrailAppUiState.BluetoothDevice) -> Unit>()
        var observationCloses = 0
        var failObserve = false
        var failObservationClose = false
        var scans = 0
        var authorizations = 0
        var disconnects = 0
        var actions = 0
        override fun observe(observer: (TrailAppUiState.BluetoothDevice) -> Unit): ConnectedDeviceStateObservation? {
            check(!failObserve)
            observers += observer
            observer(state)
            return ConnectedDeviceStateObservation { observationCloses++; check(!failObservationClose) }
        }
        override fun refreshPermissionState() = Unit
        override fun scan() { scans++ }
        override fun authorize(endpointToken: String) { authorizations++ }
        override fun disconnect() { disconnects++ }
        override fun submitAction(request: CompanionActionRequest): Boolean { actions++; return false }
    }
}
