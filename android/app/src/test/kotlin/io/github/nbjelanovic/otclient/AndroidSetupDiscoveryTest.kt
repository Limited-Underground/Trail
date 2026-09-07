package io.github.nbjelanovic.otclient

import kotlin.test.*
import java.util.UUID

class AndroidSetupDiscoveryTest {
    private fun name(code: String = "7K3P9Q") = byteArrayOf((code.length + 1).toByte(), 0x09) + code.toByteArray(Charsets.US_ASCII)
    private val flags = byteArrayOf(2, 1, 6)
    private val d1 = listOf(UUID.fromString(AndroidBlePlatformPlan.PAIRABLE_ADVERTISING_UUID))
    private val d0 = listOf(UUID.fromString(AndroidBlePlatformPlan.RETURNING_OWNER_ADVERTISING_UUID))
    private val uuidBytes = byteArrayOf(0xd1.toByte(), 0xb7.toByte(), 0x43, 0x1f, 0x4f, 0x0c, 0x10, 0xa2.toByte(),
        0xa3.toByte(), 0x4e, 0x6b, 0x7c, 0, 0x2a, 0x0f, 0x5e)
    private val service = byteArrayOf(17, 7) + uuidBytes
    private fun AndroidSetupDiscovery<String>.observe(endpoint: String, record: ByteArray?) = observe(endpoint, record, d1)

    @Test fun rawCompleteNameWithFlagsAndServiceProducesExactDisplayLabel() {
        val bytes = flags + service + name()
        assertEquals("Trail-7K3P9Q", assertIs<AndroidSetupAdvertisementLabel.Result.Label>(
            AndroidSetupAdvertisementLabel.decode(bytes)).value)
        assertEquals("Trail-7K3P9Q", assertIs<AndroidSetupAdvertisementLabel.Result.Label>(
            AndroidSetupAdvertisementLabel.decode(bytes + byteArrayOf(0, 0))).value)
    }

    @Test fun absentResponseOnlyRecordIsDistinctFromMalformedName() {
        listOf(null, byteArrayOf(), flags, service, byteArrayOf(0, 0)).forEach {
            assertEquals(AndroidSetupAdvertisementLabel.Result.Absent, AndroidSetupAdvertisementLabel.decode(it))
        }
        listOf(name("7K3P9"), name("7K3P9QA"), name("7K3P90"), name("7k3P9Q"),
            name("7K3P9I"), name("7K3P9O"), name("7K3P9 "), name("Trail-7K3P9Q"),
            byteArrayOf(7, 9, 55, 75, 51, 80, 57, 0x80.toByte()),
            byteArrayOf(7, 8) + "7K3P9Q".toByteArray(),
            name() + name(), byteArrayOf(8, 9, 55), name() + byteArrayOf(0, 1),
            ByteArray(513), byteArrayOf(1), name() + byteArrayOf(255.toByte(), 1)).forEach {
            assertEquals(AndroidSetupAdvertisementLabel.Result.Invalid, AndroidSetupAdvertisementLabel.decode(it))
        }
    }

    @Test fun distinctCodesAndRepeatedIdenticalAdvertisementsStaySelectable() {
        val scan = AndroidSetupDiscovery<String>(2)
        assertEquals("Trail-7K3P9Q", scan.observe("first", name()).label)
        assertEquals("Trail-8M4R2T", scan.observe("second", name("8M4R2T")).label)
        repeat(3) { assertEquals("Trail-7K3P9Q", scan.observe("first", name()).label) }
        assertFalse(scan.observe("first", flags, emptyList()).invalidated)
        assertFalse(scan.observe("first", null, emptyList()).invalidated)
        assertEquals("Trail-7K3P9Q", scan.observe("first", name()).label)
    }

    @Test fun duplicateVisibleLabelAcrossEndpointsInvalidatesTheWholeScanWithoutRecovery() {
        val scan = AndroidSetupDiscovery<String>(2)
        scan.observe("first", name())
        val result = scan.observe("second", name())
        assertTrue(result.invalidated)
        assertEquals(BleSetupLabelIssue.AMBIGUOUS, result.issue)
        assertNull(result.label)
        assertTrue(scan.observe("first", name()).invalidated)
        assertNull(scan.observe("second", name("8M4R2T")).label)
    }

    @Test fun changedOrExplicitlyMalformedKnownLabelRequiresFreshScan() {
        listOf(name("8M4R2T"), name("BAD"), flags + service).forEach { changed ->
            val scan = AndroidSetupDiscovery<String>(2)
            scan.observe("first", name())
            val result = scan.observe("first", changed)
            assertTrue(result.invalidated)
            assertEquals(BleSetupLabelIssue.CHANGED, result.issue)
            assertTrue(scan.observe("first", name()).invalidated)
        }
        assertEquals("Trail-8M4R2T", AndroidSetupDiscovery<String>(2).observe("first", name("8M4R2T")).label)
    }

    @Test fun actualLegacyAdvertisementAndResetResponseComposeWithoutChangingReceiptOrLabel() {
        val primary = flags + service + name()
        val receipt = 123uL
        val payload = assertNotNull(FactoryResetReceiptAdvertisementCodec.encode(receipt))
        val response = byteArrayOf(30, 0x21) + uuidBytes + payload
        assertEquals(29, primary.size)
        assertEquals(31, response.size)
        val scan = AndroidSetupDiscovery<String>(2)
        assertEquals("Trail-7K3P9Q", scan.observe("first", primary + response + ByteArray(2), d1).label)
        assertEquals(receipt, FactoryResetReceiptAdvertisementCodec.decode(response.copyOfRange(18, 31)))
        // A response-only report has service data, not the complete UUID-list primary discriminator.
        assertNull(scan.observe("first", response, emptyList()).label)
        assertEquals("Trail-7K3P9Q", scan.observe("first", primary, d1).label)
        assertNull(scan.observe("other", name(), d0).label)
        assertNull(scan.observe("other", name(), d0 + d1).label)
        assertEquals("Trail-8M4R2T", scan.observe("other", flags + service + name("8M4R2T"), d1).label)
    }

    @Test fun malformedNewEndpointCannotBecomeGenericChoiceAndCapacityCannotHideAmbiguity() {
        val scan = AndroidSetupDiscovery<String>(1)
        assertNull(scan.observe("bad", name("BAD")).label)
        assertEquals("Trail-7K3P9Q", scan.observe("first", name()).label)
        assertTrue(scan.observe("second", name("8M4R2T")).invalidated)
        assertNull(scan.observe("first", name()).label)
        val duplicateAtCapacity = AndroidSetupDiscovery<String>(1)
        duplicateAtCapacity.observe("first", name())
        assertEquals(BleSetupLabelIssue.AMBIGUOUS, duplicateAtCapacity.observe("second", name()).issue)
    }
}
