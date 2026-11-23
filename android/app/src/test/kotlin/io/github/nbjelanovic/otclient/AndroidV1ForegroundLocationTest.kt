package io.github.nbjelanovic.otclient

import android.Manifest
import android.location.Location
import android.location.LocationManager
import android.os.SystemClock
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AndroidV1ForegroundLocationTest {
    @Test fun permissionAndGpsAreUsedOnlyAfterVisibleConsentAndStopOnPause() {
        val app = RuntimeEnvironment.getApplication()
        val manager = requireNotNull(app.getSystemService(LocationManager::class.java))
        val provider = shadowOf(manager)
        val providerName = LocationManager.GPS_PROVIDER
        provider.setProviderEnabled(providerName, true)
        val owner = Owner()
        val link = LocationBinding(LocationAudience(LocationAudienceKind.GROUP, 4, 1), 9)
        val board = TestBoard()
        AndroidV1ForegroundLocation(app, owner, { link }, board, {}).use { location ->
            owner.event(Lifecycle.Event.ON_CREATE)
            owner.event(Lifecycle.Event.ON_START)
            owner.event(Lifecycle.Event.ON_RESUME)
            val requested = mutableListOf<String>()
            location.askPermission { requested.addAll(it) }
            assertTrue(requested.isEmpty())
            assertTrue(provider.getLocationUpdateListeners(providerName).isEmpty())

            location.chooseShare()
            assertEquals(PhoneLocationState.ALLOW_PHONE_LOCATION, location.state)
            location.askPermission { requested.addAll(it) }
            assertEquals(listOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
                requested)

            shadowOf(app).grantPermissions(Manifest.permission.ACCESS_FINE_LOCATION)
            location.refresh()
            assertEquals(PhoneLocationState.PAUSED, location.state)
            assertTrue(provider.getLocationUpdateListeners(providerName).isEmpty())

            location.resumeSharing()
            val first = provider.getLocationUpdateListeners(providerName).single()
            first.onLocationChanged(fix(providerName))
            assertEquals(PhoneLocationState.SHARING, location.state)
            assertEquals(1, board.starts)

            owner.event(Lifecycle.Event.ON_PAUSE)
            assertEquals(PhoneLocationState.PAUSED, location.state)
            assertTrue(provider.getLocationUpdateListeners(providerName).isEmpty())
            assertEquals(1, board.stops)

            owner.event(Lifecycle.Event.ON_RESUME)
            assertTrue(provider.getLocationUpdateListeners(providerName).isEmpty())
            location.resumeSharing()
            val second = provider.getLocationUpdateListeners(providerName).single()
            assertNotSame(first, second)
            first.onLocationChanged(fix(providerName))
            assertEquals(1, board.starts)
            second.onLocationChanged(fix(providerName))
            assertEquals(2, board.starts)
        }
        assertTrue(provider.getLocationUpdateListeners(providerName).isEmpty())
    }

    private fun fix(provider: String) = Location(provider).apply {
        latitude = 12.0
        longitude = 40.0
        elapsedRealtimeNanos = SystemClock.elapsedRealtimeNanos()
    }

    private class Owner : LifecycleOwner {
        private val registry = LifecycleRegistry(this)
        override val lifecycle: Lifecycle get() = registry
        fun event(event: Lifecycle.Event) = registry.handleLifecycleEvent(event)
    }

    private class TestBoard : PhoneLocationBoard {
        var starts = 0
        var stops = 0
        override fun laneAvailable(binding: LocationBinding): Boolean = true
        override fun consent(binding: LocationBinding, result: (Boolean) -> Unit) = result(true)
        override fun withdraw(binding: LocationBinding) = Unit
        override fun start(binding: LocationBinding, fix: PhoneLocationFix, result: (Long?) -> Unit) {
            result((++starts).toLong())
        }
        override fun renew(binding: LocationBinding, lease: Long, fix: PhoneLocationFix?, result: (Boolean) -> Unit) {
            result(true)
        }
        override fun stop(binding: LocationBinding, lease: Long) { stops++ }
    }
}
