package io.github.nbjelanovic.otclient

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

/** Phone-local preferences awaiting device application. Never evidence of completed setup. */
class V1SetupDraft private constructor(
    val deviceName: V1DeviceName?,
    val publicName: V1PublicName?,
    val radioRegion: V1RadioRegion?,
    val publiclyDiscoverable: Boolean,
) {
    override fun toString(): String = "V1SetupDraft(names=redacted, region=${radioRegion?.publicCode}, pending=true)"

    companion object {
        fun create(deviceName: String? = null, publicName: String? = null,
            radioRegion: V1RadioRegion? = null, publiclyDiscoverable: Boolean = true): V1SetupDraft? {
            val device = deviceName?.let { V1DeviceName.create(it) ?: return null }
            val person = publicName?.let { V1PublicName.create(it) ?: return null }
            // UTF-8 must encode losslessly; malformed surrogate text is never silently replaced.
            if (listOfNotNull(deviceName, publicName).any { !Charsets.UTF_8.newEncoder().canEncode(it) }) return null
            return V1SetupDraft(device, person, radioRegion, publiclyDiscoverable)
        }
    }
}

/** Fixed version and reserved-bit checks keep future formats from becoming trusted drafts. */
object V1SetupDraftCodec {
    const val MAX_BYTES = 512
    private const val MAGIC = 0x4f545344 // OTSD: engineering storage format, not a product identity.
    private const val VERSION = 1

    fun encode(draft: V1SetupDraft): ByteArray {
        val buffer = ByteArrayOutputStream()
        DataOutputStream(buffer).use { output ->
            output.writeInt(MAGIC)
            output.writeByte(VERSION)
            val flags = (if (draft.deviceName != null) 1 else 0) or
                (if (draft.publicName != null) 2 else 0) or
                (if (draft.radioRegion != null) 4 else 0) or
                (if (draft.publiclyDiscoverable) 8 else 0)
            output.writeByte(flags)
            listOfNotNull(draft.deviceName?.value, draft.publicName?.value).forEach { text ->
                val bytes = text.toByteArray(Charsets.UTF_8)
                output.writeShort(bytes.size)
                output.write(bytes)
            }
            draft.radioRegion?.let { region -> output.writeByte(when (region) { V1RadioRegion.US915 -> 1 }) }
        }
        return buffer.toByteArray().also { require(it.size <= MAX_BYTES) }
    }

    fun decode(encoded: ByteArray): V1SetupDraft? {
        if (encoded.size !in 6..MAX_BYTES) return null
        return runCatching {
            DataInputStream(ByteArrayInputStream(encoded)).use { input ->
                if (input.readInt() != MAGIC || input.readUnsignedByte() != VERSION) return null
                val flags = input.readUnsignedByte()
                if (flags and 0xf0 != 0) return null
                fun readText(): String {
                    val size = input.readUnsignedShort()
                    require(size in 1..input.available())
                    val bytes = ByteArray(size)
                    input.readFully(bytes)
                    return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                        .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
                }
                val deviceName = if (flags and 1 != 0) readText() else null
                val publicName = if (flags and 2 != 0) readText() else null
                val region = if (flags and 4 != 0) when (input.readUnsignedByte()) {
                    1 -> V1RadioRegion.US915
                    else -> return null
                } else null
                if (input.available() != 0) return null
                V1SetupDraft.create(deviceName, publicName, region, flags and 8 != 0)
            }
        }.getOrNull()
    }
}

/** The platform adapter stores only this bounded draft blob, separate from owner/session state. */
interface V1SetupDraftStorage {
    fun read(): ByteArray?
    fun write(encoded: ByteArray): Boolean
    fun clear(): Boolean
}

class V1SetupDraftRepository(private val storage: V1SetupDraftStorage) {
    fun load(): V1SetupDraft? = runCatching { storage.read()?.let(V1SetupDraftCodec::decode) }.getOrNull()
    fun save(draft: V1SetupDraft): Boolean = runCatching { storage.write(V1SetupDraftCodec.encode(draft)) }.getOrDefault(false)
    fun clear(): Boolean = runCatching { storage.clear() }.getOrDefault(false)
}
