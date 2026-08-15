package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertNull

class DeviceAuthorizationClaimTest {
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
