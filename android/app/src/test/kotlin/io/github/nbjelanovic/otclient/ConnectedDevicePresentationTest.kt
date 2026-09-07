package io.github.nbjelanovic.otclient

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class ConnectedDevicePresentationTest {
    @Test fun currentProtectedReadbackNamesTheConnectedDevice() {
        assertEquals("Connected to Trail Bench", connectedDevicePresentation(
            BleConfigurationState(available = true, deviceName = "Trail Bench", nameRevision = 3uL),
        ).title)
    }

    @Test fun confirmedUnnamedDeviceGetsAnActionablePrompt() {
        val result = connectedDevicePresentation(BleConfigurationState(available = true, nameRevision = 0uL))
        assertEquals("Connected to unnamed device", result.title)
        assertEquals("Give this device a name below so you can recognize it.", result.detail)
    }

    @Test fun unconfirmedOrPreviousSessionNameIsNotPresentedAsCurrent() {
        val result = connectedDevicePresentation(BleConfigurationState(available = true, deviceName = "Old device"))
        assertEquals("Connected to Trail device", result.title)
        assertFalse(result.toString().contains("Old device"))
        assertEquals("Connected to Trail device", connectedDevicePresentation(BleConfigurationState()).title)
    }
}
