package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ConnectedDeviceServiceTest {
    @Test
    fun policyRequiresVisibleActionAndNearbyButNotificationDenialDoesNotBlock() {
        assertEquals(
            ConnectedDeviceServiceStartFailure.NOT_VISIBLE_USER_ACTION,
            ConnectedDeviceForegroundServicePolicy.admit(
                visibleUserAction = false,
                nearbyPermissionGranted = true,
                notificationPermissionState = NotificationPermissionState.GRANTED,
            ).failure,
        )
        assertEquals(
            ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING,
            ConnectedDeviceForegroundServicePolicy.admit(
                visibleUserAction = true,
                nearbyPermissionGranted = false,
                notificationPermissionState = NotificationPermissionState.DENIED,
            ).failure,
        )
        val admitted = ConnectedDeviceForegroundServicePolicy.admit(
            visibleUserAction = true,
            nearbyPermissionGranted = true,
            notificationPermissionState = NotificationPermissionState.DENIED,
        )
        assertTrue(admitted.allowed)
        assertTrue(admitted.reducedNotificationVisibility)
        assertNull(admitted.failure)
    }

    @Test
    fun lifecycleAndModeChangesNeverStartServiceWithoutExplicitStartAction() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.refreshPermissionState()
        fixture.controller.onLifecycleStop()
        fixture.controller.onLifecycleStart()
        assertEquals(0, fixture.connector.startCount)
        assertEquals(0, fixture.connector.bindCount)

        fixture.controller.startBluetoothService()
        assertEquals(1, fixture.connector.startCount)
        assertEquals(1, fixture.connector.bindCount)
    }

    @Test
    fun nearbyPermissionIsRereadAtTheVisibleStartBoundary() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.permissions.state = NearbyDevicesPermissionState.MISSING

        fixture.controller.startBluetoothService()

        assertEquals(0, fixture.connector.startCount)
        val state = assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state)
        assertEquals(ConnectedDeviceServiceUiState.START_FAILED, state.serviceState)
        assertEquals(ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING, state.serviceFailure)
    }

    @Test
    fun deniedNotificationPermissionStillStartsAndIsReportedAsReducedVisibility() {
        val fixture = fixture(notificationState = NotificationPermissionState.DENIED)
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(FakePort(7)))

        val state = assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state)
        assertEquals(ConnectedDeviceServiceUiState.RUNNING, state.serviceState)
        assertEquals(NotificationPermissionState.DENIED, state.notificationPermissionState)
        assertEquals(1, fixture.connector.startCount)
    }

    @Test
    fun synchronousBindFailureClosesReturnedLeaseAndStopsStartedService() {
        val fixture = fixture()
        fixture.connector.synchronousConnection = ConnectedDeviceServiceConnection.Disconnected
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()

        fixture.controller.startBluetoothService()

        assertEquals(1, fixture.connector.stopCount)
        assertEquals(1, fixture.connector.bindings.single().closeCount)
        val state = assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state)
        assertEquals(ConnectedDeviceServiceUiState.START_FAILED, state.serviceState)
    }

    @Test
    fun staleOldBindingCallbackCannotReplaceReboundPort() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        val first = FakePort(11)
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(first))

        fixture.controller.onLifecycleStop()
        fixture.controller.onLifecycleStart()
        val second = FakePort(12)
        fixture.connector.emit(1, ConnectedDeviceServiceConnection.Connected(second))
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Disconnected)
        second.emit(second.state.copy(runtimeState = BleRuntimeState.Scanning(emptyList())))

        assertIs<BleRuntimeState.Scanning>(assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).runtimeState)
        assertEquals(0, fixture.connector.stopCount)
        assertEquals(1, first.observationCloseCount)
    }

    @Test
    fun activityCloseUnbindsOnlyWhileExplicitModeExitStopsService() {
        val closed = fixture()
        closed.controller.onLifecycleStart()
        closed.controller.chooseBluetoothDeviceMode()
        closed.controller.startBluetoothService()
        closed.connector.emit(0, ConnectedDeviceServiceConnection.Connected(FakePort(20)))
        closed.controller.close()
        assertEquals(0, closed.connector.stopCount)
        assertEquals(1, closed.connector.closeCount)
        assertEquals(1, closed.connector.bindings.single().closeCount)

        val stopped = fixture()
        stopped.controller.onLifecycleStart()
        stopped.controller.chooseBluetoothDeviceMode()
        stopped.controller.startBluetoothService()
        stopped.connector.emit(0, ConnectedDeviceServiceConnection.Connected(FakePort(21)))
        stopped.controller.returnToModeChoice()
        assertEquals(1, stopped.connector.stopCount)
        assertEquals(TrailAppUiState.ChooseMode, stopped.controller.state)
    }

    @Test
    fun activityLifecycleStopCancelsServiceOwnedFactoryResetConfirmationBeforeUnbinding() {
        val fixture = fixture()
        val port = FakePort(22).also {
            it.state = it.state.copy(factoryResetConfirmationVisible = true)
        }
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(port))
        assertTrue(assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).factoryResetConfirmationVisible)

        fixture.controller.onLifecycleStop()

        assertEquals(1, port.cancelFactoryResetConfirmationCount)
        assertEquals(1, port.observationCloseCount)
        assertFalse(assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).factoryResetConfirmationVisible)
        fixture.controller.close()
    }

    @Test
    fun newActivityOwnerRestoresNoServiceOrClaimAndDoesNotAutoBind() {
        val connector = FakeConnector()
        val first = fixture(connector = connector)
        first.controller.onLifecycleStart()
        first.controller.chooseBluetoothDeviceMode()
        first.controller.startBluetoothService()
        assertEquals(1, connector.startCount)

        val recreated = fixture(connector = connector)
        recreated.controller.onLifecycleStart()

        assertEquals(TrailAppUiState.ChooseMode, recreated.controller.state)
        assertEquals(1, connector.startCount)
        assertEquals(1, connector.bindCount)

        recreated.controller.chooseLocalTestMode()
        assertEquals(1, connector.stopCount)
    }

    @Test
    fun permissionRevocationStopsOnceAndLatePortStateCannotReviveService() {
        val fixture = fixture()
        val port = FakePort(41)
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(port))

        fixture.permissions.state = NearbyDevicesPermissionState.MISSING
        fixture.controller.refreshPermissionState()
        port.emit(port.state.copy(runtimeState = BleRuntimeState.Scanning(emptyList())))

        assertEquals(1, fixture.connector.stopCount)
        assertEquals(1, port.observationCloseCount)
        val state = assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state)
        assertEquals(NearbyDevicesPermissionState.MISSING, state.permissionState)
        assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, state.serviceState)
    }

    @Test
    fun generationZeroBindingPromotesOnlyAfterOwnerGenerationAppears() {
        val fixture = fixture()
        val port = FakePort(0)
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(port))
        assertEquals(
            ConnectedDeviceServiceUiState.STARTING,
            assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).serviceState,
        )

        port.generation = 51
        port.emit(port.state.copy(runtimeState = BleRuntimeState.Idle))
        assertEquals(
            ConnectedDeviceServiceUiState.RUNNING,
            assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).serviceState,
        )
        assertEquals(1, fixture.startupScheduler.leases.single().closeCount)
    }

    @Test
    fun generationZeroStartupDeadlineStopsOrphanedServiceAndPublishesFailure() {
        val fixture = fixture()
        val port = FakePort(0)
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(port))

        fixture.startupScheduler.fire(0)

        assertEquals(1, fixture.connector.stopCount)
        assertEquals(1, fixture.connector.bindings.single().closeCount)
        assertEquals(
            ConnectedDeviceServiceUiState.START_FAILED,
            assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).serviceState,
        )
    }

    @Test
    fun staleStartupDeadlineAfterRebindCannotStopCurrentService() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(FakePort(0)))
        fixture.controller.onLifecycleStop()
        fixture.controller.onLifecycleStart()
        val current = FakePort(71)
        fixture.connector.emit(1, ConnectedDeviceServiceConnection.Connected(current))

        fixture.startupScheduler.fire(0)

        assertEquals(0, fixture.connector.stopCount)
        assertEquals(
            ConnectedDeviceServiceUiState.RUNNING,
            assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).serviceState,
        )
    }

    @Test
    fun lifecycleRebindDoesNotRetryAuthorizationClaim() {
        val fixture = fixture()
        val port = FakePort(61)
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.startBluetoothService()
        fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(port))
        fixture.controller.selectBluetoothDevice("opaque-one")
        assertEquals(1, port.authorizeCount)

        fixture.controller.onLifecycleStop()
        fixture.controller.onLifecycleStart()
        fixture.connector.emit(1, ConnectedDeviceServiceConnection.Connected(port))

        assertEquals(1, port.authorizeCount)
        assertEquals(1, fixture.connector.startCount)
    }

    @Test
    fun observerRequestedModeExitDuringStartingPreventsServiceLaunch() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.observe { state ->
            if (
                state is TrailAppUiState.BluetoothDevice &&
                state.serviceState == ConnectedDeviceServiceUiState.STARTING
            ) {
                fixture.controller.returnToModeChoice()
            }
        }

        fixture.controller.startBluetoothService()

        assertEquals(0, fixture.connector.startCount)
        assertEquals(1, fixture.connector.stopCount)
        assertEquals(TrailAppUiState.ChooseMode, fixture.controller.state)
    }

    @Test
    fun observerLifecycleStopDuringStartingPreventsBackgroundServiceLaunch() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.observe { state ->
            if (
                state is TrailAppUiState.BluetoothDevice &&
                state.serviceState == ConnectedDeviceServiceUiState.STARTING
            ) {
                fixture.controller.onLifecycleStop()
            }
        }

        fixture.controller.startBluetoothService()

        assertEquals(0, fixture.connector.startCount)
        assertEquals(0, fixture.connector.bindCount)
        assertEquals(
            ConnectedDeviceServiceUiState.START_REQUIRED,
            assertIs<TrailAppUiState.BluetoothDevice>(fixture.controller.state).serviceState,
        )
    }

    @Test
    fun observerCloseWinsOverQueuedLifecycleAndModeChangesWithoutStartingService() {
        val fixture = fixture()
        fixture.controller.onLifecycleStart()
        fixture.controller.chooseBluetoothDeviceMode()
        fixture.controller.observe { state ->
            if (
                state is TrailAppUiState.BluetoothDevice &&
                state.serviceState == ConnectedDeviceServiceUiState.STARTING
            ) {
                fixture.controller.onLifecycleStop()
                fixture.controller.returnToModeChoice()
                fixture.controller.close()
            }
        }

        fixture.controller.startBluetoothService()

        assertEquals(0, fixture.connector.startCount)
        assertEquals(1, fixture.connector.closeCount)
        assertEquals(0, fixture.connector.stopCount)
    }

    @Test
    fun synchronousPortPublicationCannotRetainObservationAfterModeStopOrClose() {
        listOf("mode", "stop", "close").forEachIndexed { index, action ->
            val fixture = fixture()
            val port = FakePort(80L + index)
            fixture.controller.onLifecycleStart()
            fixture.controller.chooseBluetoothDeviceMode()
            fixture.controller.startBluetoothService()
            var acted = false
            fixture.controller.observe { state ->
                if (
                    !acted && state is TrailAppUiState.BluetoothDevice &&
                    state.serviceState == ConnectedDeviceServiceUiState.RUNNING
                ) {
                    acted = true
                    when (action) {
                        "mode" -> fixture.controller.returnToModeChoice()
                        "stop" -> fixture.controller.onLifecycleStop()
                        else -> fixture.controller.close()
                    }
                }
            }

            fixture.connector.emit(0, ConnectedDeviceServiceConnection.Connected(port))
            port.emit(port.state.copy(runtimeState = BleRuntimeState.Scanning(emptyList())))

            assertEquals(1, port.observationCloseCount, action)
            assertEquals(1, fixture.connector.bindings.single().closeCount, action)
        }
    }

    @Test
    fun notificationTextIsFixedAndContainsNoDynamicIdentityFields() {
        assertEquals("Limited Underground Trail", CONNECTED_DEVICE_NOTIFICATION_TITLE)
        assertEquals("Bluetooth device connection service is running", CONNECTED_DEVICE_NOTIFICATION_TEXT)
        val combined = "$CONNECTED_DEVICE_NOTIFICATION_TITLE $CONNECTED_DEVICE_NOTIFICATION_TEXT".lowercase()
        listOf("address", "token", "correlation", "message", "candidate").forEach {
            assertFalse(combined.contains(it))
        }
    }

    @Test
    fun serviceOwnerClosesControllerExactlyOnceAndRejectsCommandsAfterClose() {
        val controller = FakeServiceController()
        val owner = ConnectedDeviceSessionOwner(31, controller)
        assertEquals(1, controller.chooseBluetoothCount)
        assertEquals(1, controller.lifecycleStartCount)

        owner.scan()
        owner.close()
        owner.close()
        owner.scan()

        assertEquals(1, controller.scanCount)
        assertEquals(1, controller.closeCount)
        assertIs<BleRuntimeState.Closed>(owner.state.runtimeState)
    }

    @Test
    fun serviceOwnerContainsThrowingObserverAndLeaseCloseCannotRemoveAnotherObserver() {
        val controller = FakeServiceController()
        val owner = ConnectedDeviceSessionOwner(32, controller)
        assertNull(owner.observe { error("hostile immediate observer") })
        var firstCount = 0
        var secondCount = 0
        val first = owner.observe { firstCount += 1 }!!
        owner.observe { secondCount += 1 }!!

        first.close()
        controller.emitBluetooth(BleRuntimeState.Scanning(emptyList()))

        assertEquals(1, firstCount)
        assertEquals(2, secondCount)
        assertIs<BleRuntimeState.Scanning>(owner.state.runtimeState)
    }

    @Test
    fun laterThrowingObserverIsDetachedWithoutSilencingHealthyObserver() {
        val controller = FakeServiceController()
        val owner = ConnectedDeviceSessionOwner(33, controller)
        var hostileCount = 0
        var healthyCount = 0
        owner.observe {
            hostileCount += 1
            if (hostileCount > 1) error("hostile update observer")
        }
        owner.observe { healthyCount += 1 }

        controller.emitBluetooth(BleRuntimeState.Scanning(emptyList()))
        controller.emitBluetooth(BleRuntimeState.Idle)

        assertEquals(2, hostileCount)
        assertEquals(3, healthyCount)
        assertIs<BleRuntimeState.Idle>(owner.state.runtimeState)
    }

    private fun fixture(
        notificationState: NotificationPermissionState = NotificationPermissionState.GRANTED,
        connector: FakeConnector = FakeConnector(),
    ): Fixture {
        val permissions = MutablePermissionReader(NearbyDevicesPermissionState.GRANTED)
        val scheduler = FakeStartupScheduler()
        return Fixture(
            controller = TrailActivityController(
                localController = CompanionAppController(FakeCompanionTransport()),
                permissionReader = permissions,
                notificationPermissionReader = NotificationPermissionReader { notificationState },
                serviceConnector = connector,
                serviceStartupScheduler = scheduler,
            ),
            permissions = permissions,
            connector = connector,
            startupScheduler = scheduler,
        )
    }

    private data class Fixture(
        val controller: TrailActivityController,
        val permissions: MutablePermissionReader,
        val connector: FakeConnector,
        val startupScheduler: FakeStartupScheduler,
    )

    private class MutablePermissionReader(var state: NearbyDevicesPermissionState) : NearbyDevicesPermissionReader {
        override fun current(): NearbyDevicesPermissionState = state
    }

    private class FakeConnector : ConnectedDeviceServiceConnector {
        var startCount = 0
        var bindCount = 0
        var stopCount = 0
        var closeCount = 0
        var startFailure: ConnectedDeviceServiceStartFailure? = null
        var synchronousConnection: ConnectedDeviceServiceConnection? = null
        val callbacks = mutableListOf<(ConnectedDeviceServiceConnection) -> Unit>()
        val bindings = mutableListOf<FakeBinding>()

        override fun startFromVisibleUserAction(): ConnectedDeviceServiceStartFailure? {
            startCount += 1
            return startFailure
        }

        override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding {
            bindCount += 1
            callbacks += observer
            synchronousConnection?.let(observer)
            return FakeBinding().also(bindings::add)
        }

        override fun stopService() {
            stopCount += 1
        }

        override fun close() {
            closeCount += 1
        }

        fun emit(index: Int, event: ConnectedDeviceServiceConnection) = callbacks[index](event)
    }

    private class FakeBinding : ConnectedDeviceServiceBinding {
        var closeCount = 0
        override fun close() {
            closeCount += 1
        }
    }

    private class FakeStartupScheduler : BleRuntimeScheduler {
        val leases = mutableListOf<FakeDeadline>()

        override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease {
            assertEquals(10_000L, delayMillis)
            return FakeDeadline(callback).also(leases::add)
        }

        fun fire(index: Int) = leases[index].callback()
    }

    private class FakeDeadline(val callback: () -> Unit) : BleReconnectLease {
        var closeCount = 0
        override fun close() {
            closeCount += 1
        }
    }

    private class FakePort(override var generation: Long) : ConnectedDeviceSessionPort {
        override var state = TrailAppUiState.BluetoothDevice(
            runtimeState = BleRuntimeState.Idle,
            permissionState = NearbyDevicesPermissionState.GRANTED,
            permissionRequestInFlight = false,
            permissionWasDenied = false,
            authorizationState = DeviceAuthorizationUiState.None,
            serviceState = ConnectedDeviceServiceUiState.RUNNING,
        )
        var observationCloseCount = 0
        var authorizeCount = 0
        var cancelFactoryResetConfirmationCount = 0
        private var observer: ((TrailAppUiState.BluetoothDevice) -> Unit)? = null

        override fun observe(
            observer: (TrailAppUiState.BluetoothDevice) -> Unit,
        ): ConnectedDeviceStateObservation {
            this.observer = observer
            observer(state)
            return ConnectedDeviceStateObservation {
                if (this.observer === observer) this.observer = null
                observationCloseCount += 1
            }
        }

        fun emit(next: TrailAppUiState.BluetoothDevice) {
            state = next
            observer?.invoke(next)
        }

        override fun refreshPermissionState() = Unit
        override fun scan() = Unit
        override fun authorize(endpointToken: String) {
            authorizeCount += 1
        }
        override fun disconnect() = Unit
        override fun submitAction(request: CompanionActionRequest): Boolean = true
        override fun cancelFactoryResetConfirmation() {
            cancelFactoryResetConfirmationCount += 1
            emit(state.copy(factoryResetConfirmationVisible = false))
        }
    }

    private class FakeServiceController : TrailServiceController {
        override var state: TrailAppUiState = TrailAppUiState.ChooseMode
        var observer: ((TrailAppUiState) -> Unit)? = null
        var chooseBluetoothCount = 0
        var lifecycleStartCount = 0
        var scanCount = 0
        var closeCount = 0

        override fun observe(observer: ((TrailAppUiState) -> Unit)?) {
            this.observer = observer
            observer?.invoke(state)
        }

        override fun chooseBluetoothDeviceMode() {
            chooseBluetoothCount += 1
            emitBluetooth(BleRuntimeState.Idle)
        }

        override fun onLifecycleStart() {
            lifecycleStartCount += 1
        }

        override fun scanBluetoothDevices() {
            scanCount += 1
        }

        fun emitBluetooth(runtimeState: BleRuntimeState) {
            state = TrailAppUiState.BluetoothDevice(
                runtimeState = runtimeState,
                permissionState = NearbyDevicesPermissionState.GRANTED,
                permissionRequestInFlight = false,
                permissionWasDenied = false,
                authorizationState = DeviceAuthorizationUiState.None,
                serviceState = ConnectedDeviceServiceUiState.RUNNING,
            )
            observer?.invoke(state)
        }

        override fun close() {
            closeCount += 1
        }

        override fun onLifecycleStop() = Unit
        override fun chooseLocalTestMode() = Unit
        override fun returnToModeChoice() = Unit
        override fun chooseLocalDevice() = Unit
        override fun cancelLocalSelection() = Unit
        override fun connectLocalDevice(endpointToken: String) = Unit
        override fun disconnectLocalDevice() = Unit
        override fun retryLocalSelection() = Unit
        override fun submitLocalAction(request: CompanionActionRequest) = Unit
        override fun beginNearbyDevicesPermissionRequest(): Boolean = false
        override fun onNearbyDevicesPermissionResult() = Unit
        override fun refreshPermissionState() = Unit
        override fun startBluetoothService() = Unit
        override fun selectBluetoothDevice(endpointToken: String) = Unit
        override fun disconnectBluetoothDevice() = Unit
        override fun submitBluetoothAction(request: CompanionActionRequest): Boolean = false
    }
}
