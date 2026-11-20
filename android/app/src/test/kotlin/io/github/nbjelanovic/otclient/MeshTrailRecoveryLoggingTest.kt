package io.github.nbjelanovic.otclient

import java.io.File
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class MeshTrailRecoveryLoggingTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun adapterOnReportsOnlyItsOwnTriggerAndIgnoresRetiredCallbacks() {
        val log = RecoveryLog(temporary.newFolder())
        val facade = PowerFacade()
        BleCompanionRuntime(facade, scheduler, bluetoothRecoveryObserver = log.observer::observeBluetoothRecovery).use { runtime ->
            runtime.onLifecycleStart()
            assertIs<BleRuntimeState.Blocked>(runtime.state)
            val retired = facade.watches.single()
            runtime.onLifecycleStop()
            assertTrue(retired.closed)
            runtime.onLifecycleStart()
            assertIs<BleRuntimeState.Blocked>(runtime.state)
            val current = facade.watches.last()
            facade.enabled = true

            retired.callback()
            assertIs<BleRuntimeState.Blocked>(runtime.state)
            assertTrue(log.recoveryRows().isEmpty())

            current.callback()
            assertEquals(BleRuntimeState.Idle, runtime.state)
            assertEquals(listOf("Bluetooth recovery: BLUETOOTH_TURNED_ON"), log.recoveryRows())
        }
    }

    @Test fun foregroundRecheckReportsItsOwnTriggerWithoutReopeningAnIdleRuntime() {
        val log = RecoveryLog(temporary.newFolder())
        val facade = PowerFacade()
        BleCompanionRuntime(facade, scheduler, bluetoothRecoveryObserver = log.observer::observeBluetoothRecovery).use { runtime ->
            runtime.onLifecycleStart()
            assertIs<BleRuntimeState.Blocked>(runtime.state)
            facade.enabled = true

            runtime.refreshBluetoothPreflight()
            assertEquals(BleRuntimeState.Idle, runtime.state)
            runtime.refreshBluetoothPreflight()
            assertEquals(listOf("Bluetooth recovery: FOREGROUND_RECHECK"), log.recoveryRows())
        }
    }

    @Test fun manualRetryRecordsTheLiveOwnerRefusalAndDoesNotLogAnAcceptedRetry() {
        val log = RecoveryLog(temporary.newFolder())
        val facade = PowerFacade()
        val runtime = BleCompanionRuntime(facade, scheduler)
        val ownerController = TrailAppController(
            localController = CompanionAppController(FakeCompanionTransport()),
            bluetoothRuntime = runtime,
            permissionReader = grantedPermission,
            authorizationClient = RuntimeDeviceAuthorizationClaimClient(runtime),
            authorizationScheduler = scheduler,
        )
        ConnectedDeviceSessionOwner(41L, ownerController).use { owner ->
            activity(log, owner).use { activity ->
                activity.onLifecycleStart()
                activity.chooseBluetoothDeviceMode()
                activity.startBluetoothService()

                assertEquals(BluetoothRecoveryRefusal.BLUETOOTH_OFF, activity.retryBluetoothDiscovery())
                assertEquals(listOf("Bluetooth retry refused: BLUETOOTH_OFF"), log.recoveryRows())

                facade.enabled = true
                assertNull(activity.retryBluetoothDiscovery())
                assertEquals(BleRuntimeState.Idle, runtime.state)
                assertEquals(listOf("Bluetooth retry refused: BLUETOOTH_OFF"), log.recoveryRows())
            }
        }
    }

    @Test fun retryWithNoBoundOwnerRecordsTheActivityRefusal() {
        val log = RecoveryLog(temporary.newFolder())
        activity(log, owner = null).use { activity ->
            activity.onLifecycleStart()
            activity.chooseBluetoothDeviceMode()
            assertEquals(BluetoothRecoveryRefusal.OWNER_UNAVAILABLE, activity.retryBluetoothDiscovery())
            assertEquals(listOf("Bluetooth retry refused: OWNER_UNAVAILABLE"), log.recoveryRows())
        }
    }

    @Test fun everyTypedRetryRefusalIsRecordedWithItsExactReason() {
        val log = RecoveryLog(temporary.newFolder())
        BluetoothRecoveryRefusal.entries.forEach(log.observer::observeBluetoothRetryRefusal)
        assertEquals(
            BluetoothRecoveryRefusal.entries.map { "Bluetooth retry refused: ${it.name}" },
            log.recoveryRows(),
        )
    }

    private fun activity(log: RecoveryLog, owner: ConnectedDeviceSessionOwner?) = TrailActivityController(
        localController = CompanionAppController(FakeCompanionTransport()),
        permissionReader = grantedPermission,
        notificationPermissionReader = NotificationPermissionReader { NotificationPermissionState.GRANTED },
        serviceConnector = Connector(owner),
        serviceStartupScheduler = scheduler,
        bluetoothRetryRefusalObserver = log.observer::observeBluetoothRetryRefusal,
    )

    private class RecoveryLog(directory: File) {
        private val log = MeshTrailOperationLog(directory)
        val observer = MeshTrailLogTransitions { kind, value -> check(log.append(1, kind, value)) }

        fun recoveryRows() = log.exportText().lineSequence()
            .map { it.removePrefix("+1s  ") }
            .filter { it.startsWith("Bluetooth recovery: ") || it.startsWith("Bluetooth retry refused: ") }
            .toList()
    }

    private class PowerFacade : AndroidBluetoothFacade {
        class Watch(val callback: () -> Unit) : BleReconnectLease {
            var closed = false
            override fun close() { closed = true }
        }

        var enabled = false
        val watches = mutableListOf<Watch>()

        override fun watchBluetoothEnabled(callback: () -> Unit): BleReconnectLease =
            Watch(callback).also(watches::add)

        override fun preflight() =
            BlePreflight(if (enabled) null else BleRuntimeBlock.BLUETOOTH_DISABLED)

        override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease? =
            error("Recovery must not start explicit add-device discovery.")

        override fun createConnection(
            endpointToken: String,
            purpose: BleConnectionPurpose,
            observer: (BleGattEvent) -> Unit,
        ): BleGattLease? = error("Recovery must not bypass discovery and authorization.")
    }

    private class Connector(private val owner: ConnectedDeviceSessionOwner?) : ConnectedDeviceServiceConnector {
        override fun startFromVisibleUserAction(): ConnectedDeviceServiceStartFailure? = null

        override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? {
            val current = owner ?: return null
            observer(ConnectedDeviceServiceConnection.Connected(current))
            return object : ConnectedDeviceServiceBinding { override fun close() = Unit }
        }

        override fun stopService() = Unit
        override fun close() = Unit
    }

    companion object {
        private val grantedPermission = NearbyDevicesPermissionReader { NearbyDevicesPermissionState.GRANTED }
        private val scheduler = BleRuntimeScheduler { _, _ ->
            object : BleReconnectLease { override fun close() = Unit }
        }
    }
}
