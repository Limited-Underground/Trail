package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

data class CompanionPublicProfilePayload(
    val kind: Int,
    val reason: Int = 0,
    val revision: ULong = 0u,
    val name: String = "",
    val visible: Boolean = false,
) {
    override fun toString() = "CompanionPublicProfilePayload(kind=$kind, reason=$reason, fields=redacted)"
}

/** Atomic protected settings only. These bytes do not advertise presence or authorize radio TX. */
object CompanionPublicProfileCodec {
    const val PROFILE = 5
    const val REQUEST = 8
    const val RESPONSE = 0x8a
    const val HEADER_BYTES = 20
    const val MAX_NAME_BYTES = 96
    const val MAX_NAME_UNITS = 40
    private val magic = byteArrayOf(0x4f, 0x54, 0x50, 0x43)

    fun validName(name: String): Boolean = name.length in 1..MAX_NAME_UNITS &&
        !name.startsWith(' ') && !name.endsWith(' ') &&
        name.none { it.code in 0..31 || it.code in 127..159 } &&
        Charsets.UTF_8.newEncoder().canEncode(name) &&
        name.toByteArray(Charsets.UTF_8).size <= MAX_NAME_BYTES

    private fun valid(value: CompanionPublicProfilePayload): Boolean {
        if (value.reason !in 0..5) return false
        val empty = value.name.isEmpty()
        val named = validName(value.name)
        return when (value.kind) {
            1 -> value.reason == 0 && empty && value.revision == 0uL && !value.visible
            2 -> value.reason == 0 && named && value.revision != ULong.MAX_VALUE
            0x81 -> value.reason == 0 && ((empty && value.revision == 0uL && value.visible) ||
                (named && value.revision > 0uL))
            0x82 -> value.reason == 0 && named && value.revision > 0uL
            0x83 -> value.reason != 0 && empty && value.revision == 0uL && !value.visible
            0x84 -> value.reason == 4 && empty && value.revision == 0uL && !value.visible
            else -> false
        }
    }

    fun encode(value: CompanionPublicProfilePayload): ByteArray? {
        if (!valid(value)) return null
        val name = value.name.toByteArray(Charsets.UTF_8)
        return ByteArray(HEADER_BYTES + name.size).also {
            magic.copyInto(it)
            it[4] = 1; it[5] = value.kind.toByte(); it[6] = value.reason.toByte(); it[7] = name.size.toByte()
            repeat(8) { byte -> it[8 + byte] = (value.revision shr (byte * 8)).toByte() }
            it[16] = if (value.visible) 1 else 0
            name.copyInto(it, HEADER_BYTES)
        }
    }

    fun decode(bytes: ByteArray): CompanionPublicProfilePayload? {
        fun byte(index: Int) = bytes[index].toInt() and 255
        if (bytes.size !in HEADER_BYTES..HEADER_BYTES + MAX_NAME_BYTES ||
            !bytes.copyOfRange(0, 4).contentEquals(magic) || byte(4) != 1 ||
            byte(7) != bytes.size - HEADER_BYTES || byte(16) !in 0..1 ||
            (17..19).any { byte(it) != 0 }) return null
        var revision = 0uL
        repeat(8) { revision = revision or (byte(8 + it).toULong() shl (it * 8)) }
        val name = runCatching {
            Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(bytes, HEADER_BYTES, byte(7))).toString()
        }.getOrNull() ?: return null
        return CompanionPublicProfilePayload(byte(5), byte(6), revision, name, byte(16) == 1).takeIf(::valid)
    }
}
