package io.github.nbjelanovic.otclient

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.WriterException
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import com.journeyapps.barcodescanner.ScanOptions

/**
 * D-170 (c): group invite QR with open-source ZXing, no Google Play services. Showing uses
 * ZXing core; scanning uses zxing-android-embedded's capture screen, which asks for the
 * camera permission only when the person taps Scan.
 */
internal class ZxingInviteQr(private val launch: (ScanOptions) -> Unit) : V1InviteQr {
    private var pending: ((String?) -> Unit)? = null

    override fun matrix(text: String): Array<BooleanArray>? = qrMatrix(text)

    override fun scan(onResult: (String?) -> Unit): Boolean {
        if (pending != null) return false
        pending = onResult
        val options = ScanOptions()
            .setDesiredBarcodeFormats(ScanOptions.QR_CODE)
            .setPrompt("Scan the MeshTrail invitation")
            .setBeepEnabled(false)
            .setOrientationLocked(false)
        return try {
            launch(options)
            true
        } catch (_: RuntimeException) {
            pending = null
            false
        }
    }

    /** The capture screen's result; null when cancelled. */
    fun delivered(text: String?) {
        val callback = pending ?: return
        pending = null
        callback(text)
    }

    companion object {
        /** Module matrix (true = dark), no quiet zone; the screen adds the margin. */
        fun qrMatrix(text: String): Array<BooleanArray>? = try {
            val bits = QRCodeWriter().encode(
                text,
                BarcodeFormat.QR_CODE,
                0,
                0,
                mapOf(EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.M, EncodeHintType.MARGIN to 0),
            )
            Array(bits.height) { y -> BooleanArray(bits.width) { x -> bits.get(x, y) } }
        } catch (_: WriterException) {
            null
        } catch (_: IllegalArgumentException) {
            null
        }
    }
}
