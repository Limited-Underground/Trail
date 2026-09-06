package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

/** Candidate payload only. No existing envelope, capability or runtime advertises this operation. */
enum class CompanionNameKind(val code: Int) {
    READ(1), WRITE(2), SNAPSHOT(0x81), APPLIED(0x82), REJECTED(0x83), UNCERTAIN(0x84)
}

enum class CompanionNameReason(val code: Int) {
    NONE(0), UNAUTHORIZED(1), STALE_REVISION(2), INVALID_NAME(3), STORAGE_FAILURE(4), UNSUPPORTED(5)
}

data class CompanionNamePayload(
    val kind: CompanionNameKind,
    val revision: ULong = 0u,
    val name: String = "",
    val reason: CompanionNameReason = CompanionNameReason.NONE,
) {
    override fun toString() = "CompanionNamePayload(kind=$kind, revision=$revision, name=redacted, reason=$reason)"
}

object CompanionNamePayloadCodec {
    const val HEADER_BYTES = 16
    const val MAX_NAME_BYTES = 96
    private val magic = byteArrayOf(0x4f, 0x54, 0x4e, 0x43)

    fun encode(value: CompanionNamePayload): ByteArray? {
        if (!valid(value)) return null
        val name = value.name.toByteArray(Charsets.UTF_8)
        return ByteArray(HEADER_BYTES + name.size).also { output ->
            magic.copyInto(output)
            output[4] = 1
            output[5] = value.kind.code.toByte()
            output[6] = value.reason.code.toByte()
            output[7] = name.size.toByte()
            repeat(8) { output[8 + it] = (value.revision shr (it * 8)).toByte() }
            name.copyInto(output, HEADER_BYTES)
        }
    }

    fun decode(encoded: ByteArray): CompanionNamePayload? {
        if (encoded.size !in HEADER_BYTES..HEADER_BYTES + MAX_NAME_BYTES ||
            !magic.indices.all { encoded[it] == magic[it] } || encoded[4].toInt() != 1) return null
        fun byte(index: Int) = encoded[index].toInt() and 0xff
        if (encoded.size != HEADER_BYTES + byte(7)) return null
        val kind = CompanionNameKind.entries.firstOrNull { it.code == byte(5) } ?: return null
        val reason = CompanionNameReason.entries.firstOrNull { it.code == byte(6) } ?: return null
        var revision = 0uL
        repeat(8) { revision = revision or (byte(8 + it).toULong() shl (it * 8)) }
        val name = runCatching {
            Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(encoded, HEADER_BYTES, byte(7))).toString()
        }.getOrNull() ?: return null
        return CompanionNamePayload(kind, revision, name, reason).takeIf(::valid)
    }

    private fun valid(value: CompanionNamePayload): Boolean {
        val noName = value.name.isEmpty()
        val validName = value.name.length in 1..32 &&
            !value.name.startsWith(' ') && !value.name.endsWith(' ') &&
            value.name.none { it.code in 0..31 || it.code in 127..159 } &&
            Charsets.UTF_8.newEncoder().canEncode(value.name) &&
            value.name.toByteArray(Charsets.UTF_8).size <= MAX_NAME_BYTES
        val noReason = value.reason == CompanionNameReason.NONE
        return when (value.kind) {
            CompanionNameKind.READ -> noReason && noName && value.revision == 0uL
            CompanionNameKind.WRITE -> noReason && validName && value.revision != ULong.MAX_VALUE
            CompanionNameKind.SNAPSHOT -> noReason &&
                ((noName && value.revision == 0uL) || (validName && value.revision > 0uL))
            CompanionNameKind.APPLIED -> noReason && validName && value.revision > 0uL
            CompanionNameKind.REJECTED -> !noReason && noName && value.revision == 0uL
            CompanionNameKind.UNCERTAIN -> value.reason == CompanionNameReason.STORAGE_FAILURE &&
                noName && value.revision == 0uL
        }
    }
}
