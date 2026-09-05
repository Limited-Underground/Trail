package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1SupportReportTest {
    @Test
    fun createsDeterministicPlainTextReportForPreviewSaveOrShare() {
        val report = assertNotNull(
            V1SupportReportGenerator.create(snapshot(), "Reconnect took longer than expected.\r\nRetried once."),
        )
        assertEquals("trail-support-report.txt", report.fileName)
        assertEquals("text/plain", report.mimeType)
        assertTrue(report.text.startsWith("OT-SUPPORT/1\n"))
        assertTrue(report.text.contains("This file is not sent automatically."))
        assertTrue(report.text.contains("may contain details you typed"))
        assertTrue(report.text.contains("Reconnect took longer than expected.\nRetried once."))
        assertTrue(report.text.contains("Radio region: US915"))
        assertTrue(report.text.contains("Reconnect count: 3"))
    }

    @Test
    fun automaticSchemaHasNoPrivateIdentifiersMessagesInvitationsOrCoordinates() {
        val report = assertNotNull(V1SupportReportGenerator.create(snapshot(), ""))
        listOf("MAC:", "Bluetooth address:", "Pairing ID:", "PIN:", "Message body:", "Latitude:", "Longitude:", "Invitation PIN:")
            .forEach { assertFalse(report.text.contains(it), it) }
        assertTrue(report.text.contains("[Excluded from automatic diagnostics]"))
    }

    @Test
    fun publicDiagnosticLabelsRejectCommonSecretAndIdentifierShapes() {
        assertNotNull(label("Heltec V4.2"))
        val hardwareAddressShape = listOf("AA", "BB", "CC", "DD", "EE", "FF").joinToString(":")
        assertNull(V1PublicDiagnosticLabel.create(hardwareAddressShape))
        assertNull(V1PublicDiagnosticLabel.create(hardwareAddressShape.replace(':', '-')))
        assertNull(V1PublicDiagnosticLabel.create(hardwareAddressShape.replace(":", "")))
        assertNull(V1PublicDiagnosticLabel.create("Heltec $hardwareAddressShape nearby"))
        assertNull(V1PublicDiagnosticLabel.create("ID ${hardwareAddressShape.replace(":", "")} unit"))
        assertNull(V1PublicDiagnosticLabel.create("123e4567-e89b-12d3-a456-426614174000"))
        assertNull(V1PublicDiagnosticLabel.create("pairing key 1234"))
        assertNull(V1PublicDiagnosticLabel.create("x".repeat(81)))
    }

    @Test
    fun userProblemNoteIsVisibleBoundedAndRequiresReview() {
        assertNotNull(V1SupportReportGenerator.create(snapshot(), "x".repeat(V1_SUPPORT_NOTE_MAX_CHARS)))
        assertNull(V1SupportReportGenerator.create(snapshot(), "x".repeat(V1_SUPPORT_NOTE_MAX_CHARS + 1)))
        assertNull(V1SupportReportGenerator.create(snapshot(), "bad\u0000note"))
    }

    @Test
    fun negativeCountersFailAndSnapshotStringDoesNotDumpFields() {
        assertFailsWith<IllegalArgumentException> { snapshot().copy(reconnectCount = -1) }
        assertEquals("V1SafeDiagnosticSnapshot(redacted)", snapshot().toString())
        assertFalse(snapshot().toString().contains("US915"))
    }

    private fun snapshot() = V1SafeDiagnosticSnapshot(
        appVersion = label("1.0.0-test"),
        firmwareVersion = label("110e543"),
        phoneModel = label("Android test phone"),
        androidVersion = label("13"),
        deviceModel = label("Heltec V4.2"),
        radioRegion = V1RadioRegion.US915,
        phoneConnectionState = V1SupportPhoneConnection.READY,
        radioState = V1SupportRadioHealth.IDLE,
        gpsState = V1SupportGpsHealth.CURRENT,
        powerState = V1SupportPowerHealth.BATTERY_ESTIMATE_UNAVAILABLE,
        reconnectCount = 3,
        lastSupportCode = V1SupportCode.BLE_RECONNECT,
    )

    private fun label(value: String) = requireNotNull(V1PublicDiagnosticLabel.create(value))
}
