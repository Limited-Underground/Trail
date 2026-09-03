package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class DeviceAuthorizationClaimTest {
    @Test
    fun appExposesOnlyInitialPhoneAuthorization() {
        assertEquals(
            listOf(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE),
            DeviceAuthorizationPurpose.entries,
        )
    }

    @Test
    fun appCommandSurfacesExposeNoReplacementAndOnlyTheDedicatedFactoryResetAuthority() {
        listOf(
            TrailUiController::class.java,
            ConnectedDeviceSessionPort::class.java,
        ).forEach { commandSurface ->
            val commandNames = commandSurface.methods.map { it.name.lowercase() }
            assertFalse(commandNames.any { it.contains("replace") })
            assertTrue(commandNames.contains("requestfactoryresetconfirmation"))
            assertTrue(commandNames.contains("cancelfactoryresetconfirmation"))
            assertTrue(commandNames.contains("confirmfactoryreset"))
            assertTrue(commandNames.contains("retryfactoryresetverification"))
            assertFalse(commandNames.contains("submitfactoryreset"))
        }
    }

    @Test
    fun productionDefaultRemainsDisabledAndPublishesNoInventedResult() {
        var callbackInvoked = false
        val lease = DisabledDeviceAuthorizationClaimClient().createClaim(
            endpointToken = "opaque-test-endpoint",
            purpose = DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE,
        ) { callbackInvoked = true }
        assertNull(lease)
        assertFalse(callbackInvoked)
    }
}
