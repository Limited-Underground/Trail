package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

const val CONNECTED_DEVICE_NOTIFICATION_CHANNEL_ID = "trail_connected_device"
const val CONNECTED_DEVICE_NOTIFICATION_CHANNEL_NAME = "Connected device"
const val CONNECTED_DEVICE_NOTIFICATION_TITLE = "Limited Underground Trail"
const val CONNECTED_DEVICE_NOTIFICATION_TEXT = "Bluetooth device connection service is running"

enum class NotificationPermissionState {
    NOT_REQUIRED,
    GRANTED,
    DENIED,
}

enum class ConnectedDeviceServiceUiState {
    START_REQUIRED,
    STARTING,
    RUNNING,
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
    fun refreshPermissionState()
    fun scan()
    fun authorize(endpointToken: String)
    fun replaceLostPhone(endpointToken: String)
    fun disconnect()
    fun submitAction(request: CompanionActionRequest): Boolean
}
