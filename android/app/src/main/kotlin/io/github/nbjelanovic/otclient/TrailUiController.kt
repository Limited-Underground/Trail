package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

/** Observation only. A trigger never replaces Bluetooth preflight. */
enum class BluetoothRecoveryTrigger { BLUETOOTH_TURNED_ON, FOREGROUND_RECHECK }

/** Only public recovery outcomes cross the Activity/service seam. */
enum class BluetoothRecoveryRefusal {
    BLUETOOTH_OFF,
    PERMISSION_REQUIRED,
    PERMISSION_PENDING,
    OWNER_UNAVAILABLE,
    AUTHORIZATION_PENDING,
    RESET_PENDING,
    PLATFORM_BLOCKED,
    STATE_CHANGED,
}

/** Bounded UI command surface shared by the Activity host and the service-owned controller. */
interface TrailUiController {
    val state: TrailAppUiState

    fun observe(observer: ((TrailAppUiState) -> Unit)?)
    fun chooseLocalTestMode()
    fun chooseBluetoothDeviceMode()
    fun returnToModeChoice()
    fun chooseLocalDevice()
    fun cancelLocalSelection()
    fun connectLocalDevice(endpointToken: String)
    fun disconnectLocalDevice()
    fun retryLocalSelection()
    fun submitLocalAction(request: CompanionActionRequest)
    fun beginNearbyDevicesPermissionRequest(): Boolean
    fun onNearbyDevicesPermissionResult()
    fun refreshPermissionState()
    fun startBluetoothService()
    fun scanBluetoothDevices()
    fun retryBluetoothDiscovery(): BluetoothRecoveryRefusal? = BluetoothRecoveryRefusal.OWNER_UNAVAILABLE
    fun selectBluetoothDevice(endpointToken: String)
    fun disconnectBluetoothDevice()
    fun submitText(recipient: V1QuickStatusRecipient, text: String): Boolean = false
    fun submitQuickStatus(recipient: V1QuickStatusRecipient, status: io.github.nbjelanovic.otprotocol.CompanionQuickStatus): Boolean = false
    fun submitBluetoothAction(request: CompanionActionRequest): Boolean
    fun refreshGroupConfirmation(): Boolean = false
    fun confirmGroupConfirmation(offer: V1GroupConfirmationOffer): Boolean = false
    fun cancelGroupConfirmation(offer: V1GroupConfirmationOffer): Boolean = false
    fun readRadioRegion(): Boolean = false
    fun writeRadioRegion(selectionId: Int): Boolean = false
    fun readPublicProfile(): Boolean = false
    fun submitBoardGroupAction(action: V1BoardGroupAction): Boolean = false
    fun writePublicProfile(name: String, visible: Boolean): Boolean = false
    fun readDeviceName(): Boolean = false
    fun writeDeviceName(name: String): Boolean = false
    fun synchronizeDisplayTime(): Boolean = false
    fun requestFactoryResetConfirmation(): Boolean = false
    fun cancelFactoryResetConfirmation() = Unit
    fun confirmFactoryReset(): Boolean = false
    fun retryFactoryResetVerification(): Boolean = false
}

interface TrailLifecycleController : AutoCloseable {
    fun onLifecycleStart()
    fun onLifecycleStop()
}

/** Service-owned subset; implemented by the production Bluetooth controller, never the Activity host. */
interface TrailServiceController : TrailUiController, TrailLifecycleController {
    fun setStatusRefreshForeground(foreground: Boolean) = Unit
    fun refreshBluetoothPreflight() = Unit
}
