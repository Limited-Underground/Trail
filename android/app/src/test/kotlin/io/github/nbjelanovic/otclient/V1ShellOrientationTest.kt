package io.github.nbjelanovic.otclient

import android.content.res.Configuration
import kotlin.test.Test
import kotlin.test.assertEquals

class V1ShellOrientationTest {
    @Test fun portraitNavigationDoesNotDependOnKeyboardReducedHeight() {
        // The old 411 x 250 usable content aspect would incorrectly select a rail.
        assertEquals(V1AppNavigationLayout.PORTRAIT_BOTTOM_BAR,
            v1NavigationForConfiguration(Configuration.ORIENTATION_PORTRAIT))
    }

    @Test fun landscapeNavigationUsesRail() {
        assertEquals(V1AppNavigationLayout.LANDSCAPE_NAVIGATION_RAIL,
            v1NavigationForConfiguration(Configuration.ORIENTATION_LANDSCAPE))
    }

    @Test fun UndefinedOrientationHasStableBottomBarFallback() {
        assertEquals(V1AppNavigationLayout.PORTRAIT_BOTTOM_BAR,
            v1NavigationForConfiguration(Configuration.ORIENTATION_UNDEFINED))
    }
}
