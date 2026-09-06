package io.github.nbjelanovic.otclient

import kotlin.test.*

class V1SetupDraftTest {
    @Test fun emptyDraftHasNoImplicitRegionAndDiscoveryDefaultsOn() {
        val draft = requireNotNull(V1SetupDraft.create())
        val decoded = requireNotNull(V1SetupDraftCodec.decode(V1SetupDraftCodec.encode(draft)))
        assertNull(decoded.deviceName)
        assertNull(decoded.publicName)
        assertNull(decoded.radioRegion)
        assertTrue(decoded.publiclyDiscoverable)
    }

    @Test fun validatedFieldsRoundTripAndNamesAreRedacted() {
        val draft = requireNotNull(V1SetupDraft.create("Alex's device", "Alex", V1RadioRegion.US915, false))
        val decoded = requireNotNull(V1SetupDraftCodec.decode(V1SetupDraftCodec.encode(draft)))
        assertEquals("Alex's device", decoded.deviceName?.value)
        assertEquals("Alex", decoded.publicName?.value)
        assertEquals(V1RadioRegion.US915, decoded.radioRegion)
        assertFalse(decoded.publiclyDiscoverable)
        assertFalse(draft.toString().contains("Alex"))
    }

    @Test fun invalidNamesAndMalformedSurrogatesFailClosed() {
        assertNull(V1SetupDraft.create(deviceName = ""))
        assertNull(V1SetupDraft.create(deviceName = " x "))
        assertNull(V1SetupDraft.create(deviceName = "x".repeat(33)))
        assertNull(V1SetupDraft.create(publicName = "x".repeat(41)))
        assertNull(V1SetupDraft.create(deviceName = "bad\nname"))
        assertNull(V1SetupDraft.create(deviceName = "\uD800"))
    }

    @Test fun malformedVersionsFlagsEnumsAndTrailingBytesFailClosed() {
        val valid = V1SetupDraftCodec.encode(requireNotNull(V1SetupDraft.create(radioRegion = V1RadioRegion.US915)))
        assertNull(V1SetupDraftCodec.decode(valid.copyOf().also { it[0] = 0 }))
        assertNull(V1SetupDraftCodec.decode(valid.copyOf().also { it[4] = 2 }))
        assertNull(V1SetupDraftCodec.decode(valid.copyOf().also { it[5] = 0x40 }))
        assertNull(V1SetupDraftCodec.decode(valid.copyOf().also { it[6] = 99 }))
        assertNull(V1SetupDraftCodec.decode(valid + byteArrayOf(0)))
        assertNull(V1SetupDraftCodec.decode(ByteArray(V1SetupDraftCodec.MAX_BYTES + 1)))
        for (length in valid.indices) assertNull(V1SetupDraftCodec.decode(valid.copyOf(length)))
    }

    @Test fun invalidUtf8AndTruncatedNameLengthsAreRejected() {
        val encoded = V1SetupDraftCodec.encode(requireNotNull(V1SetupDraft.create(deviceName = "A")))
        assertNull(V1SetupDraftCodec.decode(encoded.copyOf().also { it[8] = 0xff.toByte() }))
        assertNull(V1SetupDraftCodec.decode(encoded.copyOf().also { it[6] = 0x7f }))
        assertNull(V1SetupDraftCodec.decode(encoded.copyOf().also { it[7] = 0 }))
    }

    @Test fun repositoryPersistsOnlyDraftAndContainsStorageFailures() {
        val storage = MemoryDraftStorage()
        val repository = V1SetupDraftRepository(storage)
        assertNull(repository.load())
        assertTrue(repository.save(requireNotNull(V1SetupDraft.create(deviceName = "Alex"))))
        assertEquals("Alex", repository.load()?.deviceName?.value)
        assertTrue(repository.clear())
        assertNull(repository.load())
        storage.fail = true
        assertNull(repository.load())
        assertFalse(repository.save(requireNotNull(V1SetupDraft.create())))
        assertFalse(repository.clear())
    }

    private class MemoryDraftStorage : V1SetupDraftStorage {
        var fail = false
        private var value: ByteArray? = null
        override fun read(): ByteArray? { check(!fail); return value?.copyOf() }
        override fun write(encoded: ByteArray): Boolean { check(!fail); value = encoded.copyOf(); return true }
        override fun clear(): Boolean { check(!fail); value = null; return true }
    }
}
