package io.github.nbjelanovic.otclient

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build

class AndroidNearbyDevicesPermissionReader(context: Context) : NearbyDevicesPermissionReader {
    private val appContext = context.applicationContext

    override fun current(): NearbyDevicesPermissionState {
        if (Build.VERSION.SDK_INT < ANDROID_BLE_MINIMUM_API) return NearbyDevicesPermissionState.UNSUPPORTED
        return if (
            AndroidBlePermissionContract.runtimePermissions.all { permission ->
                appContext.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
            }
        ) {
            NearbyDevicesPermissionState.GRANTED
        } else {
            NearbyDevicesPermissionState.MISSING
        }
    }
}
