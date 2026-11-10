package io.github.nbjelanovic.otprotocol

import kotlin.test.*

class CompanionTextCarrierTest {
    private data class Vector(val id: String, val layer: String, val refusal: String, val wire: ByteArray)
    private fun vectors(): List<Vector> = checkNotNull(javaClass.getResourceAsStream("/text-carrier-v1-vectors.tsv"))
        .bufferedReader().useLines { lines -> lines.filter { it.isNotBlank() && !it.startsWith("#") }.map {
            val c = it.split('\t'); Vector(c[0], c[1], c[2], c[3].chunked(2).map { v -> v.toInt(16).toByte() }.toByteArray())
        }.toList() }
    @Test fun everySharedGoldenRoundTripsByteExact() {
        val cases = vectors().filter { it.layer == "golden" }
        assertEquals(13, cases.size)
        cases.forEach { v ->
            val record = assertNotNull(CompanionTextCarrierCodec.decode(v.wire, true), v.id)
            assertContentEquals(v.wire, CompanionTextCarrierCodec.encode(record, true), v.id)
            assertNull(CompanionTextCarrierCodec.decode(v.wire), v.id)
            assertNull(CompanionTextCarrierCodec.encode(record), v.id)
        }
        assertEquals(36, vectors().size)
        assertEquals(36, vectors().map { it.id }.toSet().size)
    }
    @Test fun everySharedStructuralFaultRefusesWithoutTruncation() {
        val cases = vectors().filter { it.layer == "shape" }
        assertEquals(14, cases.size)
        cases.forEach { assertNull(CompanionTextCarrierCodec.decode(it.wire, true), it.id) }
    }
    @Test fun allContextFaultBytesRemainMetadataNeverAuthority() {
        val cases = vectors().filter { it.layer == "context" }
        assertEquals(9, cases.size)
        // These are deliberately well-formed. Their fault is the *live* owner context in JSON,
        // not a byte-format fault. Actual adapter/runtime tests exercise those refusal gates.
        cases.forEach {
            val record = assertNotNull(CompanionTextCarrierCodec.decode(it.wire, true), it.id)
            assertContentEquals(it.wire, CompanionTextCarrierCodec.encode(record, true), it.id)
        }
    }
    @Test fun sendAndReceiveRespect89_90_91ByteBoundaries() {
        val ref = CompanionTextReference(1u, 2u, 3u)
        for (n in 89..91) for (text in listOf("x".repeat(n), "界".repeat(n / 3) + "x".repeat(n % 3), "🚲".repeat(n / 4) + "x".repeat(n % 4))) {
            for (record in listOf<CompanionTextRecord>(CompanionTextSend(ref, 3u, 2u, 1uL, text),
                CompanionTextReceived(ref, 1u, 1u, 1uL, text))) {
                val encoded = CompanionTextCarrierCodec.encode(record, true)
                if (n == 91) assertNull(encoded) else {
                    assertEquals(58 + n, assertNotNull(encoded).size)
                    assertEquals(record, CompanionTextCarrierCodec.decode(encoded, true))
                }
            }
        }
        assertEquals(151, assertNotNull(CompanionTextCarrierCodec.encode(CompanionTextSend(ref, 3u, 2u, 1uL, "界".repeat(30)), true)).size + 3)
    }
    @Test fun emptyAndInvalidUnicodeNeverEncode() {
        val ref = CompanionTextReference(1u, 2u, 3u)
        for (text in listOf("", "\uD800", "\uDC00", "x\uD800")) {
            assertNull(CompanionTextCarrierCodec.encode(CompanionTextSend(ref, 3u, 2u, 1uL, text), true))
            assertNull(CompanionTextCarrierCodec.encode(CompanionTextReceived(ref, 1u, 1u, 1uL, text), true))
        }
    }
    @Test fun everyHeaderAndPrefixDomainIsStrict() {
        val good = vectors().first().wire
        for ((offset, values) in listOf(4 to listOf(1), 5 to listOf(0, 5), 7 to listOf(1),
            16 to listOf(1), 17 to listOf(0, 2), 24 to listOf(0, 2), 25 to listOf(1))) {
            for (value in values) assertNull(CompanionTextCarrierCodec.decode(good.copyOf().also { it[offset] = value.toByte() }, true))
        }
        for (offset in listOf(0, 1, 2, 3, 20, 21, 22, 23)) assertNull(CompanionTextCarrierCodec.decode(good.copyOf().also { it[offset] = 0 }, true))
        for ((offset, size) in listOf(8 to 4, 12 to 4, 26 to 4, 30 to 8, 42 to 8))
            assertNull(CompanionTextCarrierCodec.decode(good.copyOf().also { b -> repeat(size) { b[offset + it] = 0 } }, true))
        assertNull(CompanionTextCarrierCodec.decode(good + 0, true))
    }
    @Test fun dispositionsIdentityTriplesAndReasonDomainsRefuseClosed() {
        val ref = CompanionTextReference(1u, 2u, 3u)
        val good = CompanionTextResult(ref, 3u, 1uL, 1, 0, 100uL, 1u, 1uL)
        for (bad in listOf(good.copy(receipt=0uL), good.copy(messageId=0u), good.copy(counter=0uL),
            good.copy(disposition=257), good.copy(reason=256), good.copy(disposition=0),
            good.copy(disposition=2,reason=1), good.copy(disposition=3,reason=0), good.copy(disposition=3,reason=1,receipt=0uL)))
            assertNull(CompanionTextCarrierCodec.encode(bad, true))
        assertNotNull(CompanionTextCarrierCodec.encode(good.copy(disposition=2,reason=4,receipt=0uL,messageId=0u,counter=0uL), true))
        assertNotNull(CompanionTextCarrierCodec.encode(good.copy(disposition=3,reason=12,receipt=0uL,messageId=0u,counter=0uL), true))
        for (reason in 1..15) {
            val refusal = good.copy(disposition=2,reason=reason,receipt=0uL,messageId=0u,counter=0uL)
            val wire = assertNotNull(CompanionTextCarrierCodec.encode(refusal,true))
            assertEquals(reason,wire[47].toInt() and 255)
            assertEquals(refusal,CompanionTextCarrierCodec.decode(wire,true))
        }
        val delivery = CompanionTextDelivery(ref, 1u, 3u, 1uL, 100uL, 1u, 1uL, 3, 0)
        for (bad in listOf(delivery.copy(phase=4),delivery.copy(phase=259),delivery.copy(reason=256),delivery.copy(phase=5),
            delivery.copy(originExchange=0u),delivery.copy(receipt=0uL),delivery.copy(operation=0uL)))
            assertNull(CompanionTextCarrierCodec.encode(bad, true))
    }
    @Test fun textAndContextAreRedactedFromDiagnosticStrings() {
        val record = CompanionTextSend(CompanionTextReference(1u, 2u, 3u), 3u, 2u, 1uL, "private phrase")
        assertFalse(record.toString().contains(record.text))
    }
}
