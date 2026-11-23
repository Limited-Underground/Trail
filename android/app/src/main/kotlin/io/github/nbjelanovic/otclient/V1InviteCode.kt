package io.github.nbjelanovic.otclient

import java.util.Base64

/**
 * Text form of a signed group invite. The same string is the QR payload and the text a person
 * can share or paste. The board verifies the invite; this only carries the bytes.
 */
object V1InviteCodeText {
    private const val PREFIX = "MESHTRAIL-INVITE-1:"
    private const val MAX_BYTES = 287

    fun encode(invite: ByteArray): String =
        PREFIX + Base64.getUrlEncoder().withoutPadding().encodeToString(invite)

    fun decode(text: String): ByteArray? {
        val trimmed = text.trim()
        if (!trimmed.startsWith(PREFIX)) return null
        val body = trimmed.removePrefix(PREFIX)
        if (body.isEmpty() || body.length > 400 || body.any { !(it.isLetterOrDigit() || it == '-' || it == '_') }) return null
        return try {
            Base64.getUrlDecoder().decode(body).takeIf { it.size <= MAX_BYTES && encode(it) == trimmed }
        } catch (_: IllegalArgumentException) {
            null
        }
    }
}

/**
 * Library-neutral QR boundary for invites. The QR library is an owner decision (no Google
 * Play Services: MeshTrail must work off-grid), so no implementation is bound yet and the
 * Group tab falls back to the invite code text.
 */
interface V1InviteQr {
    /** Square module matrix (true = dark) for [text], or null if it cannot be drawn. */
    fun matrix(text: String): Array<BooleanArray>?

    /** Starts a camera scan. [onResult] receives the decoded text, or null on cancel. */
    fun scan(onResult: (String?) -> Unit): Boolean
}

internal object V1InviteQrBinding {
    @Volatile var current: V1InviteQr? = null
}
