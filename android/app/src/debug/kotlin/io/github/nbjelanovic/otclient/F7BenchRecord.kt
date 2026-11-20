package io.github.nbjelanovic.otclient

import android.content.Context
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption

/** Read the independently provisioned record from the developer Debug app's private directory. */
internal fun Context.readF7BenchRecord(): ByteArray? {
    return try {
        val file = noBackupFilesDir.resolve("meshtrail-f7-bench-v1.txt")
        if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) return null
        file.inputStream().use { input ->
            val data = ByteArray(F7BenchPins.RECORD_BYTES)
            var offset = 0
            while (offset < data.size) {
                val read = input.read(data, offset, data.size - offset)
                if (read <= 0) return null
                offset += read
            }
            data.takeIf { input.read() < 0 }
        }
    } catch (_: IOException) {
        null
    } catch (_: SecurityException) {
        null
    }
}
