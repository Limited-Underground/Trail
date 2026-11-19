package io.github.nbjelanovic.otclient

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class MeshTrailOperationLogTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun rotationKeepsRecentRecordsAcrossReopenWithinTwoSegmentLimits() {
        val directory = temp.newFolder()
        val log = MeshTrailOperationLog(directory, segmentBytes = 128)
        repeat(55) { check(log.append(it.toLong(), MeshTrailLogKind.APP, "opened")) }

        val files = directory.listFiles()!!.toList()
        assertEquals(2, files.size)
        assertTrue(files.all { it.length() <= 128L })
        val text = MeshTrailOperationLog(directory, segmentBytes = 128).exportText()
        assertFalse(text.contains("+0s  App: opened\n"))
        assertTrue(text.endsWith("+54s  App: opened\n"))
        assertTrue(text.indexOf("+50s  App: opened") < text.indexOf("+54s  App: opened"))
    }

    @Test fun unapprovedValuesAndMultilineInputNeverReachDiskOrExport() {
        val directory = temp.newFolder()
        val log = MeshTrailOperationLog(directory)
        listOf(
            "123456", "AA:BB:CC:DD:EE:FF", "board-name", "49.245,-123.11",
            "message body", "opened\nsecret key", "opened\tprivate", "opened\u0000",
        ).forEach { assertFalse(log.append(7, MeshTrailLogKind.APP, it)) }
        assertFalse(log.append(-1, MeshTrailLogKind.APP, "opened"))
        assertTrue(directory.listFiles()!!.isEmpty())
        assertTrue(log.append(8, MeshTrailLogKind.APP, "opened"))
        assertFalse(log.exportText().contains("secret"))
    }

    @Test fun textIsUtf8WithoutBomOrJsonAndEmptyLogHasAPlainExplanation() {
        val log = MeshTrailOperationLog(temp.newFolder())
        assertEquals("MeshTrail operation log\nLog format 1\nTimes are seconds since logging started and may restart when the app restarts.\nNo events recorded yet.\n", log.exportText())
        assertTrue(log.append(1, MeshTrailLogKind.APP, "opened"))
        val expected = "MeshTrail operation log\nLog format 1\nTimes are seconds since logging started and may restart when the app restarts.\n+1s  App: opened\n"
        assertTrue(expected.toByteArray(Charsets.UTF_8).contentEquals(log.exportText().toByteArray(Charsets.UTF_8)))
    }

    @Test fun corruptAndOversizedSegmentsNeverLeakBytesAndWritingRecovers() {
        val directory = temp.newFolder()
        val previous = File(directory, "previous.log")
        previous.writeText("invalid\tsecret\n".repeat(25))
        val current = File(directory, "current.log")
        current.writeBytes(byteArrayOf(0xFF.toByte(), 1, 2))
        val log = MeshTrailOperationLog(directory, segmentBytes = 128)
        val text = log.exportText()
        assertTrue(text.contains("Some entries were unreadable."))
        assertFalse(text.contains("secret"))
        assertFalse(text.contains("\uFFFD"))
        assertTrue(log.append(12, MeshTrailLogKind.APP, "opened"))
        assertTrue(log.exportText().endsWith("+12s  App: opened\n"))
    }

    @Test fun malformedDeviceRecordRecordsOnlyAFixedRefusal() {
        val directory = temp.newFolder()
        val log = MeshTrailOperationLog(directory)
        val input = "TEST secret pair 123456 at 1.234 2.345".toByteArray()
        assertFalse(log.recordDeviceParameters(3, input))
        assertEquals("MeshTrail operation log\nLog format 1\nTimes are seconds since logging started and may restart when the app restarts.\n+3s  Device parameters: invalid record\n", log.exportText())
        assertFalse(directory.listFiles()!!.single().readText().contains("123456"))
    }

    @Test fun decodedRecordAddsOnlyApprovedParametersWithAgesAndKnownZero() {
        val wire = java.nio.ByteBuffer.allocate(212).order(java.nio.ByteOrder.LITTLE_ENDIAN).apply {
            put(byteArrayOf(0x4f, 0x54, 0x44, 0x50, 1, 0))
            putShort(6, 212)
            putInt(8, 2)
            putInt(12, 300)
            putLong(16, 900)
            putInt(24, (1 shl 0) or (1 shl 5) or (1 shl 11))
            putInt(32, 100)
            putShort(36, 3700)
            put(38, 42)
            putInt(104, 200)
            put(108, 2)
            put(110, 1)
            putInt(164, 300)
            putInt(168, 7)
            val privateLookingVersion = "private-name.123456".toByteArray(Charsets.US_ASCII)
            put(172, privateLookingVersion.size.toByte())
            putInt(176, 0x10000)
            position(180)
            put(privateLookingVersion)
        }.array()
        val directory = temp.newFolder()
        val log = MeshTrailOperationLog(directory)
        assertTrue(log.recordDeviceParameters(20, wire))
        val result = MeshTrailOperationLog(directory).exportText()
        assertTrue(result.contains("sequence=2 age_ms=300 uptime_ms=900"))
        assertTrue(result.contains("Board battery: age_ms=400 millivolts=3700 percent=42"))
        assertTrue(result.contains("GNSS health: age_ms=500 state=SEARCHING satellites=0"))
        assertTrue(result.contains("Board software: age_ms=600 release_generation=7 partition_offset=65536"))
        assertFalse(result.contains("Radio counters"))
        assertFalse(result.contains("private-name"))
        assertFalse(directory.listFiles()!!.single().readText().contains("123456"))
    }

    @Test fun ownerStateProjectionRecordsTransitionsAndReasonsWithoutCandidateData() {
        val log = MeshTrailOperationLog(temp.newFolder())
        val observer = MeshTrailLogTransitions { kind, detail -> check(log.append(12, kind, detail)) }
        val candidate = BleDiscoveredCompanion("AA:BB:CC:DD:EE:FF", "private board name")
        val state = TrailAppUiState.BluetoothDevice(
            runtimeState = BleRuntimeState.Connecting(candidate),
            permissionState = NearbyDevicesPermissionState.GRANTED,
            permissionRequestInFlight = false,
            permissionWasDenied = false,
            authorizationState = DeviceAuthorizationUiState.None,
        )
        observer.observeService(state)
        observer.observeService(state)
        observer.observeService(state.copy(runtimeState = BleRuntimeState.Failed(
            BleRuntimeFailure.CONNECTION_START_FAILED, BleConnectionDiagnostic.GATT_PLATFORM_FAILURE,
        )))
        val text = log.exportText()
        assertEquals(1, Regex("Connection: connecting").findAll(text).count())
        assertTrue(text.contains("Connection error: CONNECTION_START_FAILED"))
        assertTrue(text.contains("Connection detail: GATT_PLATFORM_FAILURE"))
        assertFalse(text.contains(candidate.endpointToken))
        assertFalse(text.contains(candidate.publicLabel))
    }
}
