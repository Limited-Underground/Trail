package io.github.nbjelanovic.otprotocol

import kotlin.test.*

class CompanionNamePayloadTest {
    @Test fun fixedWriteFixtureMatchesLittleEndianContract() {
        val value = CompanionNamePayload(CompanionNameKind.WRITE, 0x0102030405060708uL, "Trail")
        val expected = hex("4f544e43010200050807060504030201547261696c")
        assertContentEquals(expected, CompanionNamePayloadCodec.encode(value))
        assertEquals(value, CompanionNamePayloadCodec.decode(expected))
    }

    @Test fun everyValidStateRoundTrips() {
        listOf(
            CompanionNamePayload(CompanionNameKind.READ),
            CompanionNamePayload(CompanionNameKind.WRITE, name = "Camp"),
            CompanionNamePayload(CompanionNameKind.SNAPSHOT),
            CompanionNamePayload(CompanionNameKind.SNAPSHOT, 1u, "Camp"),
            CompanionNamePayload(CompanionNameKind.APPLIED, 2u, "Camp"),
            CompanionNamePayload(CompanionNameKind.REJECTED, reason = CompanionNameReason.STALE_REVISION),
            CompanionNamePayload(CompanionNameKind.UNCERTAIN, reason = CompanionNameReason.STORAGE_FAILURE),
        ).forEach { assertEquals(it, CompanionNamePayloadCodec.decode(assertNotNull(CompanionNamePayloadCodec.encode(it)))) }
    }

    @Test fun invalidBindingsCannotClaimApplied() {
        listOf(
            CompanionNamePayload(CompanionNameKind.APPLIED, name = "Camp"),
            CompanionNamePayload(CompanionNameKind.APPLIED, 1u),
            CompanionNamePayload(CompanionNameKind.WRITE, ULong.MAX_VALUE, "Camp"),
            CompanionNamePayload(CompanionNameKind.READ, 1u),
            CompanionNamePayload(CompanionNameKind.SNAPSHOT, 1u),
            CompanionNamePayload(CompanionNameKind.REJECTED),
            CompanionNamePayload(CompanionNameKind.UNCERTAIN, reason = CompanionNameReason.UNAUTHORIZED),
        ).forEach { assertNull(CompanionNamePayloadCodec.encode(it)) }
    }

    @Test fun namesAreBoundedStrictUtf8WithoutControls() {
        listOf("", " x", "x ", "a\n", "a\u0080", "x".repeat(33), "\uD800").forEach {
            assertNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.WRITE, name = it)))
        }
        for (name in listOf("界".repeat(32), "😀".repeat(16))) {
            val value = CompanionNamePayload(CompanionNameKind.WRITE, name = name)
            assertEquals(value, CompanionNamePayloadCodec.decode(assertNotNull(CompanionNamePayloadCodec.encode(value))))
        }
    }

    @Test fun malformedWireFailsClosed() {
        val valid = hex("4f544e43010200050807060504030201547261696c")
        for (size in valid.indices) assertNull(CompanionNamePayloadCodec.decode(valid.copyOf(size)))
        assertNull(CompanionNamePayloadCodec.decode(valid + 0))
        for ((offset, byte) in listOf(0 to 0, 4 to 2, 5 to 9, 6 to 99, 7 to 96, 16 to 0xff)) {
            assertNull(CompanionNamePayloadCodec.decode(valid.copyOf().also { it[offset] = byte.toByte() }))
        }
    }

    @Test fun unsignedRevisionBoundariesArePreservedExactly() {
        for (revision in listOf(1uL, 0x7fffffffffffffffuL, 0x8000000000000000uL, ULong.MAX_VALUE)) {
            for (kind in listOf(CompanionNameKind.SNAPSHOT, CompanionNameKind.APPLIED)) {
                val value = CompanionNamePayload(kind, revision, "X")
                val encoded = assertNotNull(CompanionNamePayloadCodec.encode(value))
                repeat(8) { assertEquals((revision shr (8 * it)).toByte(), encoded[8 + it]) }
                assertEquals(value, CompanionNamePayloadCodec.decode(encoded))
            }
        }
        val lastWrite = CompanionNamePayload(CompanionNameKind.WRITE, ULong.MAX_VALUE - 1u, "X")
        assertEquals(lastWrite, CompanionNamePayloadCodec.decode(assertNotNull(CompanionNamePayloadCodec.encode(lastWrite))))
        val exhaustedWrite = assertNotNull(CompanionNamePayloadCodec.encode(
            CompanionNamePayload(CompanionNameKind.APPLIED, ULong.MAX_VALUE, "X")))
        exhaustedWrite[5] = CompanionNameKind.WRITE.code.toByte()
        assertNull(CompanionNamePayloadCodec.decode(exhaustedWrite))
    }

    @Test fun malformedUtf8CannotBeNormalizedOrReplaced() {
        for (bytes in listOf("80", "c080", "c1bf", "e08080", "eda080", "edbfbf",
            "f0808080", "f4908080", "f5808080", "fe", "ff", "c2", "e282", "f09f98", "e228a1")) {
            val name = hex(bytes)
            val header = hex("4f544e43010200000000000000000000")
            header[7] = name.size.toByte()
            assertNull(CompanionNamePayloadCodec.decode(header + name), bytes)
        }
        for (name in listOf("\uD800", "\uDC00", "A\uD800B", "\uDC00\uD800")) {
            assertNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.WRITE, name = name)))
        }
    }

    @Test fun exactNameLimitsAndControlsAreEnforcedOnBothDirections() {
        val maximum = assertNotNull(CompanionNamePayloadCodec.encode(
            CompanionNamePayload(CompanionNameKind.WRITE, name = "界".repeat(32))))
        assertEquals(112, maximum.size)
        assertEquals(96, maximum[7].toInt())
        assertNotNull(CompanionNamePayloadCodec.decode(maximum))
        for (name in listOf("a".repeat(33), "界".repeat(33), "😀".repeat(17), " x", "x ") +
            ((0..31) + (127..159)).map { "a${it.toChar()}b" }) {
            assertNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.WRITE, name = name)))
            val raw = name.toByteArray(Charsets.UTF_8)
            val header = hex("4f544e43010200000000000000000000")
            header[7] = raw.size.toByte()
            assertNull(CompanionNamePayloadCodec.decode(header + raw))
        }
    }

    @Test fun diagnosticStringDoesNotExposeName() {
        assertFalse(CompanionNamePayload(CompanionNameKind.WRITE, name = "PrivateName").toString().contains("PrivateName"))
    }

    @Test fun sharedCrossLanguageCorpusAgreesAndValidRowsReencodeExactly() {
        val resource = assertNotNull(javaClass.getResourceAsStream("/companion_device_name_v1.tsv"))
        val rows = resource.bufferedReader(Charsets.UTF_8).use { it.readLines() }
            .filter { it.isNotBlank() && !it.startsWith('#') }
        assertTrue(rows.isNotEmpty())
        val ids = mutableSetOf<String>()
        for (row in rows) {
            val fields = row.split('\t')
            assertEquals(7, fields.size, row)
            assertTrue(ids.add(fields[0]), "Duplicate case: ${fields[0]}")
            assertTrue(fields[1] == "0" || fields[1] == "1", row)
            assertTrue(fields[2].length % 2 == 0 && fields[2].all { it in "0123456789abcdefABCDEF" }, row)
            val wire = hex(fields[2])
            val decoded = CompanionNamePayloadCodec.decode(wire)
            if (fields[1] == "1") {
                val value = assertNotNull(decoded, fields[0])
                assertEquals(fields[3].toInt(), value.kind.code, fields[0])
                assertEquals(fields[4].toInt(), value.reason.code, fields[0])
                assertEquals(fields[5].toULong(), value.revision, fields[0])
                val nameBytes = if (fields[6] == "-") byteArrayOf() else hex(fields[6])
                assertContentEquals(nameBytes, value.name.toByteArray(Charsets.UTF_8), fields[0])
                assertContentEquals(wire, CompanionNamePayloadCodec.encode(value), fields[0])
            } else assertNull(decoded, fields[0])
        }
    }

    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
