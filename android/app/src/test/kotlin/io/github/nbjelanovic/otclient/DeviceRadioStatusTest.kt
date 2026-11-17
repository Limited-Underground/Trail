package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionRadioState
import kotlin.test.Test
import kotlin.test.assertEquals

class DeviceRadioStatusTest {
    @Test fun labelsDistinguishMissingTelemetryFromReportedRadioStates() {
        assertEquals(
            listOf("radio status unknown", "radio status not reported", "radio ready", "radio degraded", "radio fault"),
            CompanionRadioState.entries.map { it.deviceStatusLabel() },
        )
    }
}
