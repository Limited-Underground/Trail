package io.github.nbjelanovic.otclient

import android.content.Context
import android.util.AtomicFile
import java.io.FileNotFoundException

/**
 * Unwired app-private atomic storage adapter. The stored bytes are not encrypted,
 * so production controllers must not instantiate this for real message content.
 */
internal class AndroidV1InboxStorage private constructor(
    private val file: AtomicFile,
) : V1InboxStorage {
    constructor(context: Context) : this(AtomicFile(context.noBackupFilesDir.resolve(FILE_NAME)))

    override fun read(): V1InboxLoad = synchronized(FILE_LOCK) { readLocked() }

    private fun readLocked(): V1InboxLoad = try {
        val bytes = file.openRead().use { input ->
            val buffer = ByteArray(V1InboxSnapshotCodec.MAX_ENCODED_BYTES + 1)
            var used = 0
            while (used < buffer.size) {
                val count = input.read(buffer, used, buffer.size - used)
                if (count < 0) break
                if (count == 0) continue
                used += count
            }
            if (used == buffer.size || input.read() >= 0) return V1InboxLoad.Failed
            buffer.copyOf(used)
        }
        V1InboxSnapshotCodec.decode(bytes)?.let(V1InboxLoad::Found) ?: V1InboxLoad.Failed
    } catch (_: FileNotFoundException) {
        val parent = file.baseFile.parentFile
        if (file.baseFile.exists() || parent?.resolve(FILE_NAME + ".new")?.exists() == true ||
            parent?.resolve(FILE_NAME + ".bak")?.exists() == true) V1InboxLoad.Failed else V1InboxLoad.Empty
    } catch (_: Exception) {
        V1InboxLoad.Failed
    }

    override fun commit(snapshot: V1InboxSnapshot): V1InboxCommit = synchronized(FILE_LOCK) {
        val encoded = V1InboxSnapshotCodec.encode(snapshot)
            ?: return@synchronized V1InboxCommit.FAILED_BEFORE_COMMIT
        val stream = try { file.startWrite() } catch (_: Exception) {
            return@synchronized V1InboxCommit.UNCERTAIN
        }
        try {
            stream.write(encoded)
            stream.fd.sync()
            file.finishWrite(stream)
        } catch (_: Exception) {
            try { file.failWrite(stream) } catch (_: Exception) { /* recovery remains uncertain */ }
            return@synchronized V1InboxCommit.UNCERTAIN
        }
        val readback = when (val loaded = readLocked()) {
            is V1InboxLoad.Found -> V1InboxSnapshotCodec.encode(loaded.snapshot)
            else -> null
        }
        if (readback != null && readback.contentEquals(encoded)) V1InboxCommit.VERIFIED
        else V1InboxCommit.UNCERTAIN
    }

    private companion object {
        const val FILE_NAME = "v1-inbox-snapshot.bin"
        val FILE_LOCK = Any()
    }
}
