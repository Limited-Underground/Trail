package io.github.nbjelanovic.otclient

import kotlin.test.assertEquals
import org.junit.Test

class V1HomeLaunchPolicyTest {
    @Test fun settingUpLaterOpensMessagesWithoutCompletingSetupAndCanReturn() {
        val initial = V1HomeLaunchState.initial(TrailAppUiState.ChooseMode)
        val later = initial.select(V1AppDestination.MESSAGES)
        assertEquals(V1AppDestination.MESSAGES, later.destination)
        assertEquals(false, later.setupArmed)
        assertEquals(false, later.completionHandled)
        val restored = checkNotNull(V1HomeLaunchState.restore(later.save()))
        val back = restored.beginSetup(null)
        assertEquals(V1AppDestination.DEVICE, back.destination)
        assertEquals(true, back.setupArmed)
        assertEquals(false, back.completionHandled)
    }

    @Test fun coldLaunchOpensConnectionSetupWithoutGuessingBondState() {
        // Both new and returning owners start in ChooseMode until they choose a transport.
        assertEquals(V1AppDestination.DEVICE, v1InitialHomeDestination(TrailAppUiState.ChooseMode))
    }

    @Test fun establishedTransportDoesNotInferMissingBondFromDisconnectedOrSetupState() {
        val runtimeStates = listOf(
            BleRuntimeState.Inactive,
            BleRuntimeState.Idle,
            BleRuntimeState.FindingReturningOwner,
            BleRuntimeState.Scanning(emptyList()),
            BleRuntimeState.FactoryResetComplete(true),
        )
        for (runtime in runtimeStates) {
            val state = TrailAppUiState.BluetoothDevice(
                runtimeState = runtime,
                permissionState = NearbyDevicesPermissionState.GRANTED,
                permissionRequestInFlight = false,
                permissionWasDenied = false,
                authorizationState = DeviceAuthorizationUiState.None,
            )
            assertEquals(V1AppDestination.MESSAGES, v1InitialHomeDestination(state), runtime.toString())
        }
    }

    @Test fun localTestKeepsItsExistingMessagesEntry() {
        val state = TrailAppUiState.LocalTest(CompanionUiState.Disconnected(emptyList()))
        assertEquals(V1AppDestination.MESSAGES, v1InitialHomeDestination(state))
    }
}
