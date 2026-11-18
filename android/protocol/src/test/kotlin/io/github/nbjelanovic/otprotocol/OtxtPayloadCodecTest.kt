package io.github.nbjelanovic.otprotocol

import java.util.Base64
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class OtxtPayloadCodecTest {
    private data class Vector(
        val id: String, val layer: String, val refusal: String,
        val payload: String, val frame: String, val text: String, val budget: String,
    )

    private fun vectors(): List<Vector> = checkNotNull(javaClass.getResourceAsStream("/otxt-v1-vectors.tsv"))
        .bufferedReader(Charsets.UTF_8).useLines { lines ->
            lines.filter { it.isNotBlank() && !it.startsWith("#") }.map { line ->
                val c = line.split('\t')
                check(c.size == 7)
                Vector(c[0], c[1], c[2], c[3], c[4], c[5], c[6])
            }.toList()
        }

    @Test fun frozenGoldenPayloadsAreByteExactInBothDirections() {
        val cases = vectors().filter { it.refusal == "accept" }
        assertEquals(listOf("T-0042-golden-ascii", "T-0042-golden-multibyte", "T-0042-golden-at-limit"), cases.map { it.id })
        cases.forEach { v ->
            val text = String(Base64.getDecoder().decode(v.text), Charsets.UTF_8)
            val payload = hex(v.payload)
            assertEquals(text, OtxtPayloadCodec.decode(payload), v.id)
            assertContentEquals(payload, OtxtPayloadCodec.encode(text), v.id)
            assertContentEquals(payload, OtxtPayloadCodec.encode(assertNotNull(OtxtPayloadCodec.decode(payload))), v.id)
        }
    }

    @Test fun everyFrozenPayloadFaultIsRefused() {
        val cases = vectors().filter { it.layer == "payload" && it.refusal != "accept" }
        assertEquals(14, cases.size)
        cases.forEach { assertNull(OtxtPayloadCodec.decode(hex(it.payload)), it.id) }
    }

    @Test fun frozenPacketFaultsRemainOutsideThePayloadDomain() {
        // Exact original encrypted frames are not OTXT plaintext. This is a domain guard,
        // NOT authentication, replay, recipient, Packet-version or fragment acceptance.
        val cases = vectors().filter { it.layer == "packet" }
        assertEquals(listOf("wrong-recipient", "replay", "bad-tag", "truncated-frame", "unknown-packet-version", "fragmentation")
            .map { "T-0042-fault-$it" }, cases.map { it.id })
        cases.forEach { assertNull(OtxtPayloadCodec.decode(hex(it.frame)), it.id) }
        // An otherwise valid payload grants no authority: it still round-trips independently
        // of the outer-layer faults, whose checks belong to the existing Packet owner.
        val golden = vectors().first { it.refusal == "accept" }
        val payload = hex(golden.payload)
        assertContentEquals(payload, OtxtPayloadCodec.encode(assertNotNull(OtxtPayloadCodec.decode(payload))))
    }

    @Test fun frozenLowerLinkBudgetRefusesTheExactAtLimitPayload() {
        val v = vectors().single { it.layer == "budget" }
        assertEquals("T-0042-fault-link-too-small", v.id)
        val golden = vectors().single { it.id == "T-0042-golden-at-limit" }
        assertEquals(golden.frame, v.frame)
        val payload = hex(golden.payload)
        val text = assertNotNull(OtxtPayloadCodec.decode(payload))
        assertNull(OtxtPayloadCodec.decode(payload, v.budget.toInt()))
        assertNull(OtxtPayloadCodec.encode(text, v.budget.toInt()))
        assertEquals(24, vectors().size)
        assertEquals(24, vectors().map { it.id }.toSet().size)
    }

    @Test fun byteBoundariesIncludeAsciiMultibyteAndSupplementaryScalars() {
        for (make in listOf<(Int) -> String>(
            { "x".repeat(it) },
            { n -> "界".repeat(n / 3) + "x".repeat(n % 3) },
            { n -> "🚲".repeat(n / 4) + "x".repeat(n % 4) },
        )) {
            for (n in 89..91) {
                val text = make(n)
                assertEquals(n, text.toByteArray(Charsets.UTF_8).size)
                if (n <= 90) {
                    val expected = envelope(text.toByteArray(Charsets.UTF_8))
                    assertContentEquals(expected, OtxtPayloadCodec.encode(text), "encode $n")
                    assertEquals(text, OtxtPayloadCodec.decode(expected), "decode $n")
                } else {
                    assertNull(OtxtPayloadCodec.encode(text), "encode $n")
                    assertNull(OtxtPayloadCodec.decode(envelope(text.toByteArray(Charsets.UTF_8))), "decode $n")
                }
            }
        }
    }

    @Test fun refusesEmptyAndUnpairedUtf16WithoutReplacement() {
        assertNull(OtxtPayloadCodec.encode(""))
        for (text in listOf("\uD800", "\uDC00", "x\uD800", "\uDC00x", "\uD800x\uDC00", "\uDC00\uD800")) {
            assertNull(OtxtPayloadCodec.encode(text))
        }
        assertEquals("🚲", OtxtPayloadCodec.decode(assertNotNull(OtxtPayloadCodec.encode("🚲"))))
    }

    @Test fun strictDecoderRejectsAdditionalMalformedUtf8() {
        for (wire in listOf("c080", "c1bf", "e08080", "f0808080", "f5808080", "edbfbf", "fe", "ff", "e228a1", "f09f")) {
            assertNull(OtxtPayloadCodec.decode(envelope(hex(wire))), wire)
        }
    }

    @Test fun envelopeRequiresExactDomainVersionFlagsAndUnsignedLength() {
        val good = assertNotNull(OtxtPayloadCodec.encode("hello"))
        for (length in 0 until good.size) assertNull(OtxtPayloadCodec.decode(good.copyOf(length)))
        assertNull(OtxtPayloadCodec.decode(good + byteArrayOf(0)))
        for (i in 0..3) assertNull(OtxtPayloadCodec.decode(good.copyOf().also { it[i] = 0 }))
        for (v in 0..255) if (v != 1) assertNull(OtxtPayloadCodec.decode(good.copyOf().also { it[4] = v.toByte() }))
        for (v in 1..255) assertNull(OtxtPayloadCodec.decode(good.copyOf().also { it[5] = v.toByte() }))
        assertNull(OtxtPayloadCodec.decode(good.copyOf().also { it[6] = 0xff.toByte() }))
        assertNull(OtxtPayloadCodec.decode(good.copyOf().also { it[7] = 0xff.toByte() }))
    }

    @Test fun lowerBudgetsAndExtremeIntegersNeverWrapOrExpandTheNinetyByteLimit() {
        val one = assertNotNull(OtxtPayloadCodec.encode("x"))
        for (m in listOf(Int.MIN_VALUE, -1, 0, 60, 68)) {
            assertNull(OtxtPayloadCodec.encode("x", m))
            assertNull(OtxtPayloadCodec.decode(one, m))
        }
        assertContentEquals(one, OtxtPayloadCodec.encode("x", 69))
        assertEquals("x", OtxtPayloadCodec.decode(one, 69))
        assertNull(OtxtPayloadCodec.encode("xx", 69))
        assertNotNull(OtxtPayloadCodec.encode("x".repeat(86), 154))
        assertNull(OtxtPayloadCodec.encode("x".repeat(87), 154))
        assertNotNull(OtxtPayloadCodec.encode("x".repeat(90), Int.MAX_VALUE))
        assertNull(OtxtPayloadCodec.encode("x".repeat(91), Int.MAX_VALUE))
    }

    @Test fun preservesWhitespaceNullScalarNormalizationAndLineEndings() {
        for (text in listOf(" \t\r\n ", "\u0000", "é", "e\u0301", "a\r\nb\nc")) {
            val expected = envelope(text.toByteArray(Charsets.UTF_8))
            assertContentEquals(expected, OtxtPayloadCodec.encode(text))
            assertEquals(text, OtxtPayloadCodec.decode(expected))
        }
    }

    private fun hex(value: String): ByteArray = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    private fun envelope(text: ByteArray): ByteArray =
        byteArrayOf(0x4f, 0x54, 0x58, 0x54, 1, 0, (text.size ushr 8).toByte(), text.size.toByte()) + text
}
