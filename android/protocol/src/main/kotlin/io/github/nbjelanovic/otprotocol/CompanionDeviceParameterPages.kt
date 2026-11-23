package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Two single-indication reads within one protected configuration session. */
object CompanionDeviceParameterPages {
    const val REQUEST = 0x09
    const val RESPONSE = 0x8b
    const val DEADLINE_MILLIS = 5_000L

    private const val HEADER_BYTES = 8
    private const val FIRST_PAGE_BYTES = 120
    private val requestMagic = byteArrayOf(0x4f, 0x54, 0x44, 0x51)
    private val responseMagic = byteArrayOf(0x4f, 0x54, 0x44, 0x52)

    /** A nonzero first exchange requests the remainder of that exact snapshot. */
    fun request(firstExchange: UInt = 0u): ByteArray {
        val secondPage = firstExchange != 0u
        return ByteBuffer.allocate(if (secondPage) 12 else 8).order(ByteOrder.LITTLE_ENDIAN).apply {
            put(requestMagic)
            put(1)
            put(if (secondPage) 1.toByte() else 0.toByte())
            putShort(0)
            if (secondPage) putInt(firstExchange.toInt())
        }.array()
    }

    internal fun validRequest(bytes: ByteArray): Boolean {
        if (bytes.size == HEADER_BYTES) return headerMatches(bytes, requestMagic, 0)
        return bytes.size == HEADER_BYTES + 4 && headerMatches(bytes, requestMagic, 1) &&
            (HEADER_BYTES until bytes.size).any { bytes[it] != 0.toByte() }
    }

    internal fun validResponse(bytes: ByteArray): Boolean =
        matchesResponse(bytes, 0) || matchesResponse(bytes, 1)

    fun responsePage(bytes: ByteArray, page: Int): ByteArray? =
        if (matchesResponse(bytes, page)) bytes.copyOfRange(HEADER_BYTES, bytes.size) else null

    private fun matchesResponse(bytes: ByteArray, page: Int): Boolean {
        val pageBytes = when (page) {
            0 -> FIRST_PAGE_BYTES
            1 -> CompanionDeviceParametersCodec.RECORD_BYTES - FIRST_PAGE_BYTES
            else -> return false
        }
        return bytes.size == HEADER_BYTES + pageBytes && headerMatches(bytes, responseMagic, page)
    }

    private fun headerMatches(bytes: ByteArray, magic: ByteArray, page: Int) =
        magic.indices.all { bytes[it] == magic[it] } && bytes[4] == 1.toByte() &&
            bytes[5] == page.toByte() && bytes[6] == 0.toByte() && bytes[7] == 0.toByte()
}
