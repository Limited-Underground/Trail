package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction

/** OTXT v1 payload only. No Packet authentication, authority, routing or carrier admission. */
object OtxtPayloadCodec {
    const val ENVELOPE_BYTES = 8
    const val MAX_TEXT_UTF8_BYTES = 90
    const val MAX_FRAME_BYTES = 158
    private const val PACKET_OVERHEAD_BYTES = 60
    private val magic = byteArrayOf(0x4f, 0x54, 0x58, 0x54)

    /** [maximumFrameBytes] is the admitted whole Packet frame budget, including its header/tag. */
    fun encode(text: String, maximumFrameBytes: Int = MAX_FRAME_BYTES): ByteArray? {
        val limit = textLimit(maximumFrameBytes)
        // Every valid UTF-16 code unit needs at least one UTF-8 byte. Bound allocation first.
        if (text.isEmpty() || text.length > limit) return null
        val utf8 = try {
            Charsets.UTF_8.newEncoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .encode(CharBuffer.wrap(text))
        } catch (_: CharacterCodingException) {
            return null
        }
        val length = utf8.remaining()
        if (length !in 1..limit) return null
        return ByteArray(ENVELOPE_BYTES + length).also {
            magic.copyInto(it)
            it[4] = 1
            it[6] = (length ushr 8).toByte()
            it[7] = length.toByte()
            utf8.get(it, ENVELOPE_BYTES, length)
        }
    }

    /** Strict, exact-length UTF-8; preserves whitespace, normalization and line endings. */
    fun decode(payload: ByteArray, maximumFrameBytes: Int = MAX_FRAME_BYTES): String? {
        if (payload.size !in ENVELOPE_BYTES + 1..ENVELOPE_BYTES + MAX_TEXT_UTF8_BYTES ||
            !magic.indices.all { payload[it] == magic[it] } ||
            payload[4] != 1.toByte() || payload[5] != 0.toByte()
        ) return null
        val length = ((payload[6].toInt() and 255) shl 8) or (payload[7].toInt() and 255)
        if (length !in 1..textLimit(maximumFrameBytes) || payload.size != ENVELOPE_BYTES + length) return null
        return try {
            Charsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(payload, ENVELOPE_BYTES, length)).toString()
        } catch (_: CharacterCodingException) {
            null
        }
    }

    private fun textLimit(maximumFrameBytes: Int): Int =
        if (maximumFrameBytes <= PACKET_OVERHEAD_BYTES + ENVELOPE_BYTES) 0
        else minOf(MAX_TEXT_UTF8_BYTES, maximumFrameBytes - PACKET_OVERHEAD_BYTES - ENVELOPE_BYTES)
}
