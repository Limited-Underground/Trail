package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import kotlin.test.*

class FactoryResetConfirmationPresentationTest {
    private fun session(configuration: BleConfigurationState): BleActiveSession = BleActiveSession(
        BleDiscoveredCompanion("opaque", "Trail-23ABCD"), 42,
        CompanionStatusSnapshot(1, CompanionRadioState.UNKNOWN, CompanionGnssState.UNKNOWN,
            CompanionPowerState.UNKNOWN, CompanionPositionSharingState.STOPPED, 0),
        CompanionProtocolInfo(capabilities = 0),
        checkNotNull(GroupLocationSnapshot.authoritativeUnavailable(1, CompanionPositionSharingState.STOPPED)),
        configuration = configuration,
    )

    @Test fun usesOnlyTheCurrentValidatedNameReadback() {
        assertEquals("Camp board", factoryResetBoardName(session(BleConfigurationState(
            deviceName = "Camp board", nameRevision = 1u,
        ))))
        for (configuration in listOf(
            BleConfigurationState(deviceName = "Unsaved draft"),
            BleConfigurationState(deviceName = "", nameRevision = 1u),
            BleConfigurationState(deviceName = "Bad\nname", nameRevision = 1u),
        )) {
            assertEquals("this MeshTrail board", factoryResetBoardName(session(configuration)))
        }
        assertEquals("this MeshTrail board", factoryResetBoardName(null))
    }

    @Test fun describesTheBoardsDataAndExplicitlyExcludesThePhoneAndOtherBoards() {
        assertTrue(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("this board's phone pairing"))
        assertTrue(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("does not erase your phone or other boards"))
        assertTrue(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("Firmware and factory hardware data remain"))
        assertTrue(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("cannot be undone"))
    }
}
