package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

enum class CompanionGroupOperation(val wire: Int) {
    DISCOVER(1), CREATE(2), STOP_TRUST(3), READ_ROSTER(4),
    /** F8 live Invite and Join. Invite issues a boot-scoped signed invite on the admin board. */
    INVITE(5), READ_INVITE(6), JOIN(7),
}

/** One page of a signed group invite, or Invite's ID and total. */
class CompanionGroupInvitePage(
    inviteId: ByteArray?,
    val offset: Int,
    val totalBytes: Int,
    bytes: ByteArray,
) {
    private val id = inviteId?.copyOf()
    private val page = bytes.copyOf()
    val inviteId: ByteArray? get() = id?.copyOf()
    val bytes: ByteArray get() = page.copyOf()
    override fun toString() = "CompanionGroupInvitePage(offset=$offset, total=$totalBytes, bytes=redacted)"
}

/**
 * The fields of a signed group invite that a joiner reviews. The joiner's board verifies the
 * admin signature when it accepts the invite; this projection checks only the layout.
 */
class CompanionGroupInvite(
    adminKey: ByteArray,
    val groupId: ULong,
    val rosterVersion: UInt,
    val locationRule: Int,
    val label: String,
    inviteId: ByteArray,
) {
    private val admin = adminKey.copyOf()
    private val id = inviteId.copyOf()
    val adminKey: ByteArray get() = admin.copyOf()
    val inviteId: ByteArray get() = id.copyOf()
    override fun toString() = "CompanionGroupInvite(redacted)"
}

enum class CompanionGroupStatus(val wire: Int) {
    OK(0), UNAVAILABLE(1), REFUSED(2), STALE(3), UNCERTAIN(4), MISSING(5),
}

enum class CompanionGroupRosterState(val wire: Int) {
    ACTIVE(1), DISTRUSTED(2),
}

/** The Heltec uses the administrator's raw Ed25519 public key as its fingerprint. */
class CompanionGroupScope(adminFingerprint: ByteArray, val groupId: ULong, rosterDigest: ByteArray) {
    private val admin = adminFingerprint.copyOf()
    private val digest = rosterDigest.copyOf()
    val adminFingerprint: ByteArray get() = admin.copyOf()
    val rosterDigest: ByteArray get() = digest.copyOf()

    internal fun valid() = groupId != 0uL && admin.size == 32 && digest.size == 32 &&
        admin.any { it != 0.toByte() } && digest.any { it != 0.toByte() }

    override fun equals(other: Any?) = other is CompanionGroupScope &&
        groupId == other.groupId && admin.contentEquals(other.admin) && digest.contentEquals(other.digest)

    override fun hashCode() = 31 * (31 * admin.contentHashCode() + groupId.hashCode()) + digest.contentHashCode()
    override fun toString() = "CompanionGroupScope(redacted)"
}

class CompanionGroupChunk(
    val slot: Int,
    val state: CompanionGroupRosterState,
    val offset: Int,
    val totalBytes: Int,
    bytes: ByteArray,
) {
    private val fragment = bytes.copyOf()
    val bytes: ByteArray get() = fragment.copyOf()
    override fun toString() = "CompanionGroupChunk(state=$state, bytes=redacted)"
}

data class CompanionGroupResult(
    val operation: CompanionGroupOperation,
    val status: CompanionGroupStatus,
    val scope: CompanionGroupScope? = null,
    val chunk: CompanionGroupChunk? = null,
    val invite: CompanionGroupInvitePage? = null,
)

class CompanionGroupRosterMember(
    val alias: String,
    val administrator: Boolean,
    fingerprint: ByteArray,
) {
    private val key = fingerprint.copyOf()
    val fingerprint: ByteArray get() = key.copyOf()
    override fun toString() = "CompanionGroupRosterMember(administrator=$administrator, fields=redacted)"
}

data class CompanionGroupRoster(
    val version: UInt,
    val locationRule: Int,
    val members: List<CompanionGroupRosterMember>,
)

/** Mirrors the firmware OTGC payload carried in negotiated OTC0 minor 5. */
object CompanionGroupConfigurationCodec {
    const val REQUEST_KIND = 0x0b
    const val RESPONSE_KIND = 0x8f
    const val SLOT_COUNT = 4
    const val MAX_CHUNK_BYTES = 42
    const val MAX_ROSTER_BYTES = GroupRosterWire.MAX_ROSTER_BYTES

    private const val HEADER_BYTES = 8
    private const val SCOPE_BYTES = 72
    private const val READ_POSITION_BYTES = 4
    private const val READ_COMMAND_BYTES = HEADER_BYTES + READ_POSITION_BYTES
    private const val READ_SCOPE_OFFSET = READ_COMMAND_BYTES + 2
    private const val READ_RESULT_BYTES = READ_SCOPE_OFFSET + SCOPE_BYTES
    private val magic = "OTGC".encodeToByteArray()
    private val digestDomain = "OpenTrail group roster digest v1".encodeToByteArray()

    fun discover(): ByteArray = command(HEADER_BYTES, CompanionGroupOperation.DISCOVER)

    fun create(locationRule: Int): ByteArray? {
        if (locationRule !in 0..2) return null
        return command(HEADER_BYTES + 1, CompanionGroupOperation.CREATE).also {
            it[HEADER_BYTES] = locationRule.toByte()
        }
    }

    fun stopTrust(scope: CompanionGroupScope): ByteArray? {
        if (!scope.valid()) return null
        return command(HEADER_BYTES + SCOPE_BYTES, CompanionGroupOperation.STOP_TRUST).also {
            writeScope(it, HEADER_BYTES, scope)
        }
    }

    fun readRoster(
        slot: Int,
        offset: Int = 0,
        scope: CompanionGroupScope? = null,
        state: CompanionGroupRosterState? = null,
    ): ByteArray? {
        if (!positionValid(slot, offset)) return null
        if (offset == 0 && (scope != null || state != null)) return null
        if (offset != 0 && (scope?.valid() != true || state == null)) return null
        val size = if (offset == 0) READ_COMMAND_BYTES else READ_COMMAND_BYTES + SCOPE_BYTES
        return command(size, CompanionGroupOperation.READ_ROSTER).also {
            it[HEADER_BYTES] = slot.toByte()
            it[HEADER_BYTES + 1] = (state?.wire ?: 0).toByte()
            littleEndian(it).putShort(HEADER_BYTES + 2, offset.toShort())
            if (scope != null) writeScope(it, READ_COMMAND_BYTES, scope)
        }
    }

    const val INVITE_ID_BYTES = 16
    const val MIN_INVITE_BYTES = 167
    const val MAX_INVITE_BYTES = 287
    const val INVITE_PAGE_BYTES = 96
    const val MAX_INVITE_LABEL_BYTES = 47
    private const val INVITE_COMMAND_FIXED = HEADER_BYTES + SCOPE_BYTES + 1
    private const val READ_INVITE_COMMAND_BYTES = HEADER_BYTES + INVITE_ID_BYTES + 2
    private const val JOIN_FIXED = HEADER_BYTES + 4
    private const val INVITE_RESULT_BYTES = HEADER_BYTES + SCOPE_BYTES + INVITE_ID_BYTES + 2
    private const val READ_INVITE_RESULT_FIXED = HEADER_BYTES + INVITE_ID_BYTES + 4
    private const val JOIN_RESULT_FINAL_BYTES = JOIN_FIXED + SCOPE_BYTES + INVITE_ID_BYTES
    private val inviteMagic = "OTGQ".encodeToByteArray()

    /** Admin board: issue (or repeat) the live invite for [scope], shown to joiners as [label]. */
    fun invite(scope: CompanionGroupScope, label: String): ByteArray? {
        val text = label.encodeToByteArray()
        if (!scope.valid() || !labelValid(label, text)) return null
        return command(INVITE_COMMAND_FIXED + text.size, CompanionGroupOperation.INVITE).also {
            writeScope(it, HEADER_BYTES, scope)
            it[HEADER_BYTES + SCOPE_BYTES] = text.size.toByte()
            text.copyInto(it, INVITE_COMMAND_FIXED)
        }
    }

    fun readInvite(inviteId: ByteArray, offset: Int): ByteArray? {
        if (!idValid(inviteId) || offset !in 0 until MAX_INVITE_BYTES || offset % INVITE_PAGE_BYTES != 0) return null
        return command(READ_INVITE_COMMAND_BYTES, CompanionGroupOperation.READ_INVITE).also {
            inviteId.copyInto(it, HEADER_BYTES)
            littleEndian(it).putShort(HEADER_BYTES + INVITE_ID_BYTES, offset.toShort())
        }
    }

    /** Joiner board: one page of the scanned invite. Pages are sent in order from offset zero. */
    fun joinPage(invite: ByteArray, offset: Int): ByteArray? {
        val count = invitePage(offset, invite.size)
        if (count == 0) return null
        return command(JOIN_FIXED + count, CompanionGroupOperation.JOIN).also {
            littleEndian(it).putShort(HEADER_BYTES, offset.toShort())
            littleEndian(it).putShort(HEADER_BYTES + 2, invite.size.toShort())
            invite.copyInto(it, JOIN_FIXED, offset, offset + count)
        }
    }

    /** Layout check of a signed invite. The joiner's board verifies its signature. */
    fun decodeInvite(bytes: ByteArray): CompanionGroupInvite? {
        if (bytes.size !in MIN_INVITE_BYTES..MAX_INVITE_BYTES ||
            !inviteMagic.indices.all { bytes[it] == inviteMagic[it] } ||
            bytes[4] != 1.toByte() || bytes[5] != 1.toByte() || bytes[6] != 2.toByte() || bytes[7] != 0.toByte()
        ) return null
        val labelBytes = u8(bytes[102])
        val rule = u8(bytes[100])
        if (bytes.size != MIN_INVITE_BYTES + labelBytes || rule !in 0..2 || bytes[101] != 1.toByte()) return null
        val group = ByteBuffer.wrap(bytes).getLong(40).toULong()  // descriptor integers are big-endian
        val version = ByteBuffer.wrap(bytes).getInt(48).toUInt()
        val label = bytes.copyOfRange(103, 103 + labelBytes)
        val text = label.decodeToString()
        if (group == 0uL || version == 0u || !text.encodeToByteArray().contentEquals(label) ||
            !labelValid(text, label)
        ) return null
        val id = bytes.copyOfRange(84, 100)
        if (!idValid(id)) return null
        return CompanionGroupInvite(bytes.copyOfRange(8, 40), group, version, rule, text, id)
    }

    private fun labelValid(text: String, bytes: ByteArray) = bytes.size in 1..MAX_INVITE_LABEL_BYTES &&
        text.isNotEmpty() && text.first() != ' ' && text.last() != ' ' && text.codePointCount(0, text.length) <= 40 &&
        text.none { it.isISOControl() }

    private fun idValid(id: ByteArray) = id.size == INVITE_ID_BYTES && id.any { it != 0.toByte() }

    private fun invitePage(offset: Int, total: Int): Int =
        if (total !in MIN_INVITE_BYTES..MAX_INVITE_BYTES || offset !in 0 until total || offset % INVITE_PAGE_BYTES != 0) 0
        else minOf(INVITE_PAGE_BYTES, total - offset)

    private fun validInviteCommand(operation: CompanionGroupOperation, bytes: ByteArray): Boolean = when (operation) {
        CompanionGroupOperation.INVITE -> bytes.size >= INVITE_COMMAND_FIXED &&
            readScope(bytes, HEADER_BYTES) != null &&
            u8(bytes[HEADER_BYTES + SCOPE_BYTES]).let { size ->
                size in 1..MAX_INVITE_LABEL_BYTES && bytes.size == INVITE_COMMAND_FIXED + size
            }
        CompanionGroupOperation.READ_INVITE -> bytes.size == READ_INVITE_COMMAND_BYTES &&
            idValid(bytes.copyOfRange(HEADER_BYTES, HEADER_BYTES + INVITE_ID_BYTES)) &&
            u16(bytes, HEADER_BYTES + INVITE_ID_BYTES).let { it < MAX_INVITE_BYTES && it % INVITE_PAGE_BYTES == 0 }
        else -> bytes.size >= JOIN_FIXED &&
            invitePage(u16(bytes, HEADER_BYTES), u16(bytes, HEADER_BYTES + 2)).let { it != 0 && bytes.size == JOIN_FIXED + it }
    }

    private fun decodeInviteResult(
        operation: CompanionGroupOperation,
        status: CompanionGroupStatus,
        bytes: ByteArray,
    ): CompanionGroupResult? {
        if (status == CompanionGroupStatus.MISSING) return null
        if (status != CompanionGroupStatus.OK) {
            return CompanionGroupResult(operation, status).takeIf { bytes.size == HEADER_BYTES }
        }
        return when (operation) {
            CompanionGroupOperation.INVITE -> {
                if (bytes.size != INVITE_RESULT_BYTES) return null
                val scope = readScope(bytes, HEADER_BYTES) ?: return null
                val id = bytes.copyOfRange(HEADER_BYTES + SCOPE_BYTES, HEADER_BYTES + SCOPE_BYTES + INVITE_ID_BYTES)
                val total = u16(bytes, HEADER_BYTES + SCOPE_BYTES + INVITE_ID_BYTES)
                if (!idValid(id) || total !in MIN_INVITE_BYTES..MAX_INVITE_BYTES) return null
                CompanionGroupResult(operation, status, scope, invite = CompanionGroupInvitePage(id, 0, total, ByteArray(0)))
            }
            CompanionGroupOperation.READ_INVITE -> {
                if (bytes.size < READ_INVITE_RESULT_FIXED) return null
                val id = bytes.copyOfRange(HEADER_BYTES, HEADER_BYTES + INVITE_ID_BYTES)
                val offset = u16(bytes, HEADER_BYTES + INVITE_ID_BYTES)
                val total = u16(bytes, HEADER_BYTES + INVITE_ID_BYTES + 2)
                val count = invitePage(offset, total)
                if (!idValid(id) || count == 0 || bytes.size != READ_INVITE_RESULT_FIXED + count) return null
                CompanionGroupResult(operation, status, invite = CompanionGroupInvitePage(
                    id, offset, total, bytes.copyOfRange(READ_INVITE_RESULT_FIXED, bytes.size)))
            }
            else -> {
                if (bytes.size != JOIN_FIXED && bytes.size != JOIN_RESULT_FINAL_BYTES) return null
                val offset = u16(bytes, HEADER_BYTES)
                val total = u16(bytes, HEADER_BYTES + 2)
                val count = invitePage(offset, total)
                if (count == 0) return null
                val last = offset + count == total
                if (last != (bytes.size == JOIN_RESULT_FINAL_BYTES)) return null
                if (!last) return CompanionGroupResult(operation, status,
                    invite = CompanionGroupInvitePage(null, offset, total, ByteArray(0)))
                val scope = readScope(bytes, JOIN_FIXED) ?: return null
                val id = bytes.copyOfRange(JOIN_FIXED + SCOPE_BYTES, JOIN_RESULT_FINAL_BYTES)
                if (!idValid(id)) return null
                CompanionGroupResult(operation, status, scope, invite = CompanionGroupInvitePage(id, offset, total, ByteArray(0)))
            }
        }
    }

    internal fun validCommand(bytes: ByteArray): Boolean {
        val operation = operation(bytes) ?: return false
        if (bytes[6] != 0.toByte()) return false
        return when (operation) {
            CompanionGroupOperation.INVITE, CompanionGroupOperation.READ_INVITE, CompanionGroupOperation.JOIN ->
                validInviteCommand(operation, bytes)
            CompanionGroupOperation.DISCOVER -> bytes.size == HEADER_BYTES
            CompanionGroupOperation.CREATE -> bytes.size == HEADER_BYTES + 1 && u8(bytes[HEADER_BYTES]) in 0..2
            CompanionGroupOperation.STOP_TRUST ->
                bytes.size == HEADER_BYTES + SCOPE_BYTES && readScope(bytes, HEADER_BYTES) != null
            CompanionGroupOperation.READ_ROSTER -> {
                if (bytes.size < READ_COMMAND_BYTES) return false
                val slot = u8(bytes[HEADER_BYTES])
                val state = u8(bytes[HEADER_BYTES + 1])
                val offset = u16(bytes, HEADER_BYTES + 2)
                if (!positionValid(slot, offset)) return false
                if (offset == 0) bytes.size == READ_COMMAND_BYTES && state == 0
                else bytes.size == READ_COMMAND_BYTES + SCOPE_BYTES &&
                    rosterState(state) != null && readScope(bytes, READ_COMMAND_BYTES) != null
            }
        }
    }

    fun decodeResult(bytes: ByteArray): CompanionGroupResult? {
        val operation = operation(bytes) ?: return null
        val status = CompanionGroupStatus.entries.firstOrNull { it.wire == u8(bytes[6]) } ?: return null
        if (operation == CompanionGroupOperation.INVITE || operation == CompanionGroupOperation.READ_INVITE ||
            operation == CompanionGroupOperation.JOIN
        ) return decodeInviteResult(operation, status, bytes)
        if (status == CompanionGroupStatus.MISSING && operation != CompanionGroupOperation.READ_ROSTER) return null
        if (operation == CompanionGroupOperation.DISCOVER) {
            if (status != CompanionGroupStatus.OK && status != CompanionGroupStatus.UNAVAILABLE) return null
            return CompanionGroupResult(operation, status).takeIf { bytes.size == HEADER_BYTES }
        }
        if (status != CompanionGroupStatus.OK) {
            return CompanionGroupResult(operation, status).takeIf { bytes.size == HEADER_BYTES }
        }
        if (operation != CompanionGroupOperation.READ_ROSTER) {
            if (bytes.size != HEADER_BYTES + SCOPE_BYTES) return null
            return CompanionGroupResult(operation, status, readScope(bytes, HEADER_BYTES) ?: return null)
        }
        if (bytes.size !in READ_RESULT_BYTES + 1..READ_RESULT_BYTES + MAX_CHUNK_BYTES) return null
        val slot = u8(bytes[HEADER_BYTES])
        val state = rosterState(u8(bytes[HEADER_BYTES + 1])) ?: return null
        val offset = u16(bytes, HEADER_BYTES + 2)
        val total = u16(bytes, READ_COMMAND_BYTES)
        if (!positionValid(slot, offset) || total !in offset + 1..MAX_ROSTER_BYTES ||
            bytes.size - READ_RESULT_BYTES != minOf(MAX_CHUNK_BYTES, total - offset)
        ) return null
        val scope = readScope(bytes, READ_SCOPE_OFFSET) ?: return null
        val chunk = CompanionGroupChunk(slot, state, offset, total, bytes.copyOfRange(READ_RESULT_BYTES, bytes.size))
        return CompanionGroupResult(operation, status, scope, chunk)
    }

    /**
     * Check the exact assembled signed bytes against the scope returned by the protected board.
     * The board authenticates the Ed25519 signature on read. This projection checks the signed
     * format, qualified group and digest and does not verify the signature independently.
     */
    fun decodeRoster(bytes: ByteArray, scope: CompanionGroupScope): CompanionGroupRoster? {
        if (!scope.valid() || bytes.size > MAX_ROSTER_BYTES) return null
        val encoded = bytes.copyOf()
        val roster = GroupRosterWire.decode(encoded) ?: return null
        val admin = roster.members.first().publicKey()
        if (roster.groupId != scope.groupId || !admin.contentEquals(scope.adminFingerprint)) return null
        val digest = MessageDigest.getInstance("SHA-256").apply { update(digestDomain) }.digest(encoded)
        if (!MessageDigest.isEqual(digest, scope.rosterDigest)) return null
        return CompanionGroupRoster(
            roster.version,
            roster.locationRule.wireValue,
            roster.members.map { member ->
                CompanionGroupRosterMember(member.alias, member.role == GroupRosterRole.ADMIN, member.publicKey())
            },
        )
    }

    private fun operation(bytes: ByteArray): CompanionGroupOperation? {
        if (bytes.size < HEADER_BYTES || !magic.indices.all { bytes[it] == magic[it] } ||
            bytes[4] != 1.toByte() || bytes[7] != 0.toByte()
        ) return null
        return CompanionGroupOperation.entries.firstOrNull { it.wire == u8(bytes[5]) }
    }

    private fun rosterState(wire: Int) = CompanionGroupRosterState.entries.firstOrNull { it.wire == wire }

    private fun command(size: Int, operation: CompanionGroupOperation) = ByteArray(size).also {
        magic.copyInto(it)
        it[4] = 1
        it[5] = operation.wire.toByte()
    }

    private fun positionValid(slot: Int, offset: Int) =
        slot in 0 until SLOT_COUNT && offset in 0 until MAX_ROSTER_BYTES && offset % MAX_CHUNK_BYTES == 0

    private fun readScope(bytes: ByteArray, start: Int): CompanionGroupScope? {
        val idOffset = start + 32
        val digestOffset = idOffset + 8
        return CompanionGroupScope(
            bytes.copyOfRange(start, idOffset),
            littleEndian(bytes).getLong(idOffset).toULong(),
            bytes.copyOfRange(digestOffset, start + SCOPE_BYTES),
        ).takeIf { it.valid() }
    }

    private fun writeScope(bytes: ByteArray, start: Int, scope: CompanionGroupScope) {
        scope.adminFingerprint.copyInto(bytes, start)
        littleEndian(bytes).putLong(start + 32, scope.groupId.toLong())
        scope.rosterDigest.copyInto(bytes, start + 40)
    }

    private fun littleEndian(bytes: ByteArray) = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    private fun u8(value: Byte) = value.toInt() and 255
    private fun u16(bytes: ByteArray, index: Int) = littleEndian(bytes).getShort(index).toInt() and 65535
}
