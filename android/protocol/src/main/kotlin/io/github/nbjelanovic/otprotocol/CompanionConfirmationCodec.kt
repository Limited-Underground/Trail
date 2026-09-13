package io.github.nbjelanovic.otprotocol

/** OT216 evaluation-only payload. Owns all bytes; values never enter diagnostic strings. */
class CompanionConfirmationPayload internal constructor(bytes: ByteArray) {
    private val owned = bytes.copyOf()
    fun encoded(): ByteArray = owned.copyOf()
    val op: Int get() = owned[5].toInt() and 255
    val status: Int get() = owned[6].toInt() and 255
    val role: Int get() = owned[7].toInt() and 255
    val attempt: Long get() = number(8, 8).toLong()
    val group: ULong get() = number(16, 8)
    val epoch: UInt get() = number(24, 4).toUInt()
    val nonce: ByteArray get() = owned.copyOfRange(28, 44)
    val peer: ByteArray get() = owned.copyOfRange(44, 76)
    val transcript: ByteArray get() = owned.copyOfRange(76, 108)
    val remainingMillis: Long get() = number(108, 4).toLong()
    val transportGeneration: ULong get() = number(112, 8)
    val sessionNonce: UInt get() = number(120, 4).toUInt()
    fun withOperation(op: Int, status: Int = 0): CompanionConfirmationPayload? =
        CompanionConfirmationCodec.decode(owned.copyOf().also { it[5] = op.toByte(); it[6] = status.toByte() })
    fun sameDescriptor(other: CompanionConfirmationPayload): Boolean =
        (7 until 128).all { owned[it] == other.owned[it] }
    private fun number(offset: Int, count: Int): ULong {
        var result = 0uL
        repeat(count) { result = result or ((owned[offset + it].toInt() and 255).toULong() shl (8 * it)) }
        return result
    }
    override fun toString() = "CompanionConfirmationPayload(op=$op, status=$status, fields=redacted)"
}

object CompanionConfirmationCodec {
    const val PROFILE = 127
    const val CAPABILITIES = 0xdf
    const val REQUEST = 7
    const val RESPONSE = 0x89
    const val PAYLOAD_BYTES = 128
    const val READ = 1
    const val CONFIRM = 2
    const val CANCEL = 3
    const val OFFER = 4
    const val RESULT = 5
    const val LOCAL_CONFIRMED = 1
    const val CANCELLED = 2
    const val REFUSED = 3
    const val UNAVAILABLE = 4
    const val WAITING = 5

    fun read(): CompanionConfirmationPayload = checkNotNull(decode(ByteArray(128).also {
        byteArrayOf(0x4f, 0x54, 0x47, 0x43, 1, READ.toByte()).copyInto(it)
    }))

    fun decode(bytes: ByteArray): CompanionConfirmationPayload? {
        if (bytes.size != 128 || !bytes.copyOfRange(0, 5).contentEquals(byteArrayOf(0x4f, 0x54, 0x47, 0x43, 1)) ||
            (124..127).any { bytes[it] != 0.toByte() }) return null
        val value = CompanionConfirmationPayload(bytes)
        if (value.op == READ) return value.takeIf { (6..127).all { bytes[it] == 0.toByte() } }
        if (value.op == RESULT && value.status in UNAVAILABLE..WAITING)
            return value.takeIf { (7..127).all { bytes[it] == 0.toByte() } }
        if (value.op !in CONFIRM..RESULT ||
            (value.op == RESULT && value.status !in LOCAL_CONFIRMED..REFUSED) ||
            (value.op != RESULT && value.status != 0) || value.role !in 1..2 || value.attempt <= 0 ||
            value.group == 0uL || value.epoch == 0u || value.nonce.all { it == 0.toByte() } ||
            value.peer.all { it == 0.toByte() } || value.transcript.all { it == 0.toByte() } ||
            value.remainingMillis !in 1..60_000 || value.transportGeneration == 0uL || value.sessionNonce == 0u) return null
        return value
    }
}
