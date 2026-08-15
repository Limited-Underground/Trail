package io.github.nbjelanovic.otprotocol

const val COMPANION_AUTHORIZATION_PROTOCOL_MAJOR = 0
const val COMPANION_AUTHORIZATION_PROTOCOL_MINOR = 1
const val COMPANION_AUTHORIZATION_PROTOCOL_INFO_BYTES = 20
const val COMPANION_AUTHORIZATION_CLAIM_CAPABILITY = 0x10
const val COMPANION_AUTHORIZATION_CAPABILITY_MASK =
    COMPANION_KNOWN_CAPABILITY_MASK or COMPANION_AUTHORIZATION_CLAIM_CAPABILITY
const val COMPANION_AUTHORIZATION_MINIMUM_ATT_MTU = 51

enum class CompanionAuthorizationProtocolInfoError {
    NONE,
    MALFORMED,
    UNSUPPORTED_VERSION,
    INVALID_ROLE,
    INVALID_CAPABILITY,
    INVALID_LIMIT,
    INVALID_SESSION_NONCE,
    RESERVED_BITS_SET,
}

data class CompanionAuthorizationProtocolInfo(
    val role: CompanionDeviceRole = CompanionDeviceRole.SCREENLESS_CLIENT,
    val capabilities: Int = COMPANION_AUTHORIZATION_CAPABILITY_MASK,
    val maxFragmentPayloadBytes: Int = COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES,
    val minimumNormalAttMtu: Int = COMPANION_MINIMUM_ATT_MTU,
    val maxFragmentCount: Int = COMPANION_MAX_FRAGMENT_COUNT,
    val maxActiveControllers: Int = 1,
    val provisionalSessionNonce: Long,
)

data class CompanionAuthorizationProtocolInfoResult<T>(
    val value: T? = null,
    val error: CompanionAuthorizationProtocolInfoError = CompanionAuthorizationProtocolInfoError.NONE,
) {
    val isSuccess: Boolean get() = error == CompanionAuthorizationProtocolInfoError.NONE && value != null

    companion object {
        fun <T> success(value: T) = CompanionAuthorizationProtocolInfoResult(value)
        fun <T> failure(error: CompanionAuthorizationProtocolInfoError) =
            CompanionAuthorizationProtocolInfoResult<T>(error = error)
    }
}

/** Strict codec for the separate OTB0 v0.1 provisional-authorization record. */
object CompanionAuthorizationProtocolInfoCodec {
    private val magic = byteArrayOf(0x4f, 0x54, 0x42, 0x30)

    fun encode(info: CompanionAuthorizationProtocolInfo): CompanionAuthorizationProtocolInfoResult<ByteArray> {
        validate(info)?.let { return CompanionAuthorizationProtocolInfoResult.failure(it) }
        val output = ByteArray(COMPANION_AUTHORIZATION_PROTOCOL_INFO_BYTES)
        magic.copyInto(output)
        output[4] = COMPANION_AUTHORIZATION_PROTOCOL_MAJOR.toByte()
        output[5] = COMPANION_AUTHORIZATION_PROTOCOL_MINOR.toByte()
        output[6] = info.role.wireValue.toByte()
        output[7] = info.capabilities.toByte()
        output.writeU16Le(8, info.maxFragmentPayloadBytes)
        output.writeU16Le(10, info.minimumNormalAttMtu)
        output[12] = info.maxFragmentCount.toByte()
        output[13] = info.maxActiveControllers.toByte()
        output.writeU32Le(14, info.provisionalSessionNonce)
        return CompanionAuthorizationProtocolInfoResult.success(output)
    }

    fun decode(encoded: ByteArray): CompanionAuthorizationProtocolInfoResult<CompanionAuthorizationProtocolInfo> {
        if (encoded.size != COMPANION_AUTHORIZATION_PROTOCOL_INFO_BYTES || !encoded.hasMagic(magic)) {
            return CompanionAuthorizationProtocolInfoResult.failure(CompanionAuthorizationProtocolInfoError.MALFORMED)
        }
        if (
            encoded.u8(4) != COMPANION_AUTHORIZATION_PROTOCOL_MAJOR ||
            encoded.u8(5) != COMPANION_AUTHORIZATION_PROTOCOL_MINOR
        ) {
            return CompanionAuthorizationProtocolInfoResult.failure(
                CompanionAuthorizationProtocolInfoError.UNSUPPORTED_VERSION,
            )
        }
        if (encoded.u8(18) != 0 || encoded.u8(19) != 0) {
            return CompanionAuthorizationProtocolInfoResult.failure(
                CompanionAuthorizationProtocolInfoError.RESERVED_BITS_SET,
            )
        }
        val role = CompanionDeviceRole.fromWire(encoded.u8(6))
            ?: return CompanionAuthorizationProtocolInfoResult.failure(
                CompanionAuthorizationProtocolInfoError.INVALID_ROLE,
            )
        val info = CompanionAuthorizationProtocolInfo(
            role = role,
            capabilities = encoded.u8(7),
            maxFragmentPayloadBytes = encoded.readU16Le(8),
            minimumNormalAttMtu = encoded.readU16Le(10),
            maxFragmentCount = encoded.u8(12),
            maxActiveControllers = encoded.u8(13),
            provisionalSessionNonce = encoded.readU32Le(14),
        )
        validate(info)?.let { return CompanionAuthorizationProtocolInfoResult.failure(it) }
        return CompanionAuthorizationProtocolInfoResult.success(info)
    }

    private fun validate(info: CompanionAuthorizationProtocolInfo): CompanionAuthorizationProtocolInfoError? {
        if (info.role != CompanionDeviceRole.SCREENLESS_CLIENT) {
            return CompanionAuthorizationProtocolInfoError.INVALID_ROLE
        }
        if (info.capabilities != COMPANION_AUTHORIZATION_CAPABILITY_MASK) {
            return CompanionAuthorizationProtocolInfoError.INVALID_CAPABILITY
        }
        if (
            info.maxFragmentPayloadBytes !in
                COMPANION_AUTHORIZATION_CLAIM_RESULT_BYTES..COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES ||
            info.minimumNormalAttMtu !in COMPANION_MINIMUM_ATT_MTU..0xffff ||
            info.maxFragmentCount !in 1..COMPANION_MAX_FRAGMENT_COUNT ||
            info.maxActiveControllers != 1
        ) {
            return CompanionAuthorizationProtocolInfoError.INVALID_LIMIT
        }
        if (info.provisionalSessionNonce !in 1..0xffff_ffffL) {
            return CompanionAuthorizationProtocolInfoError.INVALID_SESSION_NONCE
        }
        return null
    }
}

private fun ByteArray.hasMagic(expected: ByteArray): Boolean =
    size >= expected.size && expected.indices.all { this[it] == expected[it] }

private fun ByteArray.u8(offset: Int): Int = this[offset].toInt() and 0xff

private fun ByteArray.readU16Le(offset: Int): Int = u8(offset) or (u8(offset + 1) shl 8)

private fun ByteArray.readU32Le(offset: Int): Long =
    (0 until 4).fold(0L) { value, index -> value or (u8(offset + index).toLong() shl (index * 8)) }

private fun ByteArray.writeU16Le(offset: Int, value: Int) {
    this[offset] = value.toByte()
    this[offset + 1] = (value ushr 8).toByte()
}

private fun ByteArray.writeU32Le(offset: Int, value: Long) {
    repeat(4) { index -> this[offset + index] = (value ushr (index * 8)).toByte() }
}
