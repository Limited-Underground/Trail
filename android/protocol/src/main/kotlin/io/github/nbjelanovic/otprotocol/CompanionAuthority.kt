package io.github.nbjelanovic.otprotocol

/**
 * Secret-free OTY0 serialization only, per the October 29 freeze
 * (SHA-256 3ee5022980746183706045965dc021a45711edbcbcf6b24a8a436352ede556d7).
 *
 * Decoding metadata does not authenticate a peer or supply Packet authority. The future protected
 * owner must pin identities, own one outstanding exchange, reject duplicates, and invalidate on
 * lease/callback/owner/lifecycle changes. This codec neither negotiates nor enables profile 6.
 */
object CompanionAuthorityCodec {
    const val PROFILE = 6
    const val REQUEST_KIND = 0x09
    const val RESPONSE_KIND = 0x8b
    const val REQUEST_BYTES = 76
    const val RESPONSE_BYTES = 136
    const val MINIMUM_ATT_MTU = 151
    const val MAX_REMAINING_MILLIS = 30_000L
    private val envelopeMagic = byteArrayOf(0x4f, 0x54, 0x43, 0x30)
    private val payloadMagic = byteArrayOf(0x4f, 0x54, 0x59, 0x30)

    fun encodeRequest(
        remoteIdentity: ByteArray, group: ULong, epoch: UInt, nonce: UInt, exchange: UInt,
    ): ByteArray? {
        if (!identityValid(remoteIdentity) || group == 0uL || epoch == 0u || nonce == 0u || exchange == 0u) return null
        return header(REQUEST_BYTES, REQUEST_KIND, nonce, exchange).also {
            remoteIdentity.copyInto(it, 28)
            it.put(60, group, 8)
            it.put(68, epoch.toULong(), 4)
            it.put(72, nonce.toULong(), 4)
        }
    }

    fun decodeRequest(encoded: ByteArray): CompanionAuthorityRequest? {
        if (encoded.size != REQUEST_BYTES) return null
        val bytes = encoded.copyOf()
        if (!validHeader(bytes, REQUEST_BYTES, REQUEST_KIND) || !validPrefix(bytes) || bytes[26] != 0.toByte()) return null
        val value = CompanionAuthorityRequest(bytes)
        if (!identityValid(value.remoteIdentity) || value.group == 0uL || value.epoch == 0u ||
            bytes.number(72, 4).toUInt() != value.nonce) return null
        return value
    }

    /** Encoding valid metadata is not an authority producer. No key or authenticated state is created. */
    fun encodeResponse(value: CompanionAuthorityResponse): ByteArray =
        header(RESPONSE_BYTES, RESPONSE_KIND, value.nonce, value.exchange).also {
            if (!value.isRefusal) {
                it[26] = 3
                value.localIdentity.copyInto(it, 28)
                value.remoteIdentity.copyInto(it, 60)
                it.put(92, value.group, 8)
                it.put(100, value.epoch.toULong(), 4)
                it.put(104, value.deviceGeneration, 8)
                it.put(112, value.sessionGeneration, 8)
                it.put(120, value.membershipRevision, 8)
                it.put(132, value.remainingMillis.toULong(), 4)
            }
            it.put(128, value.nonce.toULong(), 4)
        }

    /** Structural decoding only; a success record is an observation, never proof of authentication. */
    fun decodeResponse(encoded: ByteArray): CompanionAuthorityResponse? {
        if (encoded.size != RESPONSE_BYTES) return null
        val bytes = encoded.copyOf()
        if (!validHeader(bytes, RESPONSE_BYTES, RESPONSE_KIND) || !validPrefix(bytes) ||
            bytes.number(128, 4) != bytes.number(8, 4)) return null
        val value = CompanionAuthorityResponse(bytes)
        when (bytes[26].toInt() and 255) {
            0 -> if ((28 until 128).any { bytes[it] != 0.toByte() } ||
                (132 until 136).any { bytes[it] != 0.toByte() }) return null
            3 -> if (!identityValid(value.localIdentity) || !identityValid(value.remoteIdentity) ||
                value.localIdentity.contentEquals(value.remoteIdentity) || value.group == 0uL ||
                value.epoch == 0u || value.deviceGeneration == 0uL ||
                value.deviceGeneration != value.sessionGeneration || value.membershipRevision == 0uL ||
                value.remainingMillis !in 1..MAX_REMAINING_MILLIS) return null
            else -> return null
        }
        return value
    }

    /**
     * Compare against separately supplied pins and the actual outstanding request. This comparison
     * cannot establish that those pins are authenticated, that an exchange is unused, or that its
     * lease remains live. Refusal is correlated but never carries authority.
     */
    fun matchesRequest(
        response: CompanionAuthorityResponse, request: CompanionAuthorityRequest, pinnedLocalIdentity: ByteArray,
    ): Boolean = identityValid(pinnedLocalIdentity) && response.nonce == request.nonce &&
        response.exchange == request.exchange && (response.isRefusal ||
        (response.localIdentity.contentEquals(pinnedLocalIdentity) &&
            response.remoteIdentity.contentEquals(request.remoteIdentity) &&
            response.group == request.group && response.epoch == request.epoch))

    /** Fixed Android-local deadline, anchored at request start, never at reply receipt. */
    fun deadlineMillis(requestStartedAtMillis: Long, remainingMillis: Long, replyAtMillis: Long): Long? {
        if (requestStartedAtMillis < 0 || remainingMillis !in 1..MAX_REMAINING_MILLIS ||
            replyAtMillis < requestStartedAtMillis || requestStartedAtMillis > Long.MAX_VALUE - remainingMillis) return null
        return (requestStartedAtMillis + remainingMillis).takeIf { replyAtMillis < it }
    }

    /** Future owner must additionally reject rollback relative to its latest clock observation. */
    fun isBeforeDeadline(replyAtMillis: Long, deadlineMillis: Long, nowMillis: Long): Boolean =
        replyAtMillis >= 0 && deadlineMillis > replyAtMillis && nowMillis >= replyAtMillis && nowMillis < deadlineMillis

    private fun header(size: Int, kind: Int, nonce: UInt, exchange: UInt): ByteArray = ByteArray(size).also {
        envelopeMagic.copyInto(it)
        it[5] = PROFILE.toByte()
        it[6] = kind.toByte()
        it.put(8, nonce.toULong(), 4)
        it.put(12, exchange.toULong(), 4)
        it[17] = 1
        it.put(18, (size - 20).toULong(), 2)
        payloadMagic.copyInto(it, 20)
        it[24] = 1
        it[25] = 1
    }

    private fun validHeader(bytes: ByteArray, size: Int, kind: Int): Boolean =
        bytes.size == size && envelopeMagic.indices.all { bytes[it] == envelopeMagic[it] } &&
            bytes[4] == 0.toByte() && bytes[5].toInt() == PROFILE && (bytes[6].toInt() and 255) == kind &&
            bytes[7] == 0.toByte() && bytes.number(8, 4) != 0uL && bytes.number(12, 4) != 0uL &&
            bytes[16] == 0.toByte() && bytes[17] == 1.toByte() && bytes.number(18, 2) == (size - 20).toULong()

    private fun validPrefix(bytes: ByteArray): Boolean =
        payloadMagic.indices.all { bytes[20 + it] == payloadMagic[it] } &&
            bytes[24] == 1.toByte() && bytes[25] == 1.toByte() && bytes[27] == 0.toByte()

    private fun identityValid(bytes: ByteArray): Boolean = bytes.size == 32 && bytes.any { it != 0.toByte() }
}

/** Owns its bytes; identities are opaque serialization data, not credentials. */
class CompanionAuthorityRequest internal constructor(bytes: ByteArray) {
    private val owned = bytes.copyOf()
    val remoteIdentity: ByteArray get() = owned.copyOfRange(28, 60)
    val group: ULong get() = owned.number(60, 8)
    val epoch: UInt get() = owned.number(68, 4).toUInt()
    val nonce: UInt get() = owned.number(8, 4).toUInt()
    val exchange: UInt get() = owned.number(12, 4).toUInt()
    override fun toString() = "CompanionAuthorityRequest(fields=redacted)"
}

/** Validated wire observation only. No lease, callback generation, trust flag or authority API. */
class CompanionAuthorityResponse internal constructor(bytes: ByteArray) {
    private val owned = bytes.copyOf()
    val isRefusal: Boolean get() = owned[26] == 0.toByte()
    val localIdentity: ByteArray get() = owned.copyOfRange(28, 60)
    val remoteIdentity: ByteArray get() = owned.copyOfRange(60, 92)
    val group: ULong get() = owned.number(92, 8)
    val epoch: UInt get() = owned.number(100, 4).toUInt()
    val deviceGeneration: ULong get() = owned.number(104, 8)
    val sessionGeneration: ULong get() = owned.number(112, 8)
    val membershipRevision: ULong get() = owned.number(120, 8)
    val nonce: UInt get() = owned.number(8, 4).toUInt()
    val exchange: UInt get() = owned.number(12, 4).toUInt()
    val remainingMillis: Long get() = owned.number(132, 4).toLong()
    override fun toString() = "CompanionAuthorityResponse(fields=redacted)"
}

private fun ByteArray.number(offset: Int, count: Int): ULong {
    var result = 0uL
    repeat(count) { result = result or ((this[offset + it].toInt() and 255).toULong() shl (it * 8)) }
    return result
}

private fun ByteArray.put(offset: Int, value: ULong, count: Int) {
    repeat(count) { this[offset + it] = (value shr (it * 8)).toByte() }
}
