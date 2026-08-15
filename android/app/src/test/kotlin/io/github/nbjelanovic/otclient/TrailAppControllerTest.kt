package io.github.nbjelanovic.otclient

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import io.github.nbjelanovic.otprotocol.CompanionFragment
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class TrailAppControllerTest {
    @Test
    fun modeChoiceIsExplicitAndBluetoothFailureNeverFallsBackToLocalFixtures() {
        val harness = harness()
        assertEquals(TrailAppUiState.ChooseMode, harness.controller.state)

        harness.controller.chooseLocalTestMode()
        assertIs<TrailAppUiState.LocalTest>(harness.controller.state)
        harness.controller.chooseLocalDevice()
        harness.controller.connectLocalDevice("fake-a")
        assertIs<CompanionUiState.Connected>(assertIs<TrailAppUiState.LocalTest>(harness.controller.state).companionState)

        harness.facade.preflight = BlePreflight(BleRuntimeBlock.BLUETOOTH_DISABLED)
        harness.controller.chooseBluetoothDeviceMode()
        assertIs<CompanionUiState.Disconnected>(harness.local.state)
        harness.controller.scanBluetoothDevices()
        val blocked = assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state)
        assertEquals(BleRuntimeBlock.BLUETOOTH_DISABLED, assertIs<BleRuntimeState.Blocked>(blocked.runtimeState).reason)
        assertFalse(harness.controller.state is TrailAppUiState.LocalTest)
        harness.controller.close()
    }

    @Test
    fun permissionRequestUsesRecheckedAuthorityAndLateCallbacksCannotStartWork() {
        val harness = harness(permission = NearbyDevicesPermissionState.MISSING)
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        assertTrue(harness.facade.scans.isEmpty())

        assertTrue(harness.controller.beginNearbyDevicesPermissionRequest())
        assertFalse(harness.controller.beginNearbyDevicesPermissionRequest())
        harness.controller.onLifecycleStop()
        harness.permission.currentValue = NearbyDevicesPermissionState.GRANTED
        harness.controller.onNearbyDevicesPermissionResult()
        val stopped = assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state)
        assertEquals(NearbyDevicesPermissionState.GRANTED, stopped.permissionState)
        assertEquals(BleRuntimeState.Inactive, stopped.runtimeState)
        assertTrue(harness.facade.scans.isEmpty())

        harness.controller.onLifecycleStart()
        harness.permission.currentValue = NearbyDevicesPermissionState.MISSING
        harness.controller.refreshPermissionState()
        assertTrue(harness.controller.beginNearbyDevicesPermissionRequest())
        harness.controller.returnToModeChoice()
        harness.permission.currentValue = NearbyDevicesPermissionState.GRANTED
        harness.controller.onNearbyDevicesPermissionResult()
        assertEquals(TrailAppUiState.ChooseMode, harness.controller.state)
        assertTrue(harness.facade.scans.isEmpty())
        harness.controller.close()
    }

    @Test
    fun grantedPermissionEnablesOnlyAnExplicitScanAndLifecycleStopReleasesIt() {
        val harness = harness()
        harness.controller.chooseBluetoothDeviceMode()
        assertTrue(harness.facade.scans.isEmpty())
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.single()
        assertTrue(scan.started)
        assertIs<BleRuntimeState.Scanning>(assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState)

        harness.controller.onLifecycleStop()
        assertTrue(scan.closed)
        assertEquals(
            BleRuntimeState.Inactive,
            assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState,
        )
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertEquals(
            BleRuntimeState.Inactive,
            assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState,
        )
        harness.controller.onLifecycleStart()
        assertEquals(BleRuntimeState.Idle, assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState)
        harness.controller.close()
    }

    @Test
    fun switchingModesClosesScanningConnectingAndReadyLeasesBeforePublishingTheNewMode() {
        val scanning = harness()
        scanning.controller.chooseBluetoothDeviceMode()
        scanning.controller.scanBluetoothDevices()
        val scanLease = scanning.facade.scans.single()
        scanning.controller.chooseLocalTestMode()
        assertTrue(scanLease.closed)
        assertIs<TrailAppUiState.LocalTest>(scanning.controller.state)
        scanning.controller.close()

        val connecting = harness()
        val connectingLease = beginConnection(connecting)
        connecting.controller.returnToModeChoice()
        assertTrue(connectingLease.closed)
        assertEquals(TrailAppUiState.ChooseMode, connecting.controller.state)
        connecting.controller.close()

        val ready = harness()
        val readyLease = beginConnection(ready)
        makeReady(readyLease)
        assertIs<BleRuntimeState.Ready>(assertIs<TrailAppUiState.BluetoothDevice>(ready.controller.state).runtimeState)
        ready.controller.chooseLocalTestMode()
        assertTrue(readyLease.closed)
        assertIs<TrailAppUiState.LocalTest>(ready.controller.state)
        ready.controller.close()
    }

    @Test
    fun permissionRevocationClosesCurrentBluetoothWorkBeforeUiReportsMissing() {
        val harness = harness()
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.single()
        val observed = mutableListOf<TrailAppUiState>()
        harness.controller.observe(observed::add)

        harness.permission.currentValue = NearbyDevicesPermissionState.MISSING
        harness.controller.refreshPermissionState()

        assertTrue(scan.closed)
        val current = assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state)
        assertEquals(NearbyDevicesPermissionState.MISSING, current.permissionState)
        assertEquals(BleRuntimeState.Idle, current.runtimeState)
        assertTrue(
            observed.filterIsInstance<TrailAppUiState.BluetoothDevice>().none {
                it.runtimeState == BleRuntimeState.Idle && it.permissionState == NearbyDevicesPermissionState.GRANTED
            },
        )
        harness.controller.close()
    }

    @Test
    fun lifecycleStopAndCloseRequestedInsideObserverAreDeferredButNeverLost() {
        val stopped = harness()
        stopped.controller.chooseBluetoothDeviceMode()
        stopped.controller.scanBluetoothDevices()
        val stoppedScan = stopped.facade.scans.single()
        stopped.controller.observe { state ->
            if (state is TrailAppUiState.BluetoothDevice && state.runtimeState is BleRuntimeState.Scanning) {
                stopped.controller.onLifecycleStop()
            }
        }
        assertTrue(stoppedScan.closed)
        assertEquals(BleRuntimeState.Inactive, assertIs<TrailAppUiState.BluetoothDevice>(stopped.controller.state).runtimeState)
        assertEquals(0, stopped.facade.closeCount)
        stopped.controller.close()
        assertEquals(1, stopped.facade.closeCount)

        val destroyed = harness(startLifecycle = false)
        val owner = TestLifecycleOwner()
        val binding = TrailAppLifecycleBinding(owner.lifecycle, destroyed.controller)
        owner.registry.handleLifecycleEvent(Lifecycle.Event.ON_START)
        destroyed.controller.chooseBluetoothDeviceMode()
        destroyed.controller.scanBluetoothDevices()
        val destroyedScan = destroyed.facade.scans.single()
        destroyed.controller.observe { state ->
            if (state is TrailAppUiState.BluetoothDevice && state.runtimeState is BleRuntimeState.Scanning) {
                binding.close()
            }
        }
        assertTrue(destroyedScan.closed)
        assertEquals(BleRuntimeState.Closed, destroyed.runtime.state)
        assertEquals(1, destroyed.facade.closeCount)
        binding.close()
        assertEquals(1, destroyed.facade.closeCount)
        destroyedScan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertEquals(BleRuntimeState.Closed, destroyed.runtime.state)
    }

    @Test
    fun lifecycleStopReleasesConnectingAndReadyGattAndEveryOwnedTimer() {
        val connecting = harness()
        val connectingGatt = beginConnection(connecting)
        assertTrue(connecting.scheduler.leases.any { !it.closed })
        connecting.controller.onLifecycleStop()
        assertTrue(connectingGatt.closed)
        assertTrue(connecting.scheduler.leases.all { it.closed })
        assertEquals(BleRuntimeState.Inactive, connecting.runtime.state)
        connecting.controller.close()

        val ready = harness()
        val readyGatt = beginConnection(ready)
        makeReady(readyGatt)
        assertIs<BleRuntimeState.Ready>(ready.runtime.state)
        ready.controller.onLifecycleStop()
        assertTrue(readyGatt.closed)
        assertTrue(ready.scheduler.leases.all { it.closed })
        assertEquals(BleRuntimeState.Inactive, ready.runtime.state)
        ready.controller.close()
    }

    @Test
    fun deferredCloseWinsOverQueuedLifecycleChangesAndLatePermissionResultIsIgnored() {
        val harness = harness()
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.single()
        harness.controller.observe { state ->
            if (state is TrailAppUiState.BluetoothDevice && state.runtimeState is BleRuntimeState.Scanning) {
                harness.controller.onLifecycleStop()
                harness.controller.onLifecycleStart()
                harness.controller.close()
            }
        }
        assertTrue(scan.closed)
        assertEquals(BleRuntimeState.Closed, harness.runtime.state)
        assertEquals(1, harness.facade.closeCount)

        val permission = harness(permission = NearbyDevicesPermissionState.MISSING)
        permission.controller.chooseBluetoothDeviceMode()
        assertTrue(permission.controller.beginNearbyDevicesPermissionRequest())
        permission.controller.close()
        permission.permission.currentValue = NearbyDevicesPermissionState.GRANTED
        permission.controller.onNearbyDevicesPermissionResult()
        assertTrue(permission.facade.scans.isEmpty())
        assertEquals(BleRuntimeState.Closed, permission.runtime.state)
        assertEquals(1, permission.facade.closeCount)
    }

    @Test
    fun injectedPreexistingLeaseObserverReentryAndOffOwnerCallsFailClosed() {
        val facade = TestFacade()
        val scheduler = TestScheduler()
        val runtime = BleCompanionRuntime(facade, scheduler)
        runtime.onLifecycleStart()
        runtime.requestScan()
        val inheritedScan = facade.scans.single()
        val verifier = MutableThreadVerifier(true)
        val controller = TrailAppController(
            CompanionAppController(FakeCompanionTransport()),
            runtime,
            MutablePermissionReader(NearbyDevicesPermissionState.GRANTED),
            verifier,
            facade,
        )
        assertTrue(inheritedScan.closed)
        assertEquals(TrailAppUiState.ChooseMode, controller.state)

        controller.observe { state ->
            if (state is TrailAppUiState.LocalTest) controller.chooseBluetoothDeviceMode()
        }
        controller.chooseLocalTestMode()
        assertIs<TrailAppUiState.LocalTest>(controller.state)

        verifier.allowed = false
        assertFailsWith<IllegalStateException> { controller.returnToModeChoice() }
        assertIs<TrailAppUiState.LocalTest>(controller.state)
        verifier.allowed = true
        controller.close()
        assertEquals(1, facade.closeCount)
    }

    private fun beginConnection(harness: Harness): TestGattLease {
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.last()
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        harness.controller.selectBluetoothDevice(CANDIDATE.endpointToken)
        return harness.facade.connections.last()
    }

    private fun makeReady(gatt: TestGattLease) {
        gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
        gatt.emit(BleGattEvent.MtuChanged(151))
        gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes()))
        gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce = 7, eventId = 1)))
    }

    private fun harness(
        permission: NearbyDevicesPermissionState = NearbyDevicesPermissionState.GRANTED,
        startLifecycle: Boolean = true,
    ): Harness {
        val facade = TestFacade()
        val scheduler = TestScheduler()
        val runtime = BleCompanionRuntime(facade, scheduler)
        val local = CompanionAppController(FakeCompanionTransport())
        val permissionReader = MutablePermissionReader(permission)
        val controller = TrailAppController(local, runtime, permissionReader, bluetoothFacadeCloseable = facade)
        if (startLifecycle) controller.onLifecycleStart()
        return Harness(controller, local, runtime, facade, permissionReader, scheduler)
    }

    private data class Harness(
        val controller: TrailAppController,
        val local: CompanionAppController,
        val runtime: BleCompanionRuntime,
        val facade: TestFacade,
        val permission: MutablePermissionReader,
        val scheduler: TestScheduler,
    )

    private class MutablePermissionReader(var currentValue: NearbyDevicesPermissionState) :
        NearbyDevicesPermissionReader {
        override fun current(): NearbyDevicesPermissionState = currentValue
    }

    private class MutableThreadVerifier(var allowed: Boolean) : BleRuntimeThreadVerifier {
        override fun isOwnerThread(): Boolean = allowed
    }

    private class TestFacade : AndroidBluetoothFacade, AutoCloseable {
        var preflight = BlePreflight()
        val scans = mutableListOf<TestScanLease>()
        val connections = mutableListOf<TestGattLease>()
        var closeCount = 0

        override fun preflight(): BlePreflight = preflight
        override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease =
            TestScanLease(observer).also(scans::add)
        override fun createConnection(endpointToken: String, observer: (BleGattEvent) -> Unit): BleGattLease =
            TestGattLease(endpointToken, observer).also(connections::add)
        override fun close() {
            closeCount += 1
        }
    }

    private class TestScanLease(private val observer: (BleScanEvent) -> Unit) : BleScanLease {
        var started = false
        var closed = false
        override fun start(): Boolean = true.also { started = true }
        override fun close() { closed = true }
        fun emit(event: BleScanEvent) = observer(event)
    }

    private class TestGattLease(
        val endpointToken: String,
        private val observer: (BleGattEvent) -> Unit,
    ) : BleGattLease {
        var closed = false
        override fun start(): Boolean = true
        override fun requestMtu(mtu: Int): Boolean = true
        override fun readProtocolInfo(): Boolean = true
        override fun subscribeStreamIndications(): Boolean = true
        override fun writeCommandWithResponse(value: ByteArray): Boolean = true
        override fun close() { closed = true }
        fun emit(event: BleGattEvent) = observer(event)
    }

    private class TestScheduler : BleRuntimeScheduler {
        val leases = mutableListOf<TestTimerLease>()
        override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease =
            TestTimerLease().also(leases::add)
    }

    private class TestTimerLease : BleReconnectLease {
        var closed = false
        override fun close() { closed = true }
    }

    private class TestLifecycleOwner : LifecycleOwner {
        val registry = LifecycleRegistry.createUnsafe(this)
        override val lifecycle: Lifecycle get() = registry
    }

    companion object {
        private val CANDIDATE = BleDiscoveredCompanion("opaque-device", "Nearby compatible device 1")
        private val SECURE_LINK = BleSecurityEvidence(true, true, true)

        private fun protocolInfoBytes(): ByteArray = CompanionProtocolCodec.encodeProtocolInfo(
            CompanionProtocolInfo(capabilities = REQUIRED_ACTION_CAPABILITIES),
        ).value!!

        private fun snapshotEnvelope(sessionNonce: Long, eventId: Long): ByteArray = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.SNAPSHOT,
                sessionNonce = sessionNonce,
                exchangeId = eventId,
                payload = CompanionSemanticCodec.encodeStatusSnapshot(
                    CompanionStatusSnapshot(
                        revision = 1,
                        radio = CompanionRadioState.READY,
                        gnss = CompanionGnssState.SEARCHING,
                        power = CompanionPowerState.NORMAL,
                        positionSharing = CompanionPositionSharingState.STOPPED,
                        queuedActionCount = 0,
                    ),
                ).value!!,
            ),
        ).value!!
    }
}
