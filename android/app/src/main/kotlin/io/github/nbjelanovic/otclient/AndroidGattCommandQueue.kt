package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.COMPANION_FRAGMENT_HEADER_BYTES
import io.github.nbjelanovic.otprotocol.COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES

/** One lease, one in-flight write and at most one successor awaiting its Android callback. */
internal class AndroidGattCommandQueue(
    private val operations: AndroidGattOperationGate,
    private val maximumBytes: Int = COMPANION_FRAGMENT_HEADER_BYTES + COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES,
) {
    private var queued: ByteArray? = null
    private var closed = false
    val hasQueued: Boolean get() = queued != null

    fun submit(value: ByteArray, write: (ByteArray) -> Boolean): Boolean {
        if (closed || value.isEmpty() || value.size > maximumBytes) return false
        return when (operations.stage) {
            AndroidGattStage.READY -> send(value.copyOf(), write)
            AndroidGattStage.COMMAND_WRITE_PENDING -> {
                if (queued != null) false else {
                    queued = value.copyOf()
                    true
                }
            }
            else -> false
        }
    }

    fun acknowledge(write: (ByteArray) -> Boolean): Boolean {
        if (closed) return false
        if (!operations.acceptCommandWrite()) {
            close()
            return false
        }
        val next = queued ?: return true
        queued = null // Consume before calling the platform; uncertainty must never replay bytes.
        return send(next, write)
    }

    private fun send(value: ByteArray, write: (ByteArray) -> Boolean): Boolean {
        if (!operations.beginCommandWrite()) {
            value.fill(0)
            close()
            return false
        }
        // Platform adapters contain their own permission/transport exceptions and copy as needed.
        if (write(value)) return true
        value.fill(0)
        close()
        return false
    }

    fun close() {
        closed = true
        queued?.fill(0)
        queued = null
        operations.close()
    }
}
