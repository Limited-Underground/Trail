package io.github.nbjelanovic.otclient

import android.content.Context

/** Phone-local presentation only. Does not share the device or template stores. */
internal class AppDisplayPreferences(context: Context) {
    private val preferences = context.getSharedPreferences("v1-app-settings", Context.MODE_PRIVATE)

    fun showBuildLabel(): Boolean = preferences.getBoolean("show-build-label", true)

    fun setShowBuildLabel(show: Boolean): Boolean =
        preferences.edit().putBoolean("show-build-label", show).commit()
}
