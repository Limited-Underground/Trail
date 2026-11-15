package io.github.nbjelanovic.otclient

import android.Manifest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class NearbyPermissionDenialTest {
    private val scan = Manifest.permission.BLUETOOTH_SCAN
    private val connect = Manifest.permission.BLUETOOTH_CONNECT

    @Test fun currentGrantTakesPrecedenceOverAStaleDenial() {
        val result = mapOf(scan to false, connect to true)
        assertNull(nearbyPermissionDenial(result, isGranted = { true }, shouldShowRationale = { false }))
    }

    @Test fun deniedPermissionCanBeRequestedAgainWhenAndroidShowsRationale() {
        val result = mapOf(scan to true, connect to false)
        assertEquals(
            NearbyPermissionDenial.RETRYABLE,
            nearbyPermissionDenial(result, isGranted = { it == scan }, shouldShowRationale = { true }),
        )
    }

    @Test fun denialWithoutRationaleDirectsMissingPermissionToSettings() {
        val result = mapOf(scan to true, connect to false)
        assertEquals(
            NearbyPermissionDenial.SETTINGS_ONLY,
            nearbyPermissionDenial(result, isGranted = { it == scan }, shouldShowRationale = { false }),
        )
    }

    @Test fun canceledAndPartialResultsDoNotInventDenialsForMissingPermissions() {
        assertNull(nearbyPermissionDenial(emptyMap(), isGranted = { false }, shouldShowRationale = { false }))
        assertNull(nearbyPermissionDenial(mapOf(scan to true), isGranted = { it == scan }, shouldShowRationale = { false }))
    }

    @Test fun unavailableRationaleDoesNotAssumePermanentDenial() {
        assertEquals(
            NearbyPermissionDenial.RETRYABLE,
            nearbyPermissionDenial(mapOf(scan to false), isGranted = { false }, shouldShowRationale = { null }),
        )
    }
}
