package io.github.nbjelanovic.otprotocol

import kotlin.test.*

class CompanionPublicProfileTest {
    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()

    @Test fun sharedCorpusDecodesFieldsAndReencodesExactly() {
        val source = assertNotNull(javaClass.getResourceAsStream("/companion_public_profile_v1_vectors.csv"))
        val rows = source.bufferedReader(Charsets.UTF_8).use { it.readLines() }.drop(1)
        assertEquals(21, rows.size)
        for (row in rows) {
            val cells = row.split(','); assertEquals(8, cells.size)
            val bytes = hex(cells[7]); val value = CompanionPublicProfileCodec.decode(bytes)
            if (cells[1] == "0") { assertNull(value, cells[0]); continue }
            val accepted = assertNotNull(value, cells[0])
            assertEquals(cells[2].toInt(), accepted.kind, cells[0])
            assertEquals(cells[3].toInt(), accepted.reason, cells[0])
            assertEquals(cells[4].toULong(), accepted.revision, cells[0])
            assertEquals(cells[5] == "1", accepted.visible, cells[0])
            assertContentEquals(hex(cells[6]), accepted.name.toByteArray(Charsets.UTF_8), cells[0])
            assertContentEquals(bytes, CompanionPublicProfileCodec.encode(accepted), cells[0])
        }
    }

    @Test fun ProfileNegotiationIsExplicitAndLegacyProfilesRefuseNewCommands() {
        val payload = assertNotNull(CompanionPublicProfileCodec.encode(CompanionPublicProfilePayload(1)))
        for (minor in listOf(2, 3, CompanionConfirmationCodec.PROFILE)) {
            assertNull(CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(8, 7u, 11u, payload, minor)))
        }
        val info = CompanionConfigurationInfo(0xff, minorVersion=5)
        val encoded = assertNotNull(CompanionConfigurationCodec.encodeInfo(info))
        assertNull(CompanionConfigurationCodec.decodeInfo(encoded))
        assertNull(CompanionConfigurationCodec.decodeInfo(encoded, 3))
        assertEquals(info, CompanionConfigurationCodec.decodeInfo(encoded, 5))
        val frame = assertNotNull(CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(8, 7u, 11u, payload, 5)))
        assertNotNull(CompanionConfigurationCodec.decodeFrame(frame, 5))
        assertNull(CompanionConfigurationCodec.decodeFrame(frame, 3))
        assertTrue(CompanionConfigurationCodec.transportCompatible(info, 0x10, 151, 148, 148, true))
        assertNull(CompanionConfigurationCodec.encodeInfo(info.copy(capabilities=0xef)))
    }

    @Test fun LosslessNamesFlagsAndExhaustionAreBoundedWithoutNormalization() {
        val valid = CompanionPublicProfilePayload(2, name="A".repeat(40), visible=false)
        assertNotNull(CompanionPublicProfileCodec.encode(valid))
        for (name in listOf("A".repeat(41), "\u4e2d".repeat(33), " A", "A ", "A\u0085B", "A\uD800")) {
            assertNull(CompanionPublicProfileCodec.encode(valid.copy(name=name)), name)
        }
        assertNull(CompanionPublicProfileCodec.encode(valid.copy(revision=ULong.MAX_VALUE)))
        assertNull(CompanionPublicProfileCodec.encode(CompanionPublicProfilePayload(0x81)))
        assertNotNull(CompanionPublicProfileCodec.encode(CompanionPublicProfilePayload(0x81,visible=true)))
        assertFalse(valid.toString().contains(valid.name))
    }
}
