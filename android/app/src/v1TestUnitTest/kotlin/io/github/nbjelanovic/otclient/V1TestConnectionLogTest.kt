package io.github.nbjelanovic.otclient

import kotlin.test.*

class V1TestConnectionLogTest {
    @Test fun sessionsPersistAndElapsedRestartsWithoutInventingContinuity() {
        val storage = Storage()
        val first = V1TestConnectionLog(storage)
        assertFalse(first.recordConnection(0, V1TestPhoneConnectionState.READY))
        assertEquals(V1TestConnectionLogStatus.SESSION_REQUIRED, first.status)
        assertTrue(first.startSession())
        assertTrue(first.recordConnection(900, V1TestPhoneConnectionState.READY))
        val second = V1TestConnectionLog(storage)
        assertTrue(second.startSession())
        assertTrue(second.recordConnection(1, V1TestPhoneConnectionState.DISCONNECTED))
        assertEquals(listOf(1L, 2L), second.snapshot().map { it.session })
        assertTrue(second.exportText().contains("not a continuous radio trace"))
        assertTrue(second.exportText().contains("Gaps are unobserved"))
    }

    @Test fun repeatedConnectionsDoNotWriteAndBackwardTimeIsRejected() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        assertTrue(log.recordConnection(1, V1TestPhoneConnectionState.CONNECTING))
        val writes = storage.writes
        assertTrue(log.recordConnection(3, V1TestPhoneConnectionState.CONNECTING))
        assertEquals(writes, storage.writes)
        assertFalse(log.recordLifecycle(2, V1TestAppLifecycleState.FOREGROUND))
        assertEquals(V1TestConnectionLogStatus.INVALID_EVENT, log.status)
        assertTrue(log.recordLifecycle(3, V1TestAppLifecycleState.FOREGROUND))
        assertTrue(log.recordConnection(4, V1TestPhoneConnectionState.CONNECTING))
        assertEquals(2, log.snapshot().size)
    }

    @Test fun newest512SurviveBoundedSerializationAndReload() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        repeat(520) { assertTrue(log.recordLifecycle(it.toLong(), V1TestAppLifecycleState.FOREGROUND)) }
        assertEquals(512, log.snapshot().size)
        assertEquals(8L, log.snapshot().first().elapsedMillis)
        assertTrue(requireNotNull(storage.bytes).size <= V1_TEST_CONNECTION_LOG_MAX_BYTES)
        assertEquals(log.snapshot(), V1TestConnectionLog(storage).snapshot())
    }

    @Test fun failedWritesPreserveMemoryAndDurableHistoryAndCanRetry() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        assertTrue(log.recordConnection(1, V1TestPhoneConnectionState.READY))
        val before = log.snapshot()
        val durableBefore = requireNotNull(storage.bytes).copyOf()
        storage.writeResult = false
        assertFalse(log.recordConnection(2, V1TestPhoneConnectionState.DISCONNECTED))
        assertFalse(log.startSession())
        assertEquals(V1TestConnectionLogStatus.STORAGE_FAILURE, log.status)
        assertEquals(before, log.snapshot())
        assertContentEquals(durableBefore, storage.bytes)
        storage.writeResult = true
        assertTrue(log.recordConnection(2, V1TestPhoneConnectionState.DISCONNECTED))
        assertEquals(1L, log.snapshot().last().session)
    }

    @Test fun corruptionIsExplicitAndCannotBeOverwrittenUntilClear() {
        val malformed = listOf(
            "", "OTCL\t5\t0\n", "OTCL\t1\t0", "OTCL\t1\t0\n1\t0\tC\tREADY\n",
            "OTCL\t1\t1\n1\t0\tC\tUNRECOGNIZED\n", "OTCL\t1\t1\n1\t0\tNOTE\ttext\n",
            "OTCL\t1\t1\n1\t2\tC\tREADY\n1\t1\tC\tFAILED\n", "OTCL\t1\t01\n",
            "OTCL\t1\t1\n1\t-1\tC\tREADY\n", "OTCL\t1\t9223372036854775808\n",
        )
        malformed.forEach { value ->
            val storage = Storage().apply { bytes = value.toByteArray() }
            val log = V1TestConnectionLog(storage)
            assertEquals(V1TestConnectionLogStatus.CORRUPT, log.status, value)
            assertFalse(log.startSession())
            assertEquals(0, storage.writes)
            assertTrue(log.clear())
            assertTrue(log.startSession())
        }
        listOf(ByteArray(65 * 1024), byteArrayOf(0xff.toByte())).forEach { value ->
            assertEquals(V1TestConnectionLogStatus.CORRUPT, V1TestConnectionLog(Storage().apply { bytes = value }).status)
        }
    }

    @Test fun storageExceptionsAreContainedAndClearFailurePreservesHistory() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        assertTrue(log.recordLifecycle(1, V1TestAppLifecycleState.FOREGROUND))
        storage.throwErrors = true
        assertFalse(log.recordLifecycle(2, V1TestAppLifecycleState.BACKGROUND))
        assertFalse(log.clear())
        assertEquals(1, log.snapshot().size)
        assertEquals(V1TestConnectionLogStatus.STORAGE_FAILURE, V1TestConnectionLog(storage).status)
        storage.throwErrors = false
        assertTrue(log.clear())
        assertTrue(log.snapshot().isEmpty())
        assertFalse(log.recordLifecycle(0, V1TestAppLifecycleState.FOREGROUND))
    }

    @Test fun sessionCounterCannotOverflow() {
        val storage = Storage().apply { bytes = "OTCL\t1\t${Long.MAX_VALUE}\n".toByteArray() }
        val log = V1TestConnectionLog(storage)
        assertFalse(log.startSession())
        assertEquals(V1TestConnectionLogStatus.EXHAUSTED, log.status)
        assertEquals(0, storage.writes)
    }

    @Test fun boundedProtectedReadFactsRoundTripAndKeepOldVersion3Rows() {
        val storage = Storage().apply {
            bytes = ("OTCL\t3\t1\n1\t0\tS\tPROTECTED_PROTOCOL_INFO_REJECTED\t1\t1\t7\t0\t0\tCONNECTION_START_FAILED\tGATT_PLATFORM_FAILURE\n").toByteArray()
        }
        val log = V1TestConnectionLog(storage)
        assertEquals(3, log.loadedVersion)
        val oldRows = log.snapshot()
        assertTrue(log.startSession())
        assertTrue(log.recordProtectedRead(1, diagnostic()))
        val reloaded = V1TestConnectionLog(storage)
        assertEquals(4, reloaded.loadedVersion)
        assertEquals(oldRows, reloaded.snapshot().take(1))
        assertEquals(log.snapshot(), reloaded.snapshot())
        assertTrue(reloaded.exportText().contains("READ_CALLBACK_VALUE\tPLATFORM_FAILURE\t5095\t2\t1\t133\t0\t0\t0"))
        assertTrue(reloaded.exportText().contains("not proof of explicit device rejection"))
    }

    @Test fun protectedReadRowsRejectUnknownEnumsUnboundedNumbersAndNoncanonicalFlags() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        assertTrue(log.recordProtectedRead(1, diagnostic()))
        val encoded = requireNotNull(storage.bytes).decodeToString()
        val row = encoded.lines()[1].split('\t')
        val invalidFields = listOf(3 to "0", 4 to "0", 5 to "RAW_TEXT", 6 to "RAW_TEXT",
            7 to "60001", 8 to "-1", 9 to "9223372036854775807", 10 to "65536",
            11 to "true", 12 to "513", 13 to "2", 14 to "4", 15 to "true", 16 to "2", 17 to "01")
        invalidFields.forEach { (index, invalid) ->
            val fields = row.toMutableList().also { it[index] = invalid }
            val malformed = "OTCL\t4\t1\n${fields.joinToString("\t")}\n"
            val bad = V1TestConnectionLog(Storage().apply { bytes = malformed.toByteArray() })
            assertEquals(V1TestConnectionLogStatus.CORRUPT, bad.status, "$index=$invalid")
        }
        assertEquals(V1TestConnectionLogStatus.CORRUPT,
            V1TestConnectionLog(Storage().apply { bytes = encoded.replace("OTCL\t4", "OTCL\t3").toByteArray() }).status)
    }

    @Test fun protectedReadRowsShareExistingByteAndRecordBudgets() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        repeat(600) { assertTrue(log.recordProtectedRead(it.toLong(), diagnostic().copy(connection = Long.MAX_VALUE))) }
        assertTrue(log.snapshot().size <= 512)
        assertTrue(requireNotNull(storage.bytes).size <= 65_536)
        assertEquals(599L, log.snapshot().last().elapsedMillis)
        assertEquals(log.snapshot(), V1TestConnectionLog(storage).snapshot())
    }

    private fun diagnostic() = ProtectedProtocolInfoDiagnostic(1, 1,
        ProtectedProtocolInfoDiagnosticOrigin.READ_CALLBACK_VALUE, ProtectedProtocolInfoDiagnosticOutcome.PLATFORM_FAILURE,
        5095, 2, 1, 133, false, 0, false, null, true, true, true)

    private class Storage : V1TestConnectionLogStorage {
        var bytes: ByteArray? = null
        var writes = 0
        var writeResult = true
        var throwErrors = false
        override fun read(): ByteArray? { check(!throwErrors); return bytes?.copyOf() }
        override fun write(encoded: ByteArray): Boolean {
            check(!throwErrors)
            if (!writeResult) return false
            bytes = encoded.copyOf(); writes++; return true
        }
        override fun clear(): Boolean { check(!throwErrors); bytes = null; return true }
    }
}
