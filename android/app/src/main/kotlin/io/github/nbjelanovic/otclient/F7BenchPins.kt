package io.github.nbjelanovic.otclient

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption

/** Public pins supplied independently by the bench owner. Only an exact live OTY0 response can bind them. */
internal class F7BenchPins private constructor(
    private val localIdentity: ByteArray,
    private val peerIdentity: ByteArray,
    private val group: ULong,
    private val epoch: UInt,
    private val revision: ULong,
) {
    fun begin(
        adapter: BlePacketAuthorityAdapter,
        owner: Any,
        query: BlePacketAuthorityQuery,
        exchange: UInt,
    ): ByteArray? = adapter.begin(owner, query, localIdentity, peerIdentity, group, epoch, revision, exchange)

    override fun toString() = "F7BenchPins(fields=redacted)"

    companion object {
        private const val FILE_NAME = "meshtrail-f7-bench-v1.txt"
        private const val RECORD_BYTES = 227
        private val RECORD = Regex(
            "meshtrail-f7-bench-v1\nlocal=([0-9a-f]{64})\npeer=([0-9a-f]{64})\n" +
                "group=([0-9a-f]{16})\nepoch=([0-9a-f]{8})\nrevision=([0-9a-f]{16})\n",
        )

        fun read(directory: File): ByteArray? {
            return try {
                val file = directory.resolve(FILE_NAME)
                if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) return null
                file.inputStream().use { input ->
                    val data = ByteArray(RECORD_BYTES)
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

        fun decode(record: ByteArray?): F7BenchPins? {
            if (record?.size != RECORD_BYTES) return null
            val fields = RECORD.matchEntire(record.toString(Charsets.US_ASCII))?.destructured ?: return null
            val (localHex, peerHex, groupHex, epochHex, revisionHex) = fields
            fun identity(hex: String) = ByteArray(32) { i -> hex.substring(i * 2, i * 2 + 2).toInt(16).toByte() }
            val local = identity(localHex)
            val peer = identity(peerHex)
            val group = groupHex.toULong(16)
            val epoch = epochHex.toUInt(16)
            val revision = revisionHex.toULong(16)
            if (local.all { it == 0.toByte() } || peer.all { it == 0.toByte() } || local.contentEquals(peer) ||
                group == 0uL || epoch == 0u || revision == 0uL) return null
            return F7BenchPins(local, peer, group, epoch, revision)
        }
    }
}
