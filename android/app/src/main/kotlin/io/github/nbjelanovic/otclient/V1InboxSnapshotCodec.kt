package io.github.nbjelanovic.otclient

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.zip.CRC32

/** Private app-storage format only. It is not a radio/BLE format or encryption. */
internal object V1InboxSnapshotCodec {
    const val MAX_ENCODED_BYTES = 1_048_576
    const val MAX_RECORDS = 256
    const val MAX_TEXT_BYTES = 262_144
    private const val MAGIC = 0x4f544953 // OTIS
    private const val VERSION = 1
    private const val HEADER_BYTES = 46
    private const val RECORD_FIXED_BYTES = 45

    fun encode(snapshot: V1InboxSnapshot): ByteArray? {
        return try {
        if (!validPolicy(snapshot.policy) || snapshot.version != VERSION || snapshot.generation == 0uL ||
            snapshot.records.size > snapshot.policy.records || snapshot.records.size > MAX_RECORDS ||
            snapshot.records.map { it.key }.distinct().size != snapshot.records.size) return null
        var totalTextBytes = 0L
        var encodedBytes = HEADER_BYTES.toLong() + Int.SIZE_BYTES
        for (record in snapshot.records) {
            val text = record.text.toByteArray(Charsets.UTF_8)
            if (!validRecord(snapshot.policy, record, text)) return null
            totalTextBytes += text.size
            encodedBytes += RECORD_FIXED_BYTES + text.size +
                if (record.directPeer == null) 0 else V1DirectPeerToken.SIZE_BYTES
            if (totalTextBytes > snapshot.policy.utf8Bytes || encodedBytes > MAX_ENCODED_BYTES) return null
        }
        val body = ByteArrayOutputStream()
        DataOutputStream(body).use { output ->
            output.writeInt(MAGIC)
            output.writeByte(VERSION)
            output.write(snapshot.device.copyBytes())
            output.writeShort(snapshot.policy.records)
            output.writeInt(snapshot.policy.utf8Bytes)
            output.writeLong(snapshot.policy.retentionMs)
            output.writeByte(1) // REJECT_NEW
            output.writeLong(snapshot.generation.toLong())
            output.writeShort(snapshot.records.size)
            for (record in snapshot.records) writeRecord(output, snapshot.policy, record)
        }
        val payload = body.toByteArray()
        if (payload.size + Int.SIZE_BYTES > MAX_ENCODED_BYTES) return null
        val checksum = CRC32().apply { update(payload) }.value.toInt()
        ByteArrayOutputStream(payload.size + Int.SIZE_BYTES).also { result ->
            result.write(payload)
            DataOutputStream(result).use { it.writeInt(checksum) }
        }.toByteArray()
        } catch (_: Exception) {
            null
        }
    }

    fun decode(encoded: ByteArray): V1InboxSnapshot? {
        return try {
        if (encoded.size !in (HEADER_BYTES + Int.SIZE_BYTES)..MAX_ENCODED_BYTES) return null
        val payloadSize = encoded.size - Int.SIZE_BYTES
        val expected = ByteBuffer.wrap(encoded, payloadSize, Int.SIZE_BYTES).int
        val actual = CRC32().apply { update(encoded, 0, payloadSize) }.value.toInt()
        if (actual != expected) return null
        DataInputStream(ByteArrayInputStream(encoded, 0, payloadSize)).use { input ->
            if (input.readInt() != MAGIC || input.readUnsignedByte() != VERSION) return null
            val deviceBytes = ByteArray(16).also(input::readFully)
            val device = V1InboxDeviceToken.create(deviceBytes) ?: return null
            val policy = V1InboxPolicy(
                records = input.readUnsignedShort(),
                utf8Bytes = input.readInt(),
                retentionMs = input.readLong(),
                full = when (input.readUnsignedByte()) { 1 -> V1InboxFullPolicy.REJECT_NEW else -> return null },
            )
            if (!validPolicy(policy)) return null
            val generation = input.readLong().toULong()
            if (generation == 0uL) return null
            val count = input.readUnsignedShort()
            if (count > policy.records || count > MAX_RECORDS) return null
            val records = ArrayList<V1InboxStoredRecord>(count)
            var totalTextBytes = 0L
            repeat(count) {
                val record = readRecord(input, policy) ?: return null
                totalTextBytes += record.text.toByteArray(Charsets.UTF_8).size
                if (totalTextBytes > policy.utf8Bytes || records.any { it.key == record.key }) return null
                records += record
            }
            if (input.available() != 0) return null
            V1InboxSnapshot(device, policy, records, generation)
        }
        } catch (_: Exception) {
            null
        }
    }

    private fun validPolicy(policy: V1InboxPolicy) = policy.valid() &&
        policy.records <= MAX_RECORDS && policy.utf8Bytes <= MAX_TEXT_BYTES

    private fun writeRecord(output: DataOutputStream, policy: V1InboxPolicy, record: V1InboxStoredRecord) {
        val text = record.text.toByteArray(Charsets.UTF_8)
        require(validRecord(policy, record, text))
        output.writeLong(record.key.context.toLong())
        output.writeLong(record.key.recipient.toLong())
        output.writeInt(record.key.epoch.toInt())
        output.writeLong(record.key.source.toLong())
        output.writeInt(record.key.message.toInt())
        var flags = if (record.directPeer != null) 1 else 0
        if (record.opened) flags = flags or 2
        if (record.importPending) flags = flags or 4
        if (record.readPending) flags = flags or 8
        output.writeByte(flags)
        output.writeLong(record.remainingMs)
        record.directPeer?.let { output.write(it.copyBytes()) }
        output.writeInt(text.size)
        output.write(text)
    }

    private fun validRecord(policy: V1InboxPolicy, record: V1InboxStoredRecord, text: ByteArray) =
        record.key.valid() && text.isNotEmpty() && record.text.isNotBlank() && text.size <= policy.utf8Bytes &&
            text.size <= MAX_TEXT_BYTES && Charsets.UTF_8.newEncoder().canEncode(record.text) &&
            record.text.none { it.isISOControl() && it != '\n' } &&
            record.remainingMs in 0..policy.retentionMs &&
            (record.remainingMs > 0 || record.importPending || record.readPending) &&
            (!record.readPending || record.opened)

    private fun readRecord(input: DataInputStream, policy: V1InboxPolicy): V1InboxStoredRecord? {
        if (input.available() < RECORD_FIXED_BYTES) return null
        val key = V1InboxKey(input.readLong().toULong(), input.readLong().toULong(),
            input.readInt().toUInt(), input.readLong().toULong(), input.readInt().toUInt())
        if (!key.valid()) return null
        val flags = input.readUnsignedByte()
        if (flags and 0xf0 != 0) return null
        val remaining = input.readLong()
        val peer = if (flags and 1 != 0) {
            if (input.available() < V1DirectPeerToken.SIZE_BYTES) return null
            V1DirectPeerToken.create(ByteArray(V1DirectPeerToken.SIZE_BYTES).also(input::readFully)) ?: return null
        } else null
        if (input.available() < Int.SIZE_BYTES) return null
        val size = input.readInt()
        if (size !in 1..minOf(policy.utf8Bytes, MAX_TEXT_BYTES) || size > input.available()) return null
        val bytes = ByteArray(size).also(input::readFully)
        val text = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
        val opened = flags and 2 != 0
        val importPending = flags and 4 != 0
        val readPending = flags and 8 != 0
        if (text.isBlank() || text.any { it.isISOControl() && it != '\n' } ||
            remaining !in 0..policy.retentionMs ||
            (remaining == 0L && !importPending && !readPending) || (readPending && !opened)) return null
        return V1InboxStoredRecord(key, text, peer, remaining, opened, importPending, readPending)
    }
}
