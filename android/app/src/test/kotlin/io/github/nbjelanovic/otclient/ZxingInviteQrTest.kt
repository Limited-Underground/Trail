package io.github.nbjelanovic.otclient

import com.google.zxing.BinaryBitmap
import com.google.zxing.RGBLuminanceSource
import com.google.zxing.common.HybridBinarizer
import com.google.zxing.qrcode.QRCodeReader
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ZxingInviteQrTest {
    @Test
    fun largestInviteDrawsAndReadsBackExactly() {
        val invite = ByteArray(287) { (it * 37 + 11).toByte() }
        val code = V1InviteCodeText.encode(invite)
        val matrix = assertNotNull(ZxingInviteQr.qrMatrix(code))
        assertEquals(matrix.size, matrix.first().size)
        assertTrue(matrix.size <= 101, "Version ${(matrix.size - 17) / 4} is too dense for a phone screen")
        assertEquals(code, decode(matrix))
        assertContentEquals(invite, V1InviteCodeText.decode(decode(matrix)))
    }

    @Test
    fun scanDeliversOnceAndRefusesOverlap() {
        var launches = 0
        val qr = ZxingInviteQr { launches++ }
        val results = mutableListOf<String?>()
        assertTrue(qr.scan { results += it })
        assertFalse(qr.scan { results += "second" })
        qr.delivered("MESHTRAIL-INVITE-1:AA")
        qr.delivered("late")
        assertEquals(listOf<String?>("MESHTRAIL-INVITE-1:AA"), results)
        assertTrue(qr.scan { results += it })
        qr.delivered(null)  // cancelled
        assertEquals(listOf("MESHTRAIL-INVITE-1:AA", null), results)
        assertEquals(2, launches)
        val failing = ZxingInviteQr { throw IllegalStateException("no activity") }
        assertFalse(failing.scan { results += it })
        assertNull(ZxingInviteQr.qrMatrix(""))
    }

    private fun decode(matrix: Array<BooleanArray>): String {
        val scale = 4
        val margin = 4 * scale
        val size = matrix.size * scale + 2 * margin
        val pixels = IntArray(size * size) { 0xFFFFFF }
        for (y in matrix.indices) for (x in matrix.indices) if (matrix[y][x]) {
            for (dy in 0 until scale) for (dx in 0 until scale) {
                pixels[(margin + y * scale + dy) * size + margin + x * scale + dx] = 0
            }
        }
        val bitmap = BinaryBitmap(HybridBinarizer(RGBLuminanceSource(size, size, pixels)))
        return QRCodeReader().decode(bitmap).text
    }
}
