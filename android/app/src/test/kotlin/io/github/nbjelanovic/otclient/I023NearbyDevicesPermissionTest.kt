package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class I023NearbyDevicesPermissionTest {
    @Test fun reenteringBluetoothModeDoesNotOpenAnotherPermissionRequest() {
        var permission = NearbyDevicesPermissionState.MISSING
        val controller = i023PermissionController { permission }
        controller.use {
            controller.onLifecycleStart()
            controller.chooseBluetoothDeviceMode()
            assertTrue(controller.beginNearbyDevicesPermissionRequest())

            // A second tap routed through Find device must keep the outstanding sheet fenced.
            controller.chooseBluetoothDeviceMode()
            val waiting = assertIs<TrailAppUiState.BluetoothDevice>(controller.state)
            assertTrue(waiting.permissionRequestInFlight)
            assertFalse(controller.beginNearbyDevicesPermissionRequest())

            controller.onNearbyDevicesPermissionResult()
            val denied = assertIs<TrailAppUiState.BluetoothDevice>(controller.state)
            assertFalse(denied.permissionRequestInFlight)
            assertTrue(denied.permissionWasDenied)

            assertTrue(controller.beginNearbyDevicesPermissionRequest())
            permission = NearbyDevicesPermissionState.GRANTED
            controller.onNearbyDevicesPermissionResult()
            val granted = assertIs<TrailAppUiState.BluetoothDevice>(controller.state)
            assertEquals(NearbyDevicesPermissionState.GRANTED, granted.permissionState)
            assertFalse(granted.permissionRequestInFlight)
            assertFalse(granted.permissionWasDenied)
            assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, granted.serviceState)
        }
    }
}

internal fun i023PermissionController(readPermission: () -> NearbyDevicesPermissionState) =
    TrailActivityController(
        localController = CompanionAppController(UnavailableProductionLocalTransport),
        permissionReader = NearbyDevicesPermissionReader(readPermission),
        notificationPermissionReader = NotificationPermissionReader { NotificationPermissionState.NOT_REQUIRED },
        serviceConnector = object : ConnectedDeviceServiceConnector {
            override fun startFromVisibleUserAction() = error("Permission alone cannot start the service")
            override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit) =
                error("Permission alone cannot bind the service")
            override fun stopService() = Unit
            override fun close() = Unit
        },
        serviceStartupScheduler = BleRuntimeScheduler { _, _ -> error("Unexpected scheduling") },
    )
