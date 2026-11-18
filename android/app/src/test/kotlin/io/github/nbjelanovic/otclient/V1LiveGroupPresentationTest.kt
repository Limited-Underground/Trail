package io.github.nbjelanovic.otclient

import kotlin.test.*

class V1LiveGroupPresentationTest {
    @Test fun liveRouteNeverAdvertisesUnimplementedGroupSetupEvenWithDeviceConfirmation() {
        for (connected in listOf(false, true)) {
            val state = v1LiveGroupScreenState(connected, V1GroupConfirmationState.Idle)
            assertFalse(state.setupAvailable)
            assertFalse(state.canSubmit)
            assertFalse(state.canShowQr)
            assertFalse(state.canScanQr)
            assertNull(state.groupName)
            assertEquals(if (connected) V1GroupConfirmationState.Idle else V1GroupConfirmationState.Unsupported,
                state.confirmation)
        }
    }
}
