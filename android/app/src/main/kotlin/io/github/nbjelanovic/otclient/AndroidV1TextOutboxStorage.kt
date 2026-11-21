package io.github.nbjelanovic.otclient

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.AtomicFile
import io.github.nbjelanovic.otprotocol.OtxtPayloadCodec
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.FileNotFoundException
import java.nio.ByteBuffer
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** The outbox contains private message text. Save it outside Android backups, under an app key. */
internal class AndroidV1TextOutboxStorage(context: Context) : V1TextOutboxStorage {
    private val file = AtomicFile(context.noBackupFilesDir.resolve(FILE_NAME))

    override fun load(): V1TextOutboxSnapshot? = synchronized(fileLock) {
        try {
            val bytes = readFile()
            if (bytes.size !in MIN_FILE_BYTES..MAX_FILE_BYTES || bytes[0] != FILE_VERSION) {
                return@synchronized null
            }
            val key = key(create = false) ?: return@synchronized null
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(TAG_BITS, bytes, 1, IV_BYTES))
            cipher.updateAAD(byteArrayOf(FILE_VERSION))
            V1TextOutboxSnapshotCodec.decode(cipher.doFinal(bytes, 1 + IV_BYTES, bytes.size - 1 - IV_BYTES))
        } catch (_: FileNotFoundException) {
            if (fileExists()) null else V1TextOutboxSnapshot()
        } catch (_: Exception) {
            null
        }
    }

    override fun save(snapshot: V1TextOutboxSnapshot): Boolean = synchronized(fileLock) {
        val cleartext = V1TextOutboxSnapshotCodec.encode(snapshot) ?: return@synchronized false
        try {
            val key = key(create = !fileExists()) ?: return@synchronized false
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, key)
            if (cipher.iv.size != IV_BYTES) return@synchronized false
            cipher.updateAAD(byteArrayOf(FILE_VERSION))
            val bytes = byteArrayOf(FILE_VERSION) + cipher.iv + cipher.doFinal(cleartext)
            val output = file.startWrite()
            try {
                output.write(bytes)
                output.fd.sync()
                file.finishWrite(output)
            } catch (failure: Exception) {
                file.failWrite(output)
                throw failure
            }
            // AtomicFile.finishWrite can return after a failed rename, so verify the stored bytes.
            readFile().contentEquals(bytes)
        } catch (_: Exception) {
            false
        } finally {
            cleartext.fill(0)
        }
    }

    private fun readFile(): ByteArray = file.openRead().use { input ->
        val bytes = ByteArray(MAX_FILE_BYTES + 1)
        var used = 0
        while (used < bytes.size) {
            val count = input.read(bytes, used, bytes.size - used)
            if (count < 0) break
            used += count
        }
        bytes.copyOf(used)
    }

    private fun fileExists(): Boolean {
        val base = file.baseFile
        val parent = base.parentFile ?: return true
        return !parent.isDirectory || base.exists() ||
            parent.resolve("$FILE_NAME.new").exists() || parent.resolve("$FILE_NAME.bak").exists()
    }

    private fun key(create: Boolean): SecretKey? {
        val store = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        if (store.containsAlias(KEY_ALIAS)) return store.getKey(KEY_ALIAS, null) as? SecretKey
        if (!create) return null
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE).run {
            init(KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(256)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build())
            generateKey()
        }
    }

    private companion object {
        const val FILE_NAME = "v1-text-outbox.bin"
        const val KEY_ALIAS = "v1-text-outbox"
        const val KEYSTORE = "AndroidKeyStore"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val FILE_VERSION: Byte = 1
        const val IV_BYTES = 12
        const val TAG_BITS = 128
        const val MIN_FILE_BYTES = 1 + IV_BYTES + TAG_BITS / 8
        const val MAX_FILE_BYTES = MIN_FILE_BYTES + V1TextOutboxSnapshotCodec.MAX_BYTES
        val fileLock = Any()
    }
}

internal object V1TextOutboxSnapshotCodec {
    private const val VERSION = 1
    private const val RECIPIENT_BYTES = 64
    private const val ENTRY_BYTES = 8 + 1 + 1 + 1 + OtxtPayloadCodec.MAX_TEXT_UTF8_BYTES
    const val MAX_BYTES = 3 + RECIPIENT_BYTES + V1_TEXT_OUTBOX_LIMIT * ENTRY_BYTES
    private val digest = Regex("[0-9a-f]{64}")

    fun encode(snapshot: V1TextOutboxSnapshot): ByteArray? {
        if (!valid(snapshot)) return null
        val buffer = ByteArrayOutputStream()
        DataOutputStream(buffer).use { out ->
            out.writeByte(VERSION)
            val recipient = snapshot.recipient?.toByteArray(Charsets.US_ASCII)
            out.writeByte(recipient?.size ?: 0)
            if (recipient != null) out.write(recipient)
            out.writeByte(snapshot.entries.size)
            for (entry in snapshot.entries) {
                out.writeLong(entry.id)
                out.writeByte(entry.state.code())
                out.writeByte(entry.attempts)
                val text = entry.text.toByteArray(Charsets.UTF_8)
                out.writeByte(text.size)
                out.write(text)
            }
        }
        return buffer.toByteArray()
    }

    fun decode(bytes: ByteArray): V1TextOutboxSnapshot? {
        if (bytes.size !in 3..MAX_BYTES) return null
        return try {
            DataInputStream(ByteArrayInputStream(bytes)).use { input ->
                if (input.readUnsignedByte() != VERSION) return null
                val recipient = when (input.readUnsignedByte()) {
                    0 -> null
                    RECIPIENT_BYTES -> ByteArray(RECIPIENT_BYTES).also(input::readFully).toString(Charsets.US_ASCII)
                    else -> return null
                }
                val count = input.readUnsignedByte()
                if (count > V1_TEXT_OUTBOX_LIMIT) return null
                val entries = List(count) {
                    val id = input.readLong()
                    val state = phase(input.readUnsignedByte()) ?: return null
                    val attempts = input.readUnsignedByte()
                    val size = input.readUnsignedByte()
                    if (size !in 1..OtxtPayloadCodec.MAX_TEXT_UTF8_BYTES) return null
                    val textBytes = ByteArray(size).also(input::readFully)
                    val text = Charsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(textBytes)).toString()
                    V1TextOutboxEntry(id, text, state, attempts)
                }
                val snapshot = V1TextOutboxSnapshot(recipient, entries)
                snapshot.takeIf { input.read() == -1 && valid(it) }
            }
        } catch (_: java.io.IOException) {
            null
        }
    }

    private fun valid(snapshot: V1TextOutboxSnapshot): Boolean {
        val recipient = snapshot.recipient
        if (recipient == null) return snapshot.entries.isEmpty()
        if (!digest.matches(recipient) || snapshot.entries.size > V1_TEXT_OUTBOX_LIMIT) return false
        var lastId = 0L
        var hasActiveEntry = false
        for (entry in snapshot.entries) {
            if (entry.id <= lastId || entry.attempts !in 0..V1_TEXT_OUTBOX_MAX_ATTEMPTS ||
                V1TextComposer.refusal(entry.text) != null) return false
            when (entry.state) {
                V1TextOutboxPhase.WAITING -> {
                    if (entry.attempts == V1_TEXT_OUTBOX_MAX_ATTEMPTS) return false
                    hasActiveEntry = true
                }
                V1TextOutboxPhase.IN_FLIGHT -> {
                    if (hasActiveEntry || entry.attempts == 0) return false
                    hasActiveEntry = true
                }
                V1TextOutboxPhase.SENT, V1TextOutboxPhase.UNCONFIRMED -> {
                    if (entry.attempts == 0) return false
                }
                V1TextOutboxPhase.FAILED -> Unit
            }
            lastId = entry.id
        }
        return true
    }

    private fun V1TextOutboxPhase.code(): Int = when (this) {
        V1TextOutboxPhase.WAITING -> 1
        V1TextOutboxPhase.IN_FLIGHT -> 2
        V1TextOutboxPhase.SENT -> 3
        V1TextOutboxPhase.FAILED -> 4
        V1TextOutboxPhase.UNCONFIRMED -> 5
    }

    private fun phase(code: Int): V1TextOutboxPhase? = when (code) {
        1 -> V1TextOutboxPhase.WAITING
        2 -> V1TextOutboxPhase.IN_FLIGHT
        3 -> V1TextOutboxPhase.SENT
        4 -> V1TextOutboxPhase.FAILED
        5 -> V1TextOutboxPhase.UNCONFIRMED
        else -> null
    }
}
