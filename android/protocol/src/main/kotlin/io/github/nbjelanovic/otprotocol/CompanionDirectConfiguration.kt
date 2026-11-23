package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

enum class CompanionDirectOperation(val wire: Int) {
    BEGIN(1), REVIEW(2), ACCEPT(3), DECLINE(4), FINISH(5), READ(6), END(7), BLOCK(8), UNBLOCK(9),
    READ_BLOCKED(10), SEND(11), RECEIVED(12), DELIVERY(13),
}

/** Same order as the board's DirectResult. */
enum class CompanionDirectStatus(val wire: Int) {
    OK(0), BUSY(1), REFUSED(2), BLOCKED(3), FULL(4), EXPIRED(5), STALE(6), UNAVAILABLE(7),
}

enum class CompanionDirectState(val wire: Int) {
    EMPTY(0), WAITING_FOR_PEER(1), ACCEPTED(2), READY(3), ENDING(4),
}

enum class CompanionDirectDelivery(val wire: Int) {
    QUEUED(1), DELIVERED(2), FAILED(3), UNCONFIRMED(4),
}

enum class CompanionDirectRole(val wire: Int) { START(1), JOIN(2) }

/** The board's authenticated peer and the direct scope from the verified transcript. */
class CompanionDirectBinding(peer: ByteArray, scope: ByteArray) {
    private val peerKey = peer.copyOf()
    private val scopeKey = scope.copyOf()
    val peer: ByteArray get() = peerKey.copyOf()
    val scope: ByteArray get() = scopeKey.copyOf()

    internal fun valid() = peerKey.size == ID_BYTES && scopeKey.size == ID_BYTES &&
        peerKey.any { it != 0.toByte() } && scopeKey.any { it != 0.toByte() }

    /** Short grouped hex of the peer key, for comparing out loud with the other person. */
    fun peerCheckCode(): String = peerKey.copyOfRange(0, 6).joinToString("") { "%02X".format(it) }
        .chunked(4).joinToString(" ")

    override fun equals(other: Any?) = other is CompanionDirectBinding &&
        peerKey.contentEquals(other.peerKey) && scopeKey.contentEquals(other.scopeKey)
    override fun hashCode() = 31 * peerKey.contentHashCode() + scopeKey.contentHashCode()
    override fun toString() = "CompanionDirectBinding(redacted)"

    internal companion object { const val ID_BYTES = 32 }
}

class CompanionDirectText(val slot: Int, val generation: ULong, val messageId: UInt, val text: String) {
    override fun toString() = "CompanionDirectText(slot=$slot, fields=redacted)"
}

sealed interface CompanionDirectCommand {
    data class Begin(val role: CompanionDirectRole, val code: Int) : CompanionDirectCommand
    data object Review : CompanionDirectCommand
    data class Accept(val binding: CompanionDirectBinding) : CompanionDirectCommand
    data object Decline : CompanionDirectCommand
    data class Finish(val binding: CompanionDirectBinding) : CompanionDirectCommand
    data class Read(val slot: Int) : CompanionDirectCommand
    data class End(val binding: CompanionDirectBinding) : CompanionDirectCommand
    data class Block(val binding: CompanionDirectBinding) : CompanionDirectCommand
    class Unblock(peer: ByteArray) : CompanionDirectCommand {
        private val key = peer.copyOf()
        val peer: ByteArray get() = key.copyOf()
        override fun equals(other: Any?) = other is Unblock && key.contentEquals(other.key)
        override fun hashCode() = key.contentHashCode()
        override fun toString() = "Unblock(redacted)"
    }
    data class ReadBlocked(val slot: Int) : CompanionDirectCommand
    data class Send(val text: CompanionDirectText) : CompanionDirectCommand
}

sealed interface CompanionDirectResult {
    val status: CompanionDirectStatus

    /** Status-only reply to BEGIN, ACCEPT, DECLINE, FINISH, END, BLOCK, UNBLOCK, SEND, or any refusal. */
    data class Plain(val operation: CompanionDirectOperation, override val status: CompanionDirectStatus) :
        CompanionDirectResult
    data class Reviewed(val binding: CompanionDirectBinding) : CompanionDirectResult {
        override val status get() = CompanionDirectStatus.OK
    }
    data class Conversation(
        val slot: Int,
        val state: CompanionDirectState,
        val generation: ULong,
        val binding: CompanionDirectBinding?,
    ) : CompanionDirectResult {
        override val status get() = CompanionDirectStatus.OK
    }
    class BlockedPeer(val slot: Int, peer: ByteArray?) : CompanionDirectResult {
        private val key = peer?.copyOf()
        val peer: ByteArray? get() = key?.copyOf()
        override val status get() = CompanionDirectStatus.OK
        override fun toString() = "BlockedPeer(slot=$slot, redacted)"
    }
    data class Received(val text: CompanionDirectText) : CompanionDirectResult {
        override val status get() = CompanionDirectStatus.OK
    }
    data class Delivery(val slot: Int, val generation: ULong, val messageId: UInt, val phase: CompanionDirectDelivery) :
        CompanionDirectResult {
        override val status get() = CompanionDirectStatus.OK
    }
}

/**
 * Mirrors the firmware OTDC v1 payload (companion_direct_configuration.hpp). Wire checks only:
 * the board's direct conversation owner decides consent, block and traffic authority.
 */
object CompanionDirectConfigurationCodec {
    const val CONVERSATIONS = 8
    const val BLOCKED = 16
    const val MAX_TEXT_BYTES = 90
    const val MAX_PAYLOAD_BYTES = 128
    const val CODE_MIN = 1
    const val CODE_MAX = 999_999

    private const val HEADER = 8
    private const val ID = CompanionDirectBinding.ID_BYTES
    private const val TEXT_HEADER = HEADER + 16
    private const val READ_RESULT = HEADER + 12 + 2 * ID
    private const val BLOCKED_RESULT = HEADER + 4 + ID
    private val magic = "OTDC".encodeToByteArray()

    fun encode(command: CompanionDirectCommand): ByteArray? = when (command) {
        is CompanionDirectCommand.Begin ->
            if (command.code !in CODE_MIN..CODE_MAX) null
            else header(HEADER + 5, CompanionDirectOperation.BEGIN).also {
                it[HEADER] = command.role.wire.toByte(); le(it).putInt(HEADER + 1, command.code)
            }
        CompanionDirectCommand.Review -> header(HEADER, CompanionDirectOperation.REVIEW)
        CompanionDirectCommand.Decline -> header(HEADER, CompanionDirectOperation.DECLINE)
        is CompanionDirectCommand.Accept -> bindingCommand(CompanionDirectOperation.ACCEPT, command.binding)
        is CompanionDirectCommand.Finish -> bindingCommand(CompanionDirectOperation.FINISH, command.binding)
        is CompanionDirectCommand.End -> bindingCommand(CompanionDirectOperation.END, command.binding)
        is CompanionDirectCommand.Block -> bindingCommand(CompanionDirectOperation.BLOCK, command.binding)
        is CompanionDirectCommand.Unblock -> command.peer.takeIf { it.size == ID && it.any { b -> b != 0.toByte() } }
            ?.let { peer -> header(HEADER + ID, CompanionDirectOperation.UNBLOCK).also { peer.copyInto(it, HEADER) } }
        is CompanionDirectCommand.Read -> if (command.slot !in 0 until CONVERSATIONS) null
            else header(HEADER + 1, CompanionDirectOperation.READ).also { it[HEADER] = command.slot.toByte() }
        is CompanionDirectCommand.ReadBlocked -> if (command.slot !in 0 until BLOCKED) null
            else header(HEADER + 1, CompanionDirectOperation.READ_BLOCKED).also { it[HEADER] = command.slot.toByte() }
        is CompanionDirectCommand.Send -> textRecord(CompanionDirectOperation.SEND, command.text)
    }

    fun decodeCommand(bytes: ByteArray): CompanionDirectCommand? {
        val op = operation(bytes) ?: return null
        if (bytes[6] != 0.toByte()) return null
        val command = when (op) {
            CompanionDirectOperation.BEGIN -> {
                if (bytes.size != HEADER + 5) return null
                val role = CompanionDirectRole.entries.firstOrNull { it.wire == u8(bytes[HEADER]) } ?: return null
                CompanionDirectCommand.Begin(role, le(bytes).getInt(HEADER + 1))
            }
            CompanionDirectOperation.REVIEW -> CompanionDirectCommand.Review.takeIf { bytes.size == HEADER }
            CompanionDirectOperation.DECLINE -> CompanionDirectCommand.Decline.takeIf { bytes.size == HEADER }
            CompanionDirectOperation.ACCEPT -> readBinding(bytes)?.let { CompanionDirectCommand.Accept(it) }
            CompanionDirectOperation.FINISH -> readBinding(bytes)?.let { CompanionDirectCommand.Finish(it) }
            CompanionDirectOperation.END -> readBinding(bytes)?.let { CompanionDirectCommand.End(it) }
            CompanionDirectOperation.BLOCK -> readBinding(bytes)?.let { CompanionDirectCommand.Block(it) }
            CompanionDirectOperation.UNBLOCK ->
                if (bytes.size != HEADER + ID) null
                else CompanionDirectCommand.Unblock(bytes.copyOfRange(HEADER, HEADER + ID))
            CompanionDirectOperation.READ ->
                if (bytes.size != HEADER + 1) null else CompanionDirectCommand.Read(u8(bytes[HEADER]))
            CompanionDirectOperation.READ_BLOCKED ->
                if (bytes.size != HEADER + 1) null else CompanionDirectCommand.ReadBlocked(u8(bytes[HEADER]))
            CompanionDirectOperation.SEND -> readText(bytes)?.let { CompanionDirectCommand.Send(it) }
            CompanionDirectOperation.RECEIVED, CompanionDirectOperation.DELIVERY -> null
        } ?: return null
        // Canonical form only: the same bytes must come back.
        return command.takeIf { encode(it)?.contentEquals(bytes) == true }
    }

    fun decodeResult(bytes: ByteArray): CompanionDirectResult? {
        val op = operation(bytes) ?: return null
        val status = CompanionDirectStatus.entries.firstOrNull { it.wire == u8(bytes[6]) } ?: return null
        val event = op == CompanionDirectOperation.RECEIVED || op == CompanionDirectOperation.DELIVERY
        if (status != CompanionDirectStatus.OK || bytes.size == HEADER) {
            if (bytes.size != HEADER || event) return null
            val needsBody = op == CompanionDirectOperation.REVIEW || op == CompanionDirectOperation.READ ||
                op == CompanionDirectOperation.READ_BLOCKED
            if (status == CompanionDirectStatus.OK && needsBody) return null
            return CompanionDirectResult.Plain(op, status)
        }
        return when (op) {
            CompanionDirectOperation.REVIEW -> readBinding(bytes)?.let { CompanionDirectResult.Reviewed(it) }
            CompanionDirectOperation.READ -> readConversation(bytes)
            CompanionDirectOperation.READ_BLOCKED -> {
                if (bytes.size != BLOCKED_RESULT || !zero(bytes, HEADER + 1, 3)) return null
                val slot = u8(bytes[HEADER])
                if (slot >= BLOCKED) return null
                val peer = bytes.copyOfRange(HEADER + 4, HEADER + 4 + ID)
                CompanionDirectResult.BlockedPeer(slot, peer.takeIf { p -> p.any { it != 0.toByte() } })
            }
            CompanionDirectOperation.RECEIVED -> readText(bytes)?.let { CompanionDirectResult.Received(it) }
            CompanionDirectOperation.DELIVERY -> {
                if (bytes.size != TEXT_HEADER || !zero(bytes, HEADER + 2, 2)) return null
                val slot = u8(bytes[HEADER])
                val phase = CompanionDirectDelivery.entries.firstOrNull { it.wire == u8(bytes[HEADER + 1]) } ?: return null
                val generation = le(bytes).getLong(HEADER + 4).toULong()
                if (slot >= CONVERSATIONS || generation == 0uL) return null
                CompanionDirectResult.Delivery(slot, generation, le(bytes).getInt(HEADER + 12).toUInt(), phase)
            }
            else -> null
        }
    }

    private fun readConversation(bytes: ByteArray): CompanionDirectResult.Conversation? {
        if (bytes.size != READ_RESULT || !zero(bytes, HEADER + 2, 2)) return null
        val slot = u8(bytes[HEADER])
        val state = CompanionDirectState.entries.firstOrNull { it.wire == u8(bytes[HEADER + 1]) } ?: return null
        val generation = le(bytes).getLong(HEADER + 4).toULong()
        val peer = bytes.copyOfRange(HEADER + 12, HEADER + 12 + ID)
        val scope = bytes.copyOfRange(HEADER + 12 + ID, HEADER + 12 + 2 * ID)
        if (slot >= CONVERSATIONS) return null
        if (state == CompanionDirectState.EMPTY) {
            if (generation != 0uL || peer.any { it != 0.toByte() } || scope.any { it != 0.toByte() }) return null
            return CompanionDirectResult.Conversation(slot, state, 0uL, null)
        }
        val binding = CompanionDirectBinding(peer, scope).takeIf { it.valid() } ?: return null
        if ((state == CompanionDirectState.READY) != (generation != 0uL)) return null
        return CompanionDirectResult.Conversation(slot, state, generation, binding)
    }

    private fun bindingCommand(op: CompanionDirectOperation, binding: CompanionDirectBinding): ByteArray? {
        if (!binding.valid()) return null
        return header(HEADER + 2 * ID, op).also {
            binding.peer.copyInto(it, HEADER); binding.scope.copyInto(it, HEADER + ID)
        }
    }

    private fun readBinding(bytes: ByteArray): CompanionDirectBinding? {
        if (bytes.size != HEADER + 2 * ID) return null
        return CompanionDirectBinding(bytes.copyOfRange(HEADER, HEADER + ID), bytes.copyOfRange(HEADER + ID, HEADER + 2 * ID))
            .takeIf { it.valid() }
    }

    private fun textRecord(op: CompanionDirectOperation, text: CompanionDirectText): ByteArray? {
        val body = text.text.encodeToByteArray()
        if (text.slot !in 0 until CONVERSATIONS || text.generation == 0uL ||
            body.isEmpty() || body.size > MAX_TEXT_BYTES || body.decodeToString() != text.text
        ) return null
        return header(TEXT_HEADER + body.size, op).also {
            it[HEADER] = text.slot.toByte()
            le(it).putLong(HEADER + 4, text.generation.toLong()).putInt(HEADER + 12, text.messageId.toInt())
            body.copyInto(it, TEXT_HEADER)
        }
    }

    private fun readText(bytes: ByteArray): CompanionDirectText? {
        if (bytes.size !in TEXT_HEADER + 1..TEXT_HEADER + MAX_TEXT_BYTES || !zero(bytes, HEADER + 1, 3)) return null
        val slot = u8(bytes[HEADER])
        val generation = le(bytes).getLong(HEADER + 4).toULong()
        if (slot >= CONVERSATIONS || generation == 0uL) return null
        val body = bytes.copyOfRange(TEXT_HEADER, bytes.size)
        val text = runCatching {
            Charsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(body)).toString()
        }.getOrNull() ?: return null
        return CompanionDirectText(slot, generation, le(bytes).getInt(HEADER + 12).toUInt(), text)
    }

    private fun operation(bytes: ByteArray): CompanionDirectOperation? {
        if (bytes.size !in HEADER..MAX_PAYLOAD_BYTES || !magic.indices.all { bytes[it] == magic[it] } ||
            bytes[4] != 1.toByte() || bytes[7] != 0.toByte()
        ) return null
        return CompanionDirectOperation.entries.firstOrNull { it.wire == u8(bytes[5]) }
    }

    private fun header(size: Int, op: CompanionDirectOperation) = ByteArray(size).also {
        magic.copyInto(it); it[4] = 1; it[5] = op.wire.toByte()
    }

    private fun le(bytes: ByteArray) = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    private fun u8(value: Byte) = value.toInt() and 0xff
    private fun zero(bytes: ByteArray, from: Int, count: Int) = (from until from + count).all { bytes[it] == 0.toByte() }
}
