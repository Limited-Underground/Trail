package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRequest

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
    fun selectBluetoothDevice(endpointToken: String)
    fun replaceLostPhoneWithBluetoothDevice(endpointToken: String)
    fun disconnectBluetoothDevice()
    fun submitBluetoothAction(request: CompanionActionRequest): Boolean
}

interface TrailLifecycleController : AutoCloseable {
    fun onLifecycleStart()
    fun onLifecycleStop()
}

/** Service-owned subset; implemented by the production Bluetooth controller, never the Activity host. */
interface TrailServiceController : TrailUiController, TrailLifecycleController
