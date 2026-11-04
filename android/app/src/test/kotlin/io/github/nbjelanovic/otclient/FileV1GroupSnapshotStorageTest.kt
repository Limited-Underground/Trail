package io.github.nbjelanovic.otclient

import java.nio.channels.FileChannel
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFails
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class FileV1GroupSnapshotStorageTest {
    @Test fun actualFileIsReopenedComparedAndAtomicallyAdvancedWithoutStaleOverwrite() = isolated { dir ->
        val path = dir.resolve("snapshot")
        val bytes = V1GroupSnapshotTest.encoded()
        val storage = FileV1GroupSnapshotStorage(path)
        assertNull(storage.read())
        assertTrue(storage.replace(null, bytes))
        assertContentEquals(bytes, FileV1GroupSnapshotStorage(path).read())
        val newer = V1GroupSnapshotTest.encoded(V1GroupSnapshotTest.snapshot(revision = 5))
        assertFalse(storage.replace(null, newer))
        assertTrue(storage.replace(bytes, newer))
        assertFalse(storage.replace(bytes, bytes))
        assertContentEquals(newer, FileV1GroupSnapshotStorage(path).read())
        assertEquals(listOf("snapshot", "snapshot.lock"), filenames(dir))
    }

    @Test fun reloadedFileStillCannotCreateLiveAuthorityWithoutTheUnavailableDeviceAdapter() = isolated { dir ->
        val storage = FileV1GroupSnapshotStorage(dir.resolve("snapshot"))
        assertTrue(storage.replace(null, V1GroupSnapshotTest.encoded()))
        val context = V1GroupDeviceContext("11223344", V1GroupSnapshotTest.token(1))
        val owner = V1GroupSnapshotCoordinator(context, { context }, storage)
        assertNull(owner.currentSnapshot)
        assertFalse(owner.refresh())
        assertEquals(V1GroupSnapshotRefusal.SOURCE_UNAVAILABLE, owner.firstRefusal)
        owner.close()
        assertNotNull(storage.read())
    }

    @Test fun corruptedOrOversizedFileIsRetainedAndNeverAutomaticallyRepaired() = isolated { dir ->
        val path = dir.resolve("snapshot")
        val storage = FileV1GroupSnapshotStorage(path)
        val corrupt = byteArrayOf(7, 8)
        Files.write(path, corrupt)
        assertNull(V1GroupSnapshotCodec.decode(assertNotNull(storage.read())))
        assertFalse(storage.replace(null, V1GroupSnapshotTest.encoded()))
        assertContentEquals(corrupt, Files.readAllBytes(path))
        val oversized = ByteArray(V1GroupSnapshotCodec.MAX_BYTES + 1)
        Files.write(path, oversized)
        assertFails { storage.read() }
        assertContentEquals(oversized, Files.readAllBytes(path))
    }

    @Test fun exclusiveWriterConflictRefusesOnceWithoutLeavingPartialSnapshot() = isolated { dir ->
        val path = dir.resolve("snapshot")
        FileChannel.open(dir.resolve("snapshot.lock"), StandardOpenOption.CREATE, StandardOpenOption.WRITE).use { channel ->
            channel.lock().use {
                assertFalse(FileV1GroupSnapshotStorage(path).replace(null, V1GroupSnapshotTest.encoded()))
                assertFalse(Files.exists(path))
            }
        }
        assertEquals(listOf("snapshot.lock"), filenames(dir))
    }

    @Test fun invalidPayloadOrUnavailableDirectoryNeverCreatesAReplacement() = isolated { dir ->
        val path = dir.resolve("snapshot")
        assertFails { FileV1GroupSnapshotStorage(path).replace(null, byteArrayOf(1)) }
        assertFalse(Files.exists(path))
        val missing = dir.resolve("absent").resolve("snapshot")
        assertFails { FileV1GroupSnapshotStorage(missing).replace(null, V1GroupSnapshotTest.encoded()) }
        assertFalse(Files.exists(missing.parent))
        assertFails { FileV1GroupSnapshotStorage(Path.of("relative")) }
    }

    private fun filenames(dir: Path): List<String> = Files.list(dir).use { stream ->
        stream.map { it.fileName.toString() }.sorted().toList()
    }

    private fun isolated(test: (Path) -> Unit) {
        val dir = Files.createTempDirectory("ot0305-synthetic-").toAbsolutePath()
        try { test(dir) }
        finally {
            Files.list(dir).use { stream -> stream.forEach {
                check(it.parent == dir)
                Files.delete(it)
            } }
            Files.delete(dir)
        }
    }
}
