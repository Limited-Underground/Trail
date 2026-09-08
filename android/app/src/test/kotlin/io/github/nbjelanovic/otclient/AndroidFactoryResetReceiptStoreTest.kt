package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.test.assertFalse

class AndroidFactoryResetReceiptStoreTest {
    @Test
    fun stagePersistsOnlyReceiptAndCoherentBoundsThenExpiresExactly() {
        val storage = InMemoryReceiptStorage()
        var now = 5_000L
        val receipt = 0xf122334455667788uL
        val store = AndroidFactoryResetReceiptStore(storage, { now }, { receipt })

        assertEquals(receipt, store.stage())
        assertEquals(
            mapOf(
                AndroidFactoryResetReceiptStore.RECEIPT_KEY to receipt.toLong(),
                AndroidFactoryResetReceiptStore.ISSUED_AT_KEY to 5_000L,
                AndroidFactoryResetReceiptStore.EXPIRY_KEY to
                    (5_000L + ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS),
            ),
            storage.values,
        )

        now = 5_000L + ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS - 1L
        assertEquals(receipt, store.load())
        now += 1L
        assertNull(store.load())
        assertTrue(storage.values.isEmpty())
    }

    @Test
    fun clockRollbackClearsThePendingReceiptFailClosed() {
        val storage = InMemoryReceiptStorage()
        var now = 9_000L
        val store = AndroidFactoryResetReceiptStore(storage, { now }, { 7uL })
        assertEquals(7uL, store.stage())

        now = 8_999L

        assertNull(store.load())
        assertTrue(storage.values.isEmpty())
    }

    @Test
    fun corruptFutureOrPartialBoundsCannotExtendReceiptLifetime() {
        val storage = InMemoryReceiptStorage()
        var now = 12_000L
        val store = AndroidFactoryResetReceiptStore(storage, { now }, { 11uL })
        assertEquals(11uL, store.stage())
        storage.values[AndroidFactoryResetReceiptStore.EXPIRY_KEY] =
            12_000L + ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS + 1L

        assertNull(store.load())
        assertTrue(storage.values.isEmpty())

        now = 13_000L
        assertEquals(11uL, store.stage())
        storage.values.remove(AndroidFactoryResetReceiptStore.ISSUED_AT_KEY)
        assertNull(store.load())
        assertTrue(storage.values.isEmpty())
    }

    @Test
    fun verifiedResetAtomicallyPersistsFreshSetupAcrossStoreRestartUntilReady() {
        val storage = InMemoryReceiptStorage()
        val first = AndroidFactoryResetReceiptStore(storage, { 5000L }, { 7uL })
        first.stage()
        assertFalse(first.completeVerified(8uL))
        assertFalse(first.requiresFreshSetup())
        assertTrue(first.completeVerified(7uL))
        assertNull(first.load())
        assertEquals(mapOf(AndroidFactoryResetReceiptStore.FRESH_SETUP_KEY to 1L), storage.values)
        val restarted = AndroidFactoryResetReceiptStore(storage, { 6000L }, { 8uL })
        assertTrue(restarted.requiresFreshSetup())
        assertTrue(restarted.authenticatedReady())
        assertFalse(first.requiresFreshSetup())
    }
    @Test
    fun failedCompletionKeepsPendingReceiptAndNeverClaimsFreshSetup() {
        val storage = InMemoryReceiptStorage()
        val store = AndroidFactoryResetReceiptStore(storage, { 5000L }, { 7uL })
        store.stage()
        storage.failWrite = true
        assertFalse(store.completeVerified(7uL))
        assertEquals(7uL, store.load())
        assertFalse(store.requiresFreshSetup())
        storage.failWrite = false
        assertTrue(store.clearExact(7uL))
        assertFalse(store.requiresFreshSetup())
    }
    @Test
    fun failedReadyCommitRetainsConservativeFreshSetupInMemory() {
        val storage = InMemoryReceiptStorage()
        val store = AndroidFactoryResetReceiptStore(storage, { 5000L }, { 7uL })
        store.stage()
        assertTrue(store.completeVerified(7uL))
        storage.failRemoveAfterMemoryUpdate = true
        assertFalse(store.authenticatedReady())
        assertTrue(store.requiresFreshSetup())
        storage.failRemoveAfterMemoryUpdate = false
        assertTrue(store.authenticatedReady())
        assertFalse(store.requiresFreshSetup())
    }

    private class InMemoryReceiptStorage : FactoryResetReceiptStorage {
        val values = linkedMapOf<String, Long>()

        override fun readLong(key: String): Long? = values[key]

        var failWrite = false
        var failRemoveAfterMemoryUpdate = false
        override fun writeLongs(values: Map<String, Long>, removeKeys: Set<String>): Boolean {
            if (failWrite) return false
            removeKeys.forEach(this.values::remove)
            this.values.putAll(values)
            return true
        }

        override fun remove(keys: Set<String>): Boolean {
            keys.forEach(values::remove)
            return !failRemoveAfterMemoryUpdate
        }
    }
}
