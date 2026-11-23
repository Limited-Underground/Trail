package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

enum class CompanionLocationOperation(val wire: Int) {
    CONSENT(1), START(2), RENEW(3), STOP(4), WITHDRAW(5),
}

enum class CompanionLocationStatus(val wire: Int) {
    OK(1), DENIED(2), UNAVAILABLE(3),
}

data class CompanionLocationRequest(
    val operation: CompanionLocationOperation,
    val kind: Int,
    val slot: Int,
    val scopeId: ULong,
    val generation: ULong,
    val lease: ULong = 0uL,
    val sample: ULong = 0uL,
    val ageMs: Int = 0,
    val latitudeE4: Int = 0,
    val longitudeE4: Int = 0,
    val adminFingerprint: ByteArray = ByteArray(32),
    val rosterDigest: ByteArray = ByteArray(32),
) {
    override fun toString() = "CompanionLocationRequest(operation=$operation, fields=redacted)"
}

data class CompanionLocationReply(
    val operation: CompanionLocationOperation,
    val status: CompanionLocationStatus,
    val scopeId: ULong,
    val generation: ULong,
    val lease: ULong,
) {
    override fun toString() = "CompanionLocationReply(operation=$operation, status=$status, fields=redacted)"
}

/** Payload codec for protected configuration profile 5. The session supplies authority and exchange correlation. */
object CompanionLocationCodec {
    const val REQUEST_KIND = 0x0a
    const val RESPONSE_KIND = 0x8c
    const val REQUEST_BYTES = 112
    const val REPLY_BYTES = 28
    const val MAX_FIX_AGE_MS = 15_000
    const val GROUP_KIND = 1
    const val DIRECT_KIND = 2
    const val DIRECT_SLOT = 255
    private const val VERSION: Byte = 1
    private const val DISCRIMINATOR_BYTES = 32

    fun encode(request: CompanionLocationRequest): ByteArray? {
        if (!validRequest(request)) return null
        return ByteBuffer.allocate(REQUEST_BYTES).order(ByteOrder.BIG_ENDIAN)
            .put(VERSION)
            .put(request.operation.wire.toByte())
            .put(request.kind.toByte())
            .put(request.slot.toByte())
            .putLong(request.scopeId.toLong())
            .putLong(request.generation.toLong())
            .putLong(request.lease.toLong())
            .putLong(request.sample.toLong())
            .putInt(request.ageMs)
            .putInt(request.latitudeE4)
            .putInt(request.longitudeE4)
            .put(request.adminFingerprint)
            .put(request.rosterDigest)
            .array()
    }

    internal fun validCommand(payload: ByteArray): Boolean {
        if (payload.size != REQUEST_BYTES || payload[0] != VERSION) return false
        val operation = operation(payload[1]) ?: return false
        val fields = reader(payload)
        return validRequest(CompanionLocationRequest(
            operation,
            fields.get().unsigned(),
            fields.get().unsigned(),
            fields.long.toULong(),
            fields.long.toULong(),
            fields.long.toULong(),
            fields.long.toULong(),
            fields.int,
            fields.int,
            fields.int,
            ByteArray(DISCRIMINATOR_BYTES).also { fields.get(it) },
            ByteArray(DISCRIMINATOR_BYTES).also { fields.get(it) },
        ))
    }

    internal fun validReply(payload: ByteArray): Boolean = readReply(payload) != null

    fun decodeReply(payload: ByteArray, expected: CompanionLocationRequest): CompanionLocationReply? {
        if (!validRequest(expected)) return null
        return decodeReply(payload, expected.operation, expected.scopeId, expected.generation, expected.lease)
    }

    /** Binds the reply without retaining the position or roster bytes from the sent request. */
    fun decodeReply(
        payload: ByteArray,
        operation: CompanionLocationOperation,
        scopeId: ULong,
        generation: ULong,
        expectedLease: ULong = 0uL,
    ): CompanionLocationReply? {
        val reply = readReply(payload) ?: return null
        if (reply.operation != operation || reply.scopeId != scopeId || reply.generation != generation) return null
        if (reply.status == CompanionLocationStatus.OK &&
            operation == CompanionLocationOperation.RENEW && reply.lease != expectedLease
        ) return null
        return reply
    }

    private fun validRequest(request: CompanionLocationRequest): Boolean {
        if (request.scopeId == 0uL || request.generation == 0uL ||
            request.adminFingerprint.size != DISCRIMINATOR_BYTES || request.rosterDigest.size != DISCRIMINATOR_BYTES
        ) return false

        val hasAdmin = request.adminFingerprint.any { it != 0.toByte() }
        val hasDigest = request.rosterDigest.any { it != 0.toByte() }
        val validScope = when (request.kind) {
            GROUP_KIND -> request.slot in 0 until CompanionGroupConfigurationCodec.SLOT_COUNT &&
                request.generation <= UInt.MAX_VALUE.toULong() && hasAdmin && hasDigest
            DIRECT_KIND -> request.slot == DIRECT_SLOT && !hasAdmin && !hasDigest
            else -> false
        }
        if (!validScope) return false

        val zeroFix = request.sample == 0uL && request.ageMs == 0 &&
            request.latitudeE4 == 0 && request.longitudeE4 == 0
        val freshFix = request.sample != 0uL && request.ageMs in 0 until MAX_FIX_AGE_MS &&
            request.latitudeE4 in -900_000..900_000 && request.longitudeE4 in -1_800_000..1_800_000
        return when (request.operation) {
            CompanionLocationOperation.CONSENT, CompanionLocationOperation.WITHDRAW -> request.lease == 0uL && zeroFix
            CompanionLocationOperation.START -> request.lease == 0uL && freshFix
            CompanionLocationOperation.RENEW -> request.lease != 0uL && (freshFix || zeroFix)
            CompanionLocationOperation.STOP -> request.lease != 0uL && zeroFix
        }
    }

    private fun readReply(payload: ByteArray): CompanionLocationReply? {
        if (payload.size != REPLY_BYTES || payload[0] != VERSION || payload[3] != 0.toByte()) return null
        val operation = operation(payload[1]) ?: return null
        val status = CompanionLocationStatus.entries.firstOrNull { it.wire == payload[2].unsigned() } ?: return null
        val fields = reader(payload).apply { position(4) }
        val id = fields.long.toULong()
        val generation = fields.long.toULong()
        val lease = fields.long.toULong()
        val requiresLease = status == CompanionLocationStatus.OK &&
            (operation == CompanionLocationOperation.START || operation == CompanionLocationOperation.RENEW)
        if (id == 0uL || generation == 0uL || requiresLease != (lease != 0uL)) return null
        return CompanionLocationReply(operation, status, id, generation, lease)
    }

    private fun operation(wire: Byte) = CompanionLocationOperation.entries.firstOrNull { it.wire == wire.unsigned() }

    private fun reader(payload: ByteArray) = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN).apply { position(2) }

    private fun Byte.unsigned(): Int = toInt() and 255
}
