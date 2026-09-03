package io.github.nbjelanovic.otclient

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class AndroidPairingPresentationTest {
    @Test
    fun connectionDiagnosticsExposeOnlyFixedIdentityFreeSupportCodes() {
        val expected = mapOf(
            BleConnectionDiagnostic.LEASE_UNAVAILABLE to "BLE-LEASE-UNAVAILABLE",
            BleConnectionDiagnostic.START_REJECTED to "BLE-OPEN-REJECTED",
            BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE to "BLE-DISCONNECTED-BEFORE-PROFILE",
            BleConnectionDiagnostic.GATT_TRANSIENT_LINK to "BLE-GATT-TRANSIENT-LINK",
            BleConnectionDiagnostic.GATT_PERMISSION_REVOKED to "BLE-GATT-PERMISSION-REVOKED",
            BleConnectionDiagnostic.GATT_SECURITY_REJECTED to "BLE-GATT-SECURITY-REJECTED",
            BleConnectionDiagnostic.GATT_BOND_REQUIRED to "BLE-GATT-BOND-REQUIRED",
            BleConnectionDiagnostic.GATT_AUTHORIZATION_REJECTED to "BLE-GATT-AUTHORIZATION-REJECTED",
            BleConnectionDiagnostic.GATT_PLATFORM_FAILURE to "BLE-GATT-PLATFORM-FAILURE",
        )

        assertEquals(expected, BleConnectionDiagnostic.entries.associateWith { it.supportCode() })
        assertTrue(expected.values.all { it.matches(Regex("[A-Z0-9-]+")) })

        val source = projectFile("src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt").readText()
        assertTrue(source.contains("Support code:"))
        assertTrue(source.contains("state.connectionDiagnostic.supportCode()"))
    }

    @Test
    fun initialPairingInstructionsUseTheAutomaticUnownedBootWindowBeforeTheOsBond() {
        val instructions = androidSystemPairingInstructions(
            DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE,
        )

        assertTrue(instructions.beforeActionBody.startsWith("Restart the unowned or just-reset device"))
        assertTrue(instructions.beforeActionBody.contains("open automatically for 60 seconds"))
        assertTrue(instructions.beforeActionBody.contains("Then tap Authorize this phone"))
        assertFalse(instructions.beforeActionBody.contains("30-second"))
        assertFalse(instructions.beforeActionBody.contains("hold", ignoreCase = true))
        assertFalse(instructions.beforeActionBody.contains("3 seconds"))
        assertTrue(instructions.beforeActionBody.contains("only in Android's system pairing dialog"))
        assertTrue(instructions.beforeActionBody.contains("never receives or stores the code"))
        assertTrue(instructions.startingBody.contains("automatically opened 60-second PIN window"))
        assertTrue(instructions.startingBody.contains("restart the unowned device"))
        assertTrue(instructions.pendingBody.contains("does not require a second physical confirmation"))
        assertFalse(instructions.startingBody.contains("Do not press"))
    }

    @Test
    fun pairingAndRecoveryContractsUseAuthoritativeTimingAndExplicitDestructiveCopy() {
        assertEquals(60_000L, ANDROID_SYSTEM_BOND_TIMEOUT_MILLIS)
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("cannot take over"))
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("at least 10 seconds"))
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("release when prompted"))
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("short-press within 10 seconds"))
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("erases all Trail user data"))
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("reboots the device unowned"))
        assertTrue(LOST_PHONE_RECOVERY_PUBLIC_TEXT.contains("open automatically for 60 seconds"))
        assertTrue(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("permanently erases"))
        assertTrue(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("cannot be undone"))
        assertFalse(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("not available", ignoreCase = true))
        assertFalse(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT.contains("short-press", ignoreCase = true))
    }

    @Test
    fun factoryResetEntryIsCapabilityGatedBehindTheFullScreenDeviceSettingsRoute() {
        val source = projectFile("src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt").readText()
        val readyStart = source.indexOf("private fun BluetoothReadyPanel(")
        val settingsStart = source.indexOf("private fun DeviceSettingsScreen(")
        val confirmationStart = source.indexOf("private fun FactoryResetConfirmationScreen(")
        assertTrue(readyStart >= 0 && settingsStart > readyStart && confirmationStart > settingsStart)

        val readyPanel = source.substring(readyStart, settingsStart)
        val settingsScreen = source.substring(settingsStart, confirmationStart)
        assertTrue(readyPanel.contains("Text(\"Device settings\")"))
        assertFalse(readyPanel.contains("requestFactoryResetConfirmation"))
        assertTrue(settingsScreen.contains("COMPANION_FACTORY_RESET_CAPABILITY"))
        assertTrue(settingsScreen.contains("requestFactoryResetConfirmation"))
        assertTrue(settingsScreen.contains("Modifier.fillMaxSize()"))
        assertFalse(settingsScreen.contains("verticalScroll"))
        assertFalse(source.contains("APP_FACTORY_RESET_UNAVAILABLE"))
    }

    @Test
    fun nonRetryableFactoryResetResultHasAnExplicitSafeExitAndNoResetResubmission() {
        val source = projectFile("src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt").readText()
        val notVerifiedStart = source.indexOf("is BleRuntimeState.FactoryResetNotVerified ->")
        val completeStart = source.indexOf("is BleRuntimeState.FactoryResetComplete ->")
        assertTrue(notVerifiedStart >= 0 && completeStart > notVerifiedStart)

        val notVerifiedPanel = source.substring(notVerifiedStart, completeStart)
        assertTrue(notVerifiedPanel.contains("if (state.canRetryVerification)"))
        assertTrue(notVerifiedPanel.contains("onClick = controller::disconnectBluetoothDevice"))
        assertTrue(notVerifiedPanel.contains("Text(\"Return to Bluetooth\")"))
        assertFalse(notVerifiedPanel.contains("confirmFactoryReset"))
        assertFalse(notVerifiedPanel.contains("submitFactoryReset"))
    }

    private fun projectFile(path: String): File {
        val direct = File(path)
        if (direct.isFile) return direct
        val fromAndroid = File("app", path)
        require(fromAndroid.isFile) { "Required Android source file is missing: $path" }
        return fromAndroid
    }
}
