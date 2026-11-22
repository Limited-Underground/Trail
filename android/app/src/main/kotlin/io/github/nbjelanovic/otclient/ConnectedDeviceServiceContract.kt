package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

const val CONNECTED_DEVICE_NOTIFICATION_CHANNEL_ID = "trail_connected_device"
const val CONNECTED_DEVICE_NOTIFICATION_CHANNEL_NAME = "Connected device"
const val CONNECTED_DEVICE_NOTIFICATION_TITLE = "Limited Underground MeshTrail"
const val CONNECTED_DEVICE_NOTIFICATION_TEXT = "MeshTrail is running in the background."

enum class NotificationPermissionState {
    NOT_REQUIRED,
    GRANTED,
    DENIED,
}

enum class ConnectedDeviceServiceUiState {
    START_REQUIRED,
    STARTING,
    RUNNING,
    BOUND_ONLY,
    START_FAILED,
}

enum class ConnectedDeviceServiceStartFailure {
    NOT_VISIBLE_USER_ACTION,
    NEARBY_PERMISSION_MISSING,
    PLATFORM_REJECTED,
    SERVICE_UNAVAILABLE,
}

data class ConnectedDeviceServiceStartAdmission(
    val allowed: Boolean,
    val reducedNotificationVisibility: Boolean,
    val failure: ConnectedDeviceServiceStartFailure? = null,
)

/** Visible-action/Nearby admission subset. BLE remains unavailable below API 31 despite minSdk 26. */
object ConnectedDeviceForegroundServicePolicy {
    fun admit(
        visibleUserAction: Boolean,
        nearbyPermissionGranted: Boolean,
        notificationPermissionState: NotificationPermissionState,
    ): ConnectedDeviceServiceStartAdmission = when {
        !visibleUserAction -> ConnectedDeviceServiceStartAdmission(
            allowed = false,
            reducedNotificationVisibility = notificationPermissionState == NotificationPermissionState.DENIED,
            failure = ConnectedDeviceServiceStartFailure.NOT_VISIBLE_USER_ACTION,
        )
        !nearbyPermissionGranted -> ConnectedDeviceServiceStartAdmission(
            allowed = false,
            reducedNotificationVisibility = notificationPermissionState == NotificationPermissionState.DENIED,
            failure = ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING,
        )
        else -> ConnectedDeviceServiceStartAdmission(
            allowed = true,
            reducedNotificationVisibility = notificationPermissionState == NotificationPermissionState.DENIED,
        )
    }
}

sealed interface ConnectedDeviceServiceConnection {
    data class Connected(val port: ConnectedDeviceSessionPort) : ConnectedDeviceServiceConnection
    data object Disconnected : ConnectedDeviceServiceConnection
}

interface ConnectedDeviceServiceBinding : AutoCloseable
fun interface ConnectedDeviceStateObservation : AutoCloseable

/** close() releases Activity observation/bind leases only; it must never stop the service. */
interface ConnectedDeviceServiceConnector : AutoCloseable {
    fun startFromVisibleUserAction(): ConnectedDeviceServiceStartFailure?
    fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding?
    /** Observe an already running owner only. Implementations must not create or start a service. */
    fun bindExisting(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? = null
    /** Activity-scoped binding for a cold returning-owner check. This never starts a foreground service. */
    fun bindForReturningOwner(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? = null
    fun stopService()
}

/**
 * In-process binder surface. It exposes only bounded public state and opaque endpoint tokens; no
 * Android Bluetooth object, address, device name, wire correlation, or secret crosses this seam.
 */
interface ConnectedDeviceSessionPort {
    val generation: Long
    val state: TrailAppUiState.BluetoothDevice

    fun observe(observer: (TrailAppUiState.BluetoothDevice) -> Unit): ConnectedDeviceStateObservation?
    /** Called only from a current foreground Activity's returning-owner binding. */
    fun activateReturningOwner(): Boolean = false
    fun refreshPermissionState()
    fun refreshBluetoothPreflight() = Unit
    fun scan()
    fun retryDiscovery(): BluetoothRecoveryRefusal? = BluetoothRecoveryRefusal.OWNER_UNAVAILABLE
    fun authorize(endpointToken: String)
    fun disconnect()
    fun submitText(recipient: V1QuickStatusRecipient, text: String): Boolean = false
    fun submitQuickStatus(recipient: V1QuickStatusRecipient, status: io.github.nbjelanovic.otprotocol.CompanionQuickStatus): Boolean = false
    fun submitAction(request: CompanionActionRequest): Boolean
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
