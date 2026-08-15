package io.github.nbjelanovic.otprotocol

private val infoMagic = byteArrayOf(0x4f, 0x54, 0x42, 0x30)
private val fragmentMagic = byteArrayOf(0x4f, 0x54, 0x43, 0x30)

const val COMPANION_PROTOCOL_MAJOR = 0
const val COMPANION_PROTOCOL_MINOR = 0
const val COMPANION_PROTOCOL_INFO_BYTES = 16
const val COMPANION_FRAGMENT_HEADER_BYTES = 20
const val COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES = 128
const val COMPANION_MAX_FRAGMENT_COUNT = 16
const val COMPANION_MINIMUM_ATT_MTU = 151
const val COMPANION_KNOWN_CAPABILITY_MASK = 0x0f

enum class CompanionDeviceRole(val wireValue: Int) {
    SCREENLESS_CLIENT(1);

    companion object {
        fun fromWire(value: Int): CompanionDeviceRole? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class CompanionFrameKind(val wireValue: Int) {
    SNAPSHOT_REQUEST(0x01),
    ACTION_REQUEST(0x02),
    SNAPSHOT(0x81),
    ACTION_RESULT(0x82),
    EVENT(0x83);

    companion object {
        fun fromWire(value: Int): CompanionFrameKind? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class CompanionCodecError {
    NONE,
    MALFORMED,
    UNSUPPORTED_VERSION,
    UNKNOWN_ROLE,
    UNKNOWN_CAPABILITY,
    UNKNOWN_FRAME_KIND,
    INVALID_LIMIT,
    INVALID_SESSION_NONCE,
    INVALID_EXCHANGE_ID,
    INVALID_FRAGMENT,
    PAYLOAD_TOO_LARGE,
    RESERVED_BITS_SET,
}

data class CodecResult<T>(
    val value: T? = null,
    val error: CompanionCodecError = CompanionCodecError.NONE,
) {
    val isSuccess: Boolean get() = error == CompanionCodecError.NONE && value != null

    companion object {
        fun <T> success(value: T) = CodecResult(value)
        fun <T> failure(error: CompanionCodecError) = CodecResult<T>(error = error)
    }
}

data class CompanionProtocolInfo(
    val role: CompanionDeviceRole = CompanionDeviceRole.SCREENLESS_CLIENT,
    val capabilities: Int,
    val maxFragmentPayloadBytes: Int = COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES,
    val minimumAttMtu: Int = COMPANION_MINIMUM_ATT_MTU,
    val maxFragmentCount: Int = COMPANION_MAX_FRAGMENT_COUNT,
    val maxActiveControllers: Int = 1,
)

data class CompanionFragment(
    val kind: CompanionFrameKind,
    val sessionNonce: Long,
    val exchangeId: Long,
    val fragmentIndex: Int = 0,
    val fragmentCount: Int = 1,
    val payload: ByteArray = byteArrayOf(),
) {
    override fun equals(other: Any?): Boolean =
        other is CompanionFragment &&
            kind == other.kind &&
            sessionNonce == other.sessionNonce &&
            exchangeId == other.exchangeId &&
            fragmentIndex == other.fragmentIndex &&
            fragmentCount == other.fragmentCount &&
            payload.contentEquals(other.payload)

    override fun hashCode(): Int {
        var result = kind.hashCode()
        result = 31 * result + sessionNonce.hashCode()
        result = 31 * result + exchangeId.hashCode()
        result = 31 * result + fragmentIndex
        result = 31 * result + fragmentCount
        return 31 * result + payload.contentHashCode()
    }
}

object CompanionProtocolCodec {
    fun encodeProtocolInfo(info: CompanionProtocolInfo): CodecResult<ByteArray> {
        validateInfo(info)?.let { return CodecResult.failure(it) }
        val output = ByteArray(COMPANION_PROTOCOL_INFO_BYTES)
        infoMagic.copyInto(output)
        output[4] = COMPANION_PROTOCOL_MAJOR.toByte()
        output[5] = COMPANION_PROTOCOL_MINOR.toByte()
        output[6] = info.role.wireValue.toByte()
        output[7] = info.capabilities.toByte()
        writeU16Le(output, 8, info.maxFragmentPayloadBytes)
        writeU16Le(output, 10, info.minimumAttMtu)
        output[12] = info.maxFragmentCount.toByte()
        output[13] = info.maxActiveControllers.toByte()
        return CodecResult.success(output)
    }

    fun decodeProtocolInfo(encoded: ByteArray): CodecResult<CompanionProtocolInfo> {
        if (encoded.size != COMPANION_PROTOCOL_INFO_BYTES || !encoded.hasMagic(infoMagic)) {
            return CodecResult.failure(CompanionCodecError.MALFORMED)
        }
        if (encoded.u8(4) != COMPANION_PROTOCOL_MAJOR || encoded.u8(5) != COMPANION_PROTOCOL_MINOR) {
            return CodecResult.failure(CompanionCodecError.UNSUPPORTED_VERSION)
        }
        if (encoded.u8(14) != 0 || encoded.u8(15) != 0) {
            return CodecResult.failure(CompanionCodecError.RESERVED_BITS_SET)
        }
        val role = CompanionDeviceRole.fromWire(encoded.u8(6))
            ?: return CodecResult.failure(CompanionCodecError.UNKNOWN_ROLE)
        val info = CompanionProtocolInfo(
            role = role,
            capabilities = encoded.u8(7),
            maxFragmentPayloadBytes = encoded.readU16Le(8),
            minimumAttMtu = encoded.readU16Le(10),
            maxFragmentCount = encoded.u8(12),
            maxActiveControllers = encoded.u8(13),
        )
        validateInfo(info)?.let { return CodecResult.failure(it) }
        return CodecResult.success(info)
    }

    fun encodeFragment(fragment: CompanionFragment): CodecResult<ByteArray> {
        validateFragment(fragment)?.let { return CodecResult.failure(it) }
        val output = ByteArray(COMPANION_FRAGMENT_HEADER_BYTES + fragment.payload.size)
        fragmentMagic.copyInto(output)
        output[4] = COMPANION_PROTOCOL_MAJOR.toByte()
        output[5] = COMPANION_PROTOCOL_MINOR.toByte()
        output[6] = fragment.kind.wireValue.toByte()
        writeU32Le(output, 8, fragment.sessionNonce)
        writeU32Le(output, 12, fragment.exchangeId)
        output[16] = fragment.fragmentIndex.toByte()
        output[17] = fragment.fragmentCount.toByte()
        writeU16Le(output, 18, fragment.payload.size)
        fragment.payload.copyInto(output, COMPANION_FRAGMENT_HEADER_BYTES)
        return CodecResult.success(output)
    }

    fun decodeFragment(encoded: ByteArray): CodecResult<CompanionFragment> {
        if (encoded.size < COMPANION_FRAGMENT_HEADER_BYTES || !encoded.hasMagic(fragmentMagic)) {
            return CodecResult.failure(CompanionCodecError.MALFORMED)
        }
        if (encoded.size > COMPANION_FRAGMENT_HEADER_BYTES + COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) {
            return CodecResult.failure(CompanionCodecError.PAYLOAD_TOO_LARGE)
        }
        if (encoded.u8(4) != COMPANION_PROTOCOL_MAJOR || encoded.u8(5) != COMPANION_PROTOCOL_MINOR) {
            return CodecResult.failure(CompanionCodecError.UNSUPPORTED_VERSION)
        }
        if (encoded.u8(7) != 0) {
            return CodecResult.failure(CompanionCodecError.RESERVED_BITS_SET)
        }
        val kind = CompanionFrameKind.fromWire(encoded.u8(6))
            ?: return CodecResult.failure(CompanionCodecError.UNKNOWN_FRAME_KIND)
        val declaredPayloadBytes = encoded.readU16Le(18)
        if (declaredPayloadBytes > COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) {
            return CodecResult.failure(CompanionCodecError.PAYLOAD_TOO_LARGE)
        }
        if (encoded.size != COMPANION_FRAGMENT_HEADER_BYTES + declaredPayloadBytes) {
            return CodecResult.failure(CompanionCodecError.MALFORMED)
        }
        val fragment = CompanionFragment(
            kind = kind,
            sessionNonce = encoded.readU32Le(8),
            exchangeId = encoded.readU32Le(12),
            fragmentIndex = encoded.u8(16),
            fragmentCount = encoded.u8(17),
            payload = encoded.copyOfRange(COMPANION_FRAGMENT_HEADER_BYTES, encoded.size),
        )
        validateFragment(fragment)?.let { return CodecResult.failure(it) }
        return CodecResult.success(fragment)
    }

    private fun validateInfo(info: CompanionProtocolInfo): CompanionCodecError? {
        if (info.capabilities !in 0..COMPANION_KNOWN_CAPABILITY_MASK) {
            return CompanionCodecError.UNKNOWN_CAPABILITY
        }
        if (
            info.maxFragmentPayloadBytes !in 1..COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES ||
            info.minimumAttMtu !in 23..0xffff ||
            info.minimumAttMtu < COMPANION_FRAGMENT_HEADER_BYTES + info.maxFragmentPayloadBytes + 3 ||
            info.maxFragmentCount !in 1..COMPANION_MAX_FRAGMENT_COUNT ||
            info.maxActiveControllers != 1
        ) {
            return CompanionCodecError.INVALID_LIMIT
        }
        return null
    }

    private fun validateFragment(fragment: CompanionFragment): CompanionCodecError? {
        if (fragment.sessionNonce !in 1..0xffff_ffffL) {
            return CompanionCodecError.INVALID_SESSION_NONCE
        }
        if (fragment.exchangeId !in 1..0xffff_ffffL) {
            return CompanionCodecError.INVALID_EXCHANGE_ID
        }
        if (
            fragment.fragmentCount !in 1..COMPANION_MAX_FRAGMENT_COUNT ||
            fragment.fragmentIndex !in 0 until fragment.fragmentCount
        ) {
            return CompanionCodecError.INVALID_FRAGMENT
        }
        if (fragment.payload.size > COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) {
            return CompanionCodecError.PAYLOAD_TOO_LARGE
        }
        return null
    }
}

private fun ByteArray.hasMagic(magic: ByteArray): Boolean =
    size >= magic.size && magic.indices.all { this[it] == magic[it] }

private fun ByteArray.u8(offset: Int): Int = this[offset].toInt() and 0xff

private fun ByteArray.readU16Le(offset: Int): Int = u8(offset) or (u8(offset + 1) shl 8)

private fun ByteArray.readU32Le(offset: Int): Long =
    u8(offset).toLong() or
        (u8(offset + 1).toLong() shl 8) or
        (u8(offset + 2).toLong() shl 16) or
        (u8(offset + 3).toLong() shl 24)

private fun writeU16Le(output: ByteArray, offset: Int, value: Int) {
    output[offset] = value.toByte()
    output[offset + 1] = (value ushr 8).toByte()
}

private fun writeU32Le(output: ByteArray, offset: Int, value: Long) {
    repeat(4) { index -> output[offset + index] = (value ushr (index * 8)).toByte() }
}
