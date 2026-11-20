package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp")
class V1SetupResumeUiTest {
    @get:Rule val ui = createComposeRule()

    @Test fun coldEntryDoesNotDisplayStepOneBeforeReadingTheReturningDevice() {
        val controller = P1UiController(TrailAppUiState.ChooseMode)
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)

        // A newly created Activity cannot tell whether this is a first device or a returning one.
        ui.onNodeWithText("Step 1 of 5").assertDoesNotExist()
        val session = p1Session()
        val needsRegion = session.copy(configuration = session.configuration.copy(
            regionRevision = 0uL,
            regionSelectionId = null,
        ))
        ui.runOnIdle { controller.publish(p1BluetoothState(BleRuntimeState.Ready(needsRegion))) }

        ui.onNodeWithText("Step 4 of 5", substring = true).assertExists()
        ui.runOnIdle { assertEquals(0, controller.connectionRequests) }
    }

    @Test fun coldEntryAlreadyAttachedToAPartlyConfiguredDeviceKeepsSetupVisible() {
        val session = p1Session()
        val needsProfile = session.copy(configuration = session.configuration.copy(
            publicRevision = 0uL,
            publicName = null,
            publiclyDiscoverable = null,
        ))
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.Ready(needsProfile)))
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)

        ui.onNodeWithText("Step 5 of 5", substring = true).assertExists()
        ui.onNodeWithText("Step 1 of 5").assertDoesNotExist()
        ui.runOnIdle { assertEquals(0, controller.connectionRequests) }
    }

    @Test fun coldEntryWaitingForAReturningDeviceOpensItsIncompleteSetupWhenReady() {
        val controller = P1UiController(p1BluetoothState(BleRuntimeState.FindingReturningOwner))
        ui.setContent { MaterialTheme { TrailApp(controller) } }
        ui.mainClock.advanceTimeBy(2_000)

        val session = p1Session()
        val needsProfile = session.copy(configuration = session.configuration.copy(
            publicRevision = 0uL,
            publicName = null,
            publiclyDiscoverable = null,
        ))
        ui.runOnIdle { controller.publish(p1BluetoothState(BleRuntimeState.Ready(needsProfile))) }

        ui.onNodeWithText("Step 5 of 5", substring = true).assertExists()
        ui.runOnIdle { assertEquals(0, controller.connectionRequests) }
    }

    @Test fun deliberateAndLegacyMessagesSelectionsAreReusedWhenAnOwnerArrives() {
        val returning = p1BluetoothState(BleRuntimeState.FindingReturningOwner)
        val session = p1Session()
        val incomplete = p1BluetoothState(BleRuntimeState.Ready(session.copy(
            configuration = session.configuration.copy(publicRevision = 0uL, publicName = null),
        )))
        val pending = V1HomeLaunchState.initial(returning)
        val deliberate = pending.select(V1AppDestination.GROUP)
        val legacy = checkNotNull(V1HomeLaunchState.restore(listOf("MESSAGES", false, false)))
        val restoredPending = checkNotNull(V1HomeLaunchState.restore(pending.save()))

        assertEquals(V1AppDestination.GROUP, deliberate.resolveInitialSetup(incomplete).destination)
        assertEquals(V1AppDestination.MESSAGES, legacy.resolveInitialSetup(incomplete).destination)
        assertEquals(V1AppDestination.DEVICE, restoredPending.resolveInitialSetup(incomplete).destination)
    }

    @Test fun completedAndUnsupportedDevicesKeepTheirMessagesEntry() {
        val session = p1Session()
        val unsupported = session.copy(configuration = session.configuration.copy(available = false))
        for (current in listOf(session, unsupported)) {
            val state = p1BluetoothState(BleRuntimeState.Ready(current))
            val pending = V1HomeLaunchState.initial(p1BluetoothState(BleRuntimeState.FindingReturningOwner))
            assertEquals(V1AppDestination.MESSAGES, V1HomeLaunchState.initial(state).destination)
            assertEquals(V1AppDestination.MESSAGES, pending.resolveInitialSetup(state).destination)
        }
    }
}
