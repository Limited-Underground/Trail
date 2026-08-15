package io.github.nbjelanovic.otclient

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build

fun interface NotificationPermissionReader {
    fun current(): NotificationPermissionState
}

class AndroidNotificationPermissionReader(private val context: Context) : NotificationPermissionReader {
    override fun current(): NotificationPermissionState = when {
        Build.VERSION.SDK_INT < 33 -> NotificationPermissionState.NOT_REQUIRED
        context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED ->
            NotificationPermissionState.GRANTED
        else -> NotificationPermissionState.DENIED
    }
}
