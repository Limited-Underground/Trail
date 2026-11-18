package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*

/** UI-only controller. Tests must explicitly handle every action they exercise. */
internal class P1UiController(initial: TrailAppUiState) : TrailUiController by idleController() {
    override var state = initial
        private set
    private var observer: ((TrailAppUiState) -> Unit)? = null
    var connectionRequests = 0
    var resetConfirmations = 0
    var discoveryRetries = 0

    override fun observe(observer: ((TrailAppUiState) -> Unit)?) {
        this.observer = observer
        observer?.invoke(state)
    }

    fun publish(state: TrailAppUiState) {
        this.state = state
        observer?.invoke(state)
    }

    override fun chooseBluetoothDeviceMode() { connectionRequests++ }
    override fun scanBluetoothDevices() { connectionRequests++ }
    override fun retryBluetoothDiscovery() { discoveryRetries++ }
    override fun startBluetoothService() { connectionRequests++ }
    override fun requestFactoryResetConfirmation(): Boolean {
        publish((state as TrailAppUiState.BluetoothDevice).copy(factoryResetConfirmationVisible = true))
        return true
    }
    override fun confirmFactoryReset(): Boolean { resetConfirmations++; return true }
    override fun cancelFactoryResetConfirmation() {
        publish((state as TrailAppUiState.BluetoothDevice).copy(factoryResetConfirmationVisible = false))
    }
}

private fun idleController() = TrailActivityController(
    localController = CompanionAppController(UnavailableProductionLocalTransport),
    permissionReader = NearbyDevicesPermissionReader { NearbyDevicesPermissionState.GRANTED },
    notificationPermissionReader = NotificationPermissionReader { NotificationPermissionState.NOT_REQUIRED },
    serviceConnector = object : ConnectedDeviceServiceConnector {
        override fun startFromVisibleUserAction() = error("Unexpected service start")
        override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit) = error("Unexpected service bind")
        override fun stopService() = Unit
        override fun close() = Unit
    },
    serviceStartupScheduler = BleRuntimeScheduler { _, _ -> error("Unexpected scheduling") },
)

internal fun p1BluetoothState(runtime: BleRuntimeState) = TrailAppUiState.BluetoothDevice(
    runtime, NearbyDevicesPermissionState.GRANTED, false, false, DeviceAuthorizationUiState.None,
    serviceState = ConnectedDeviceServiceUiState.RUNNING,
)

internal fun p1Session(name: String = "Camp board", nonce: Long = 42) = BleActiveSession(
    BleDiscoveredCompanion("fixture-board", "Trail-23ABCD"), nonce,
    CompanionStatusSnapshot(1, CompanionRadioState.UNKNOWN, CompanionGnssState.UNKNOWN,
        CompanionPowerState.UNKNOWN, CompanionPositionSharingState.STOPPED, 0),
    CompanionProtocolInfo(capabilities = 0xff),
    checkNotNull(GroupLocationSnapshot.authoritativeUnavailable(1, CompanionPositionSharingState.STOPPED)),
    configuration = BleConfigurationState(
        available = true, editorSessionId = 1, deviceName = name, nameRevision = 1u,
        regionAvailable = true, regionSelectionId = 1, regionRevision = 1u,
        publicProfileAvailable = true, publicName = name, publiclyDiscoverable = false, publicRevision = 1u,
    ),
)
