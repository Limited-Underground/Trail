package io.github.nbjelanovic.otclient

import kotlin.test.*

class V1SynchronizedInboxTest {
    private val device = requireNotNull(V1InboxDeviceToken.create(ByteArray(16) { 1 }))
    private val policy = V1InboxPolicy(2, 40, 100, V1InboxFullPolicy.REJECT_NEW)
    private fun key(id: UInt = 1u) = V1InboxKey(ULong.MAX_VALUE, 2uL, UInt.MAX_VALUE, 4uL, id)
    private fun group(id: UInt = 1u) = V1InboxAccess.Group(device, key(id))
    private fun peer(n: Byte = 1) = requireNotNull(V1DirectPeerToken.create(ByteArray(16) { n }))
    private fun contact() = V1DirectContact.available(peer(), requireNotNull(V1PublicName.create("Alex")))
    private fun accepted(): V1DirectContact {
        val pending = assertIs<V1DirectContactResult.Accepted>(contact().requestChat()).contact
        return assertIs<V1DirectContactResult.Accepted>(pending.markAcceptedByPeer()).contact
    }
    private class Store : V1InboxStorage {
        var snapshot: V1InboxSnapshot? = null
        var outcome = V1InboxCommit.VERIFIED
        var applyUncertain = false
        var writes = 0
        var onCommit: (() -> Unit)? = null
        var readError: Exception? = null
        var writeError: Exception? = null
        override fun read(): V1InboxLoad {
            readError?.let { throw it }
            return snapshot?.let(V1InboxLoad::Found) ?: V1InboxLoad.Empty
        }
        override fun commit(snapshot: V1InboxSnapshot): V1InboxCommit {
            writes++
            onCommit?.invoke()
            writeError?.let { throw it }
            if (outcome == V1InboxCommit.VERIFIED || applyUncertain) this.snapshot = snapshot
            return outcome
        }
    }
    private fun boot(store: Store = Store()): V1SynchronizedInbox =
        V1SynchronizedInbox(device, policy, store).also { assertEquals(V1InboxFailure.NONE, it.restore(0, null)) }

    @Test fun commitPrecedesExposureAndDuplicateIdentitySurvivesRestart() {
        val store = Store()
        val inbox = boot(store)
        store.outcome = V1InboxCommit.FAILED_BEFORE_COMMIT
        val failed = inbox.importMessage(group(), "hello", null, 1)
        assertEquals(V1InboxFailure.STORAGE_FAILURE, failed.failure)
        assertNull(failed.message)
        assertNull(failed.receipt)
        assertTrue(store.snapshot!!.records.isEmpty())
        store.outcome = V1InboxCommit.VERIFIED
        val imported = inbox.importMessage(group(), "hello", null, 2)
        assertFalse(imported.message!!.opened)
        assertFalse(imported.message.locationSharingEnabled)
        val duplicate = inbox.importMessage(group(), "hello", null, 3)
        assertTrue(duplicate.duplicate)
        assertEquals(imported.receipt, duplicate.receipt)
        assertEquals(1, store.snapshot!!.records.size)
        val restarted = V1SynchronizedInbox(device, policy, store)
        assertEquals(V1InboxFailure.TIME_UNCERTAIN, restarted.restore(0, null))
        assertEquals(V1InboxFailure.NONE, restarted.restore(0, 1))
        assertEquals(imported.receipt, restarted.importMessage(group(), "hello", null, 1).receipt)
        assertEquals(V1InboxFailure.CONFLICT, restarted.importMessage(group(), "different", null, 2).failure)
    }

    @Test fun typedAccessRequiresAcceptedMappedPeerAndRechecksPresentation() {
        val inbox = boot()
        val pending = V1InboxAccess.Direct(device, key(), contact())
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.importMessage(pending, "hello", peer(), 0).failure)
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.importMessage(group(), "hello", peer(), 0).failure)
        val approved = V1InboxAccess.Direct(device, key(), accepted())
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.importMessage(approved, "hello", peer(2), 0).failure)
        assertEquals(V1InboxFailure.NONE, inbox.importMessage(approved, "hello", peer(), 0).failure)
        val blocked = assertIs<V1DirectContactResult.Accepted>(accepted().block()).contact
        val denied = V1InboxAccess.Direct(device, key(), blocked)
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.present(denied, 1).failure)
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.opened(denied, 1).failure)
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.pendingReceipts(denied, 1).failure)
        assertEquals(V1InboxFailure.UNAUTHORIZED, inbox.present(group(), 1).failure)
        val wrongDevice = requireNotNull(V1InboxDeviceToken.create(ByteArray(16) { 2 }))
        assertEquals(V1InboxFailure.UNAUTHORIZED,
            inbox.present(V1InboxAccess.Direct(wrongDevice, key(), accepted()), 1).failure)
    }

    @Test fun importedAndExplicitReadRemainSeparateWithExactHandoff() {
        val inbox = boot()
        val imported = inbox.importMessage(group(), "hello", null, 0)
        assertFalse(inbox.present(group(), 1).message!!.opened)
        assertEquals(listOf(V1InboxReceiptKind.IMPORTED), inbox.pendingReceipts(group(), 1).receipts.map { it.kind })
        val opened = inbox.opened(group(), 2)
        assertTrue(opened.message!!.opened)
        assertEquals(V1InboxReceiptKind.READ, opened.receipt!!.kind)
        assertEquals(2, inbox.pendingReceipts(group(), 2).receipts.size)
        assertEquals(V1InboxFailure.IMPORT_PENDING, inbox.confirmHandoff(group(), opened.receipt, 2))
        assertEquals(V1InboxFailure.INVALID, inbox.confirmHandoff(group(), imported.receipt!!.copy(key = key(2u)), 3))
        assertEquals(V1InboxFailure.NONE, inbox.confirmHandoff(group(), imported.receipt, 3))
        assertEquals(V1InboxFailure.MISSING, inbox.confirmHandoff(group(), imported.receipt, 3))
        assertEquals(listOf(opened.receipt), inbox.pendingReceipts(group(), 3).receipts)
        assertEquals(V1InboxFailure.NONE, inbox.confirmHandoff(group(), opened.receipt, 4))
        assertEquals(V1InboxFailure.NONE, inbox.pendingReceipts(group(), 4).failure)
        assertTrue(inbox.pendingReceipts(group(), 4).receipts.isEmpty())
    }

    @Test fun expiredContentHidesButPendingReceiptsRetainBoundedTombstone() {
        val store = Store()
        val inbox = boot(store)
        val imported = inbox.importMessage(group(), "hello", null, 0)
        val opened = inbox.opened(group(), 0)
        assertEquals(V1InboxFailure.EXPIRED, inbox.present(group(), 100).failure)
        assertEquals(V1InboxFailure.EXPIRED, inbox.opened(group(), 100).failure)
        assertEquals(2, inbox.pendingReceipts(group(), 100).receipts.size)
        assertEquals(V1InboxFailure.EXPIRED, inbox.importMessage(group(), "hello", null, 100).failure)
        assertEquals(V1InboxFailure.NONE, inbox.confirmHandoff(group(), imported.receipt!!, 100))
        val restarted = V1SynchronizedInbox(device, policy, store)
        assertEquals(V1InboxFailure.NONE, restarted.restore(0, 1))
        assertEquals(listOf(opened.receipt), restarted.pendingReceipts(group(), 0).receipts)
        assertEquals(V1InboxFailure.NONE, restarted.confirmHandoff(group(), opened.receipt!!, 1))
        assertTrue(store.snapshot!!.records.isEmpty())
    }

    @Test fun boundsUtf8AndFullPreservePriorRecords() {
        val inbox = boot()
        for (text in listOf("", " ", "x".repeat(41), "é".repeat(21), "bad\ttext", "\uD800", "bad\u0085")) {
            assertEquals(V1InboxFailure.INVALID, inbox.importMessage(group(), text, null, 0).failure)
        }
        assertEquals(V1InboxFailure.NONE, inbox.importMessage(group(), "é".repeat(10), null, 0).failure)
        assertEquals(V1InboxFailure.FULL, inbox.importMessage(group(2u), "x".repeat(21), null, 0).failure)
        assertEquals(V1InboxFailure.NONE, inbox.importMessage(group(2u), "x", null, 0).failure)
        assertEquals(V1InboxFailure.FULL, inbox.importMessage(group(3u), "x", null, 0).failure)
        assertEquals("é".repeat(10), inbox.present(group(), 0).message!!.text)
        assertEquals(V1InboxFailure.FULL, inbox.importMessage(group(3u), "x", null, 100).failure)
    }

    @Test fun ambiguousWriteAndOrdinaryExceptionsFailClosed() {
        val store = Store()
        val inbox = boot(store)
        store.outcome = V1InboxCommit.UNCERTAIN
        store.applyUncertain = true
        val failed = inbox.importMessage(group(), "hello", null, 0)
        assertEquals(V1InboxFailure.RECOVERY_REQUIRED, failed.failure)
        assertNull(failed.message)
        assertNull(failed.receipt)
        assertEquals(V1InboxFailure.RECOVERY_REQUIRED, inbox.present(group(), 0).failure)
        store.outcome = V1InboxCommit.VERIFIED
        val restarted = V1SynchronizedInbox(device, policy, store)
        assertEquals(V1InboxFailure.NONE, restarted.restore(0, 0))
        assertTrue(restarted.importMessage(group(), "hello", null, 0).duplicate)
        store.writeError = IllegalStateException("failed")
        assertEquals(V1InboxFailure.RECOVERY_REQUIRED, restarted.opened(group(), 1).failure)
        val unreadable = Store().also { it.readError = IllegalStateException("failed") }
        assertEquals(V1InboxFailure.STORAGE_FAILURE, V1SynchronizedInbox(device, policy, unreadable).restore(0, 0))
    }

    @Test fun corruptSnapshotClockRegressionAndGenerationExhaustion() {
        val store = Store()
        val inbox = boot(store)
        inbox.importMessage(group(), "hello", null, 10)
        assertEquals(V1InboxFailure.CLOCK_FAULT, inbox.present(group(), 9).failure)
        assertEquals(V1InboxFailure.RECOVERY_REQUIRED, inbox.present(group(), 11).failure)
        val original = store.snapshot!!
        store.snapshot = V1InboxSnapshot(device, policy, original.records, 1uL, 99)
        assertEquals(V1InboxFailure.CORRUPT, V1SynchronizedInbox(device, policy, store).restore(0, 0))
        store.snapshot = V1InboxSnapshot(device, policy, original.records + original.records, 1uL)
        assertEquals(V1InboxFailure.CORRUPT, V1SynchronizedInbox(device, policy, store).restore(0, 0))
        store.snapshot = V1InboxSnapshot(device, policy, original.records, ULong.MAX_VALUE)
        assertEquals(V1InboxFailure.RECOVERY_REQUIRED, V1SynchronizedInbox(device, policy, store).restore(0, 0))
    }

    @Test fun defensiveCopiesRedactionAndBackendReentry() {
        val bytes = ByteArray(16) { 1 }
        val copied = requireNotNull(V1InboxDeviceToken.create(bytes))
        bytes.fill(2)
        assertEquals(device, copied)
        val store = Store()
        val inbox = boot(store)
        store.onCommit = { assertEquals(V1InboxFailure.UNAVAILABLE, inbox.present(group(), 0).failure) }
        val imported = inbox.importMessage(group(), "private-text", null, 0)
        assertFalse(imported.toString().contains("private-text"))
        assertFalse(imported.message.toString().contains("private-text"))
        assertFalse(store.snapshot.toString().contains("private-text"))
        val records = store.snapshot!!.records.toMutableList()
        val snapshot = V1InboxSnapshot(device, policy, records, 1uL)
        records.clear()
        assertEquals(1, snapshot.records.size)
        assertFailsWith<UnsupportedOperationException> { (snapshot.records as MutableList).clear() }
    }

    @Test fun failedOpenAndHandoffRetainUnreadAndPendingReceiptState() {
        val store = Store()
        val inbox = boot(store)
        val imported = inbox.importMessage(group(), "hello", null, 0)
        store.outcome = V1InboxCommit.FAILED_BEFORE_COMMIT
        val opened = inbox.opened(group(), 1)
        assertEquals(V1InboxFailure.STORAGE_FAILURE, opened.failure)
        assertNull(opened.message)
        assertNull(opened.receipt)
        assertFalse(inbox.present(group(), 1).message!!.opened)
        assertEquals(V1InboxFailure.STORAGE_FAILURE, inbox.confirmHandoff(group(), imported.receipt!!, 1))
        assertEquals(listOf(imported.receipt), inbox.pendingReceipts(group(), 1).receipts)
    }

    @Test fun invalidPolicyAndNegativeElapsedNeverInitialize() {
        val invalid = policy.copy(full = V1InboxFullPolicy.UNSPECIFIED)
        assertEquals(V1InboxFailure.INVALID, V1SynchronizedInbox(device, invalid, Store()).restore(0, null))
        val store = Store()
        boot(store).importMessage(group(), "hello", null, 0)
        val restored = V1SynchronizedInbox(device, policy, store)
        assertEquals(V1InboxFailure.INVALID, restored.restore(0, -1))
        assertEquals(V1InboxFailure.UNAVAILABLE, restored.present(group(), 0).failure)
        assertEquals(V1InboxFailure.NONE, restored.restore(Long.MAX_VALUE - 1, 0))
        assertEquals(V1InboxFailure.NONE, restored.present(group(), Long.MAX_VALUE).failure)
    }

    @Test fun receiptQueriesExposeFailureAndOwnImmutableLists() {
        val store = Store()
        val inbox = V1SynchronizedInbox(device, policy, store)
        assertEquals(V1InboxFailure.UNAVAILABLE, inbox.pendingReceipts(group(), 0).failure)
        assertEquals(V1InboxFailure.NONE, inbox.restore(0, null))
        assertEquals(V1InboxFailure.MISSING, inbox.pendingReceipts(group(), 0).failure)
        inbox.importMessage(group(), "hello", null, 0)
        val pending = inbox.pendingReceipts(group(), 0)
        assertEquals(V1InboxFailure.NONE, pending.failure)
        assertFailsWith<UnsupportedOperationException> { (pending.receipts as MutableList).clear() }
        val mutable = pending.receipts.toMutableList()
        val copied = V1InboxReceiptsResult(V1InboxFailure.NONE, mutable)
        mutable.clear()
        assertEquals(1, copied.receipts.size)
        store.outcome = V1InboxCommit.UNCERTAIN
        inbox.opened(group(), 1)
        val faulted = inbox.pendingReceipts(group(), 1)
        assertEquals(V1InboxFailure.RECOVERY_REQUIRED, faulted.failure)
        assertTrue(faulted.receipts.isEmpty())
    }
}
