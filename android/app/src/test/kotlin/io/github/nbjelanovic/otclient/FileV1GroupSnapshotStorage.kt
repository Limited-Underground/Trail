package io.github.nbjelanovic.otclient

import java.io.ByteArrayOutputStream
import java.io.FileOutputStream
import java.nio.channels.FileChannel
import java.nio.channels.OverlappingFileLockException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.NoSuchFileException
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption

/**
 * Dormant ordinary-file host adapter, not an Android protected-store/Keystore implementation.
 * Caller supplies an existing private directory. No directory creation, migration or auto-clear.
 * Sync + atomic replacement + full reopen is host persistence evidence, not power-loss acceptance.
 * The OS lock serializes cooperating owners; it is not a hostile-filesystem security boundary.
 */
internal class FileV1GroupSnapshotStorage(private val path: Path) : V1GroupSnapshotStorage {
    init { require(path.isAbsolute && path.parent != null) }

    override fun read(): ByteArray? {
        check(!Files.isSymbolicLink(path) && !Files.isSymbolicLink(path.parent))
        return try {
            Files.newInputStream(path, LinkOption.NOFOLLOW_LINKS).use { input ->
                val result = ByteArrayOutputStream()
                val buffer = ByteArray(256)
                while (true) {
                    val count = input.read(buffer, 0, minOf(buffer.size, V1GroupSnapshotCodec.MAX_BYTES + 1 - result.size()))
                    if (count < 0) break
                    check(count > 0)
                    result.write(buffer, 0, count)
                    check(result.size() <= V1GroupSnapshotCodec.MAX_BYTES)
                }
                result.toByteArray()
            }
        } catch (_: NoSuchFileException) { null }
    }

    override fun replace(expected: ByteArray?, replacement: ByteArray): Boolean {
        require(V1GroupSnapshotCodec.decode(replacement) != null)
        val intended = replacement.copyOf()
        val previous = expected?.copyOf()
        check(!Files.isSymbolicLink(path) && !Files.isSymbolicLink(path.parent))
        val lockPath = path.resolveSibling(path.fileName.toString() + ".lock")
        return FileChannel.open(lockPath, StandardOpenOption.CREATE, StandardOpenOption.WRITE,
            LinkOption.NOFOLLOW_LINKS).use { channel ->
            val lock = try { channel.tryLock() } catch (_: OverlappingFileLockException) { null }
                ?: return false
            lock.use {
                val actual = read()
                if (!(actual == null && previous == null || actual != null && previous != null && actual.contentEquals(previous)))
                    return false
                val temporary = Files.createTempFile(path.parent, path.fileName.toString(), ".pending")
                var firstError: Throwable? = null
                try {
                    FileOutputStream(temporary.toFile()).use { out -> out.write(intended); out.fd.sync() }
                    // No fallback to non-atomic replacement on an unsupported filesystem.
                    Files.move(temporary, path, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
                    true
                } catch (error: Throwable) {
                    firstError = error
                    throw error
                } finally {
                    try { Files.deleteIfExists(temporary) }
                    catch (cleanup: Throwable) { if (firstError != null) firstError.addSuppressed(cleanup) else throw cleanup }
                }
            }
        }
    }
}
