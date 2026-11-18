package io.github.nbjelanovic.otprotocol

/** Correlation to a current owner-held OTY0 observation; this value is never authority. */
data class CompanionTextReference(val nonce: UInt, val authorityExchange: UInt, val generation: ULong) {
    override fun toString() = "CompanionTextReference(fields=redacted)"
}

sealed interface CompanionTextRecord {
    val reference: CompanionTextReference
    val exchange: UInt
}

data class CompanionTextSend(override val reference: CompanionTextReference, override val exchange: UInt,
    val epoch: UInt, val operation: ULong, val text: String) : CompanionTextRecord {
    override fun toString() = "CompanionTextSend(fields=redacted)"
}
data class CompanionTextResult(override val reference: CompanionTextReference, override val exchange: UInt,
    val operation: ULong, val disposition: Int, val reason: Int,
    val receipt: ULong, val messageId: UInt, val counter: ULong) : CompanionTextRecord {
    override fun toString() = "CompanionTextResult(disposition=$disposition, reason=$reason, fields=redacted)"
}
data class CompanionTextReceived(override val reference: CompanionTextReference, override val exchange: UInt,
    val messageId: UInt, val counter: ULong, val text: String) : CompanionTextRecord {
    override fun toString() = "CompanionTextReceived(fields=redacted)"
}
data class CompanionTextDelivery(override val reference: CompanionTextReference, override val exchange: UInt,
    val originExchange: UInt, val operation: ULong, val receipt: ULong, val messageId: UInt,
    val counter: ULong, val phase: Int, val reason: Int) : CompanionTextRecord {
    override fun toString() = "CompanionTextDelivery(phase=$phase, reason=$reason, fields=redacted)"
}

/** Accepted TEXT-CARRIER-V1 format only. Explicit host opt-in is not authentication or activation. */
object CompanionTextCarrierCodec {
    const val SEND = 0x0a
    const val RESULT = 0x8c
    const val RECEIVED = 0x8d
    const val DELIVERY = 0x8e
    const val MAX_RECORD_BYTES = 148
    const val MINIMUM_ATT_MTU = 151
    private val envelope = byteArrayOf(0x4f, 0x54, 0x43, 0x30)
    private val domain = byteArrayOf(0x4f, 0x54, 0x58, 0x43)

    fun encode(record: CompanionTextRecord, allowProfile06: Boolean = false): ByteArray? {
        if (!allowProfile06) return null
        val text = when (record) {
            is CompanionTextSend -> OtxtPayloadCodec.encode(record.text) ?: return null
            is CompanionTextReceived -> OtxtPayloadCodec.encode(record.text) ?: return null
            else -> null
        }
        val kind = when (record) {
            is CompanionTextSend -> SEND
            is CompanionTextResult -> RESULT
            is CompanionTextReceived -> RECEIVED
            is CompanionTextDelivery -> DELIVERY
        }
        val size = when (record) {
            is CompanionTextSend, is CompanionTextReceived -> 50 + checkNotNull(text).size
            is CompanionTextResult -> 68
            is CompanionTextDelivery -> 72
        }
        val b = ByteArray(size)
        envelope.copyInto(b); b[5] = 6; b[6] = kind.toByte(); b[17] = 1
        put(b, 8, record.reference.nonce.toULong(), 4); put(b, 12, record.exchange.toULong(), 4)
        put(b, 18, (size - 20).toULong(), 2)
        domain.copyInto(b, 20); b[24] = 1
        put(b, 26, record.reference.authorityExchange.toULong(), 4); put(b, 30, record.reference.generation, 8)
        when (record) {
            is CompanionTextSend -> {
                put(b, 38, record.epoch.toULong(), 4); put(b, 42, record.operation, 8)
                checkNotNull(text).copyInto(b, 50)
            }
            is CompanionTextResult -> {
                put(b, 38, record.operation, 8); b[46] = record.disposition.toByte(); b[47] = record.reason.toByte()
                put(b, 48, record.receipt, 8); put(b, 56, record.messageId.toULong(), 4); put(b, 60, record.counter, 8)
            }
            is CompanionTextReceived -> {
                put(b, 38, record.messageId.toULong(), 4); put(b, 42, record.counter, 8)
                checkNotNull(text).copyInto(b, 50)
            }
            is CompanionTextDelivery -> {
                put(b, 38, record.originExchange.toULong(), 4); put(b, 42, record.operation, 8)
                put(b, 50, record.receipt, 8); put(b, 58, record.messageId.toULong(), 4); put(b, 62, record.counter, 8)
                b[70] = record.phase.toByte(); b[71] = record.reason.toByte()
            }
        }
        // Refuse invalid scalar domains as well as bytes that would silently wrap an Int to u8.
        if (record is CompanionTextResult && (record.disposition !in 1..3 || record.reason !in 0..15) ||
            record is CompanionTextDelivery && (record.phase !in listOf(1, 2, 3, 5, 6) || record.reason !in 0..15)) return null
        return b.takeIf { decode(it, allowProfile06 = true) != null }
    }

    fun decode(encoded: ByteArray, allowProfile06: Boolean = false): CompanionTextRecord? {
        if (!allowProfile06 || encoded.size !in 38..MAX_RECORD_BYTES) return null
        val b = encoded.copyOf()
        if (!envelope.indices.all { b[it] == envelope[it] } || b[4] != 0.toByte() || b[5] != 6.toByte() ||
            b[7] != 0.toByte() || b[16] != 0.toByte() || b[17] != 1.toByte() ||
            get(b, 18, 2) != (b.size - 20).toULong() || !domain.indices.all { b[20 + it] == domain[it] } ||
            b[24] != 1.toByte() || b[25] != 0.toByte()) return null
        val ref = CompanionTextReference(get(b, 8, 4).toUInt(), get(b, 26, 4).toUInt(), get(b, 30, 8))
        val exchange = get(b, 12, 4).toUInt()
        if (ref.nonce == 0u || ref.authorityExchange == 0u || ref.generation == 0uL || exchange == 0u) return null
        return when (u8(b, 6)) {
            SEND -> {
                if (b.size < 59) return null
                val epoch = get(b, 38, 4).toUInt(); val operation = get(b, 42, 8)
                if (operation == 0uL) return null
                CompanionTextSend(ref, exchange, epoch, operation, OtxtPayloadCodec.decode(b.copyOfRange(50, b.size)) ?: return null)
            }
            RESULT -> {
                if (b.size != 68) return null
                val r = CompanionTextResult(ref, exchange, get(b, 38, 8), u8(b, 46), u8(b, 47),
                    get(b, 48, 8), get(b, 56, 4).toUInt(), get(b, 60, 8))
                val allZero = r.receipt == 0uL && r.messageId == 0u && r.counter == 0uL
                val allKnown = r.receipt != 0uL && r.messageId != 0u && r.counter != 0uL
                if (r.operation == 0uL || r.reason !in 0..15 || when (r.disposition) {
                    1 -> r.reason != 0 || !allKnown
                    2 -> r.reason == 0 || !allZero
                    3 -> r.reason == 0 || !(allZero || allKnown)
                    else -> true
                }) null else r
            }
            RECEIVED -> {
                if (b.size < 59) return null
                val message = get(b, 38, 4).toUInt(); val counter = get(b, 42, 8)
                if (message == 0u || counter == 0uL) return null
                CompanionTextReceived(ref, exchange, message, counter, OtxtPayloadCodec.decode(b.copyOfRange(50, b.size)) ?: return null)
            }
            DELIVERY -> {
                if (b.size != 72) return null
                val r = CompanionTextDelivery(ref, exchange, get(b, 38, 4).toUInt(), get(b, 42, 8), get(b, 50, 8),
                    get(b, 58, 4).toUInt(), get(b, 62, 8), u8(b, 70), u8(b, 71))
                if (r.originExchange == 0u || r.operation == 0uL || r.receipt == 0uL || r.messageId == 0u || r.counter == 0uL ||
                    r.phase !in listOf(1, 2, 3, 5, 6) || r.reason !in 0..15 ||
                    r.phase in 1..3 && r.reason != 0 || r.phase in 5..6 && r.reason == 0) null else r
            }
            else -> null
        }
    }
    private fun u8(b: ByteArray, offset: Int) = b[offset].toInt() and 255
    private fun get(b: ByteArray, offset: Int, size: Int): ULong {
        var value = 0uL
        repeat(size) { value = value or (u8(b, offset + it).toULong() shl (it * 8)) }
        return value
    }
    private fun put(b: ByteArray, offset: Int, value: ULong, size: Int) {
        repeat(size) { b[offset + it] = (value shr (it * 8)).toByte() }
    }
}
