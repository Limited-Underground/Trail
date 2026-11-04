package io.github.nbjelanovic.otclient

import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** The source is a synthetic device boundary, never a production issuer or transport. */
class V1GroupSnapshotTest {
    @Test fun localSchemaRoundTripsOptionalSharingAndNoGroupTombstone() {
        val initial = snapshot(optional = true)
        val decoded = assertNotNull(V1GroupSnapshotCodec.decode(encoded(initial)))
        assertEquals(4L, decoded.revision)
        assertEquals("Owner", decoded.membership!!.profile.members.first().displayName.value)
        assertFalse(decoded.membership.profile.members.last().locationSharingEnabled)
        val removed = assertNotNull(V1GroupSnapshot.create(DEVICE, token(1), 5, null))
        assertNull(assertNotNull(V1GroupSnapshotCodec.decode(encoded(removed))).membership)
    }

    @Test fun creationReusesGroupConstraintsAndRequiresLocalMembership() {
        assertNull(V1GroupSnapshot.create("00", token(1), 1, null))
        assertNull(V1GroupSnapshot.create("AB", token(1), 1, null))
        assertNull(V1GroupSnapshot.create(DEVICE, token(1), 0, null))
        assertNull(V1GroupSnapshot.create(DEVICE, token(8), 1, snapshot().membership))
        val six = (1..6).map { member(it, it == 1) }
        assertNotNull(V1GroupProfile.create(token(1), V1GroupLocationRule.REQUIRED, six))
        assertNull(V1GroupProfile.create(token(1), V1GroupLocationRule.REQUIRED, six + member(7)))
        assertNull(V1GroupProfile.create(token(1), V1GroupLocationRule.REQUIRED, listOf(member(1, true), member(2, true))))
    }

    @Test fun malformedAndUnsupportedLocalRecordsAreRejectedEvenWithRecomputedChecksum() {
        val valid = encoded(snapshot())
        assertNull(V1GroupSnapshotCodec.decode(valid.copyOf(valid.size - 1)))
        assertNull(V1GroupSnapshotCodec.decode(valid + 0))
        assertNull(V1GroupSnapshotCodec.decode(ByteArray(V1GroupSnapshotCodec.MAX_BYTES + 1)))
        assertNull(V1GroupSnapshotCodec.decode(valid.copyOf().also { it[7] = 'f'.code.toByte() }))
        val firstMember = HEADER + 16 + 16 + 1 + 1
        for ((offset, value) in listOf(4 to 2, HEADER - 1 to 2, HEADER + 32 to 2,
            HEADER + 33 to 7, firstMember + 16 to 8)) {
            assertNull(V1GroupSnapshotCodec.decode(rewrite(valid) { it[offset] = value.toByte() }), "offset=$offset")
        }
        assertNull(V1GroupSnapshotCodec.decode(rewrite(valid) {
            for (offset in (HEADER - 9) until (HEADER - 1)) it[offset] = 0
        }))
        assertNull(V1GroupSnapshotCodec.decode(rewrite(valid) { it[firstMember + 18] = 0xc3.toByte() }))
        assertNull(V1GroupSnapshotCodec.decode(rewrite(valid) { it[firstMember + 16] = 1 })) // Required sharing OFF.
        val secondMember = firstMember + 16 + 1 + 1 + "Owner".length
        assertNull(V1GroupSnapshotCodec.decode(rewrite(valid) {
            it.copyInto(it, secondMember, firstMember, firstMember + 16)
        }))
        assertNull(V1GroupSnapshotCodec.decode(rewrite(valid) {
            it[HEADER + 16] = 99 // Administrator token no longer names the single administrator.
        }))
    }

    @Test fun durableBytesAndDiagnosticConfirmationCannotRestoreCurrentAuthority() {
        val storage = MemoryStorage(encoded(snapshot()))
        val context = context()
        val owner = V1GroupSnapshotCoordinator(context, { context }, storage)
        assertNull(owner.currentSnapshot)
        assertFalse(owner.refresh())
        assertEquals(V1GroupSnapshotRefusal.SOURCE_UNAVAILABLE, owner.firstRefusal)
        assertEquals(0, storage.reads)
        assertEquals(0, storage.writes)
        // No diagnostic confirmation input or command/activation API exists on this coordinator.
    }

    @Test fun freshDeviceReadPersistsAndReopensButNewOwnerStillNeedsFreshRead() {
        val storage = MemoryStorage()
        val fixture = Fixture()
        val first = fixture.owner(storage)
        assertTrue(first.refresh())
        assertEquals(1, storage.writes)
        assertEquals(2, storage.reads) // Existing bytes plus independent post-write readback.
        assertEquals(4L, assertNotNull(first.currentSnapshot).revision)
        first.close()
        assertEquals(1, fixture.closes)
        val reopened = Fixture().owner(storage)
        assertNull(reopened.currentSnapshot)
        assertTrue(reopened.refresh())
        assertEquals(1, storage.writes) // Identical fresh version is not rewritten.
    }

    @Test fun newerRevisionCanPersistAuthoritativeNoGroupStateWithoutResurrectingOldMembership() {
        val storage = MemoryStorage(encoded(snapshot()))
        val fixture = Fixture()
        fixture.read = { V1GroupDeviceRead.Admitted(fixture.context,
            assertNotNull(V1GroupSnapshot.create(DEVICE, token(1), 5, null))) }
        val owner = fixture.owner(storage)
        assertTrue(owner.refresh())
        assertNull(assertNotNull(owner.currentSnapshot).membership)
        val replay = Fixture().owner(storage)
        assertFalse(replay.refresh())
        assertEquals(V1GroupSnapshotRefusal.REVISION_ROLLBACK, replay.firstRefusal)
        assertEquals(1, storage.writes)
    }

    @Test fun staleRevisionAndSameVersionConflictDoNotOverwriteOrBecomeCurrent() {
        for ((fresh, expected) in listOf(
            snapshot(revision = 3) to V1GroupSnapshotRefusal.REVISION_ROLLBACK,
            snapshot(optional = true) to V1GroupSnapshotRefusal.REVISION_CONFLICT,
        )) {
            val stored = encoded(snapshot())
            val storage = MemoryStorage(stored)
            val fixture = Fixture()
            fixture.read = { V1GroupDeviceRead.Admitted(fixture.context, fresh) }
            val owner = fixture.owner(storage)
            assertFalse(owner.refresh())
            assertEquals(expected, owner.firstRefusal)
            assertNull(owner.currentSnapshot)
            assertContentEquals(stored, storage.bytes)
            assertEquals(0, storage.writes)
        }
    }

    @Test fun wrongDeviceMemberOrEqualValuedButDifferentContextIsRejectedBeforePersistence() {
        for (case in 0..3) {
            val fixture = Fixture()
            val storage = MemoryStorage(if (case == 3) encoded(snapshot(device = "55667788")) else null)
            fixture.read = { V1GroupDeviceRead.Admitted(
                if (case == 0) context() else fixture.context,
                when (case) {
                    1 -> snapshot(device = "55667788")
                    2 -> snapshot(local = token(2))
                    else -> snapshot()
                }) }
            val owner = fixture.owner(storage)
            assertFalse(owner.refresh())
            assertEquals(V1GroupSnapshotRefusal.BINDING_MISMATCH, owner.firstRefusal)
            assertEquals(0, storage.writes)
            assertNull(owner.currentSnapshot)
        }
    }

    @Test fun replacedSessionCannotReadSourceOrReusePreviouslyCurrentSnapshot() {
        val fixture = Fixture()
        val owner = fixture.owner(MemoryStorage())
        assertTrue(owner.refresh())
        fixture.current = context() // Same identifiers are insufficient; the session reference changed.
        assertNull(owner.currentSnapshot)
        assertEquals(V1GroupSnapshotRefusal.CONTEXT_CHANGED, owner.firstRefusal)
        assertFalse(owner.refresh())
        assertEquals(1, fixture.reads)
    }

    @Test fun sessionLossDuringSourceReadOrStorageReadStopsBeforeWrite() {
        for (duringSource in listOf(true, false)) {
            val fixture = Fixture()
            val storage = MemoryStorage()
            if (duringSource) fixture.read = {
                fixture.current = null
                V1GroupDeviceRead.Admitted(fixture.context, snapshot())
            } else storage.onRead = { fixture.current = null }
            val owner = fixture.owner(storage)
            assertFalse(owner.refresh())
            assertEquals(V1GroupSnapshotRefusal.CONTEXT_CHANGED, owner.firstRefusal)
            assertEquals(0, storage.writes)
        }
    }

    @Test fun sessionLossAfterWriteKeepsActualDiskStateButNeverCurrentAuthority() {
        val fixture = Fixture()
        val storage = MemoryStorage()
        storage.onWrite = { fixture.current = null }
        val owner = fixture.owner(storage)
        assertFalse(owner.refresh())
        assertEquals(1, storage.writes)
        assertNotNull(storage.bytes)
        assertNull(owner.currentSnapshot)
        assertEquals(V1GroupSnapshotRefusal.CONTEXT_CHANGED, owner.firstRefusal)
        val next = Fixture().owner(storage)
        assertNull(next.currentSnapshot)
        assertTrue(next.refresh())
    }

    @Test fun corruptRetainedDataRefusesWithoutRepairEraseOrAutomaticSecondRead() {
        val damaged = encoded(snapshot()).also { it[0] = 0 }
        val storage = MemoryStorage(damaged)
        val owner = Fixture().owner(storage)
        assertFalse(owner.refresh())
        assertEquals(V1GroupSnapshotRefusal.RETAINED_INVALID, owner.firstRefusal)
        assertFalse(owner.refresh())
        assertEquals(1, storage.reads)
        assertEquals(0, storage.writes)
        assertContentEquals(damaged, storage.bytes)
    }

    @Test fun storageConflictAndReadbackMismatchRefuseWithoutRetry() {
        for (conflict in listOf(true, false)) {
            val storage = MemoryStorage()
            if (conflict) storage.replaceAccepted = false else storage.onWrite = { storage.bytes = byteArrayOf(1) }
            val owner = Fixture().owner(storage)
            assertFalse(owner.refresh())
            assertEquals(if (conflict) V1GroupSnapshotRefusal.STORAGE_CONFLICT else
                V1GroupSnapshotRefusal.READBACK_MISMATCH, owner.firstRefusal)
            assertFalse(owner.refresh())
            assertEquals(1, storage.writes)
            assertNull(owner.currentSnapshot)
        }
    }

    @Test fun firstSourceAndStorageErrorsSurviveFailingCleanup() {
        for (failure in listOf(V1GroupSnapshotRefusal.SOURCE_REFUSED, V1GroupSnapshotRefusal.SOURCE_FAILED,
            V1GroupSnapshotRefusal.STORAGE_READ_FAILED, V1GroupSnapshotRefusal.STORAGE_WRITE_FAILED)) {
            val fixture = Fixture()
            fixture.closeFails = true
            val storage = MemoryStorage()
            when (failure) {
                V1GroupSnapshotRefusal.SOURCE_REFUSED -> fixture.read = { V1GroupDeviceRead.Refused }
                V1GroupSnapshotRefusal.SOURCE_FAILED -> fixture.read = { throw IOException("synthetic source failure") }
                V1GroupSnapshotRefusal.STORAGE_READ_FAILED -> storage.onRead = { throw IOException("synthetic read failure") }
                else -> storage.onWrite = { throw IOException("synthetic write failure") }
            }
            val owner = fixture.owner(storage)
            assertFalse(owner.refresh())
            owner.close()
            owner.close()
            assertEquals(failure, owner.firstRefusal)
            assertTrue(owner.cleanupFailed)
            assertEquals(1, fixture.closes)
            assertNull(owner.currentSnapshot)
        }
    }

    @Test fun sourceReentryAndCloseDuringReadCannotCommitOrPublish() {
        for (reenter in listOf(true, false)) {
            val fixture = Fixture()
            val storage = MemoryStorage()
            val owner = fixture.owner(storage)
            fixture.read = {
                assertNull(owner.currentSnapshot)
                if (reenter) assertFalse(owner.refresh()) else owner.close()
                V1GroupDeviceRead.Admitted(fixture.context, snapshot())
            }
            assertFalse(owner.refresh())
            assertNull(owner.currentSnapshot)
            assertEquals(0, storage.writes)
            if (reenter) assertEquals(V1GroupSnapshotRefusal.REENTRANT, owner.firstRefusal)
        }
    }

    @Test fun actualForeignThreadCannotOverlapRefreshReadStateOrCloseTheOwner() {
        val fixture = Fixture()
        val storage = MemoryStorage()
        val enteredSource = CountDownLatch(1)
        val foreignFinished = CountDownLatch(1)
        val foreignFailure = AtomicReference<Throwable?>()
        val owner = fixture.owner(storage) // This test thread owns the coordinator.
        fixture.read = {
            enteredSource.countDown()
            assertTrue(foreignFinished.await(5, TimeUnit.SECONDS))
            V1GroupDeviceRead.Admitted(fixture.context, snapshot())
        }
        val foreign = Thread {
            try {
                assertTrue(enteredSource.await(5, TimeUnit.SECONDS))
                for (operation in listOf<() -> Unit>(
                    { owner.refresh() }, { owner.close() }, { owner.currentSnapshot },
                    { owner.firstRefusal }, { owner.cleanupFailed },
                )) assertFailsWith<IllegalStateException> { operation() }
            } catch (failure: Throwable) { foreignFailure.set(failure) }
            finally { foreignFinished.countDown() }
        }
        foreign.start()
        try {
            assertTrue(owner.refresh())
            foreign.join(5_000)
            assertFalse(foreign.isAlive)
            assertNull(foreignFailure.get())
            assertNull(owner.firstRefusal)
            assertEquals(1, fixture.reads)
            assertEquals(1, storage.writes)
            assertEquals(0, fixture.closes)
            assertNotNull(owner.currentSnapshot)
        } finally {
            foreignFinished.countDown()
            foreign.join(5_000)
            owner.close() // Only the owner thread may close; exactly once.
        }
        assertEquals(1, fixture.closes)
        assertNull(owner.currentSnapshot)
    }

    @Test fun privateTokensAreDefensivelyCopiedAndDiagnosticsAreRedacted() {
        val token = token(1)
        token.persistenceBytes().fill(9)
        assertEquals(token(1), token)
        val group = assertNotNull(V1GroupToken.create(ByteArray(16) { 7 }))
        group.persistenceBytes().fill(9)
        assertEquals(V1GroupToken.create(ByteArray(16) { 7 }), group)
        assertFalse(snapshot().toString().contains(DEVICE))
        assertFalse(context().toString().contains(DEVICE))
    }

    private class Fixture {
        val context = context()
        var current: V1GroupDeviceContext? = context
        var reads = 0
        var closes = 0
        var closeFails = false
        var read: () -> V1GroupDeviceRead = { V1GroupDeviceRead.Admitted(context, snapshot()) }
        private fun readFixture() = read()
        fun owner(storage: V1GroupSnapshotStorage) = V1GroupSnapshotCoordinator(context, { current }, storage,
            object : V1GroupDeviceSource {
                override fun read(context: V1GroupDeviceContext): V1GroupDeviceRead { reads++; return readFixture() }
                override fun close() { closes++; if (closeFails) throw IOException("synthetic cleanup") }
            })
    }

    private class MemoryStorage(var bytes: ByteArray? = null) : V1GroupSnapshotStorage {
        var reads = 0
        var writes = 0
        var replaceAccepted = true
        var onRead: () -> Unit = {}
        var onWrite: () -> Unit = {}
        override fun read(): ByteArray? { reads++; onRead(); return bytes?.copyOf() }
        override fun replace(expected: ByteArray?, replacement: ByteArray): Boolean {
            writes++
            if (!replaceAccepted) return false
            assertTrue(bytes == null && expected == null || bytes != null && expected != null && bytes!!.contentEquals(expected))
            bytes = replacement.copyOf()
            onWrite()
            return true
        }
    }

    companion object {
        private const val DEVICE = "11223344"
        private const val HEADER = 4 + 1 + 2 + DEVICE.length + 16 + 8 + 1
        internal fun token(marker: Int) = requireNotNull(V1GroupMemberToken.create(ByteArray(16) { marker.toByte() }))
        private fun context() = V1GroupDeviceContext(DEVICE, token(1))
        private fun member(marker: Int, admin: Boolean = false, sharing: Boolean = true) = requireNotNull(
            V1GroupMember.create(token(marker), if (admin) "Owner" else "Member $marker", admin, sharing))
        internal fun snapshot(revision: Long = 4, device: String = DEVICE,
            local: V1GroupMemberToken = token(1), optional: Boolean = false): V1GroupSnapshot {
            val profile = requireNotNull(V1GroupProfile.create(token(1), if (optional)
                V1GroupLocationRule.MEMBER_OPTIONAL else V1GroupLocationRule.REQUIRED,
                listOf(member(1, true), member(2, sharing = !optional))))
            return requireNotNull(V1GroupSnapshot.create(device, local, revision, V1ActiveGroupMembership(
                requireNotNull(V1GroupToken.create(ByteArray(16) { 7 })), profile)))
        }
        internal fun encoded(snapshot: V1GroupSnapshot = snapshot()) = requireNotNull(V1GroupSnapshotCodec.encode(snapshot))
        private fun rewrite(bytes: ByteArray, action: (ByteArray) -> Unit): ByteArray {
            val body = bytes.copyOfRange(0, bytes.size - 32)
            action(body)
            return body + MessageDigest.getInstance("SHA-256").digest(body)
        }
    }
}
