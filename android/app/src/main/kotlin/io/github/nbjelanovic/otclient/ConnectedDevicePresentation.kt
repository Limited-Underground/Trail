package io.github.nbjelanovic.otclient

internal data class ConnectedDevicePresentation(val title: String, val detail: String)

/** Uses only the current protected name readback, never a scan ordinal or cached draft. */
internal fun connectedDevicePresentation(state: BleConfigurationState): ConnectedDevicePresentation {
    val name = state.deviceName?.takeIf { state.nameRevision != null }
        ?.let(V1DeviceName::create)?.value
    return when {
        name != null -> ConnectedDevicePresentation("Connected to $name", "Your device is connected.")
        state.nameRevision != null -> ConnectedDevicePresentation(
            "Connected to unnamed device",
            "Give this device a name below so you can recognize it.",
        )
        else -> ConnectedDevicePresentation(
            "Connected to Trail device",
            "Device name is not available yet.",
        )
    }
}
