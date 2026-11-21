package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.util.Collections

class GroupDigest(bytes: ByteArray) {
    private val value = copyGroupBytes(bytes, 32)

    fun bytes(): ByteArray = value.copyOf()
    override fun equals(other: Any?) = other is GroupDigest && value.contentEquals(other.value)
    override fun hashCode() = value.contentHashCode()
    override fun toString() = "GroupDigest(redacted)"
}

enum class GroupLocationRule(val wireValue: Int) { OFF(0), PREFERRED(1), REQUIRED(2) }
enum class GroupRosterRole(val wireValue: Int) { ADMIN(1), MEMBER(2) }

class GroupRosterMember(
    val role: GroupRosterRole,
    val addedIn: UInt,
    publicKey: ByteArray,
    val alias: String,
) {
    private val key = copyGroupBytes(publicKey, 32)

    fun publicKey(): ByteArray = key.copyOf()
    override fun equals(other: Any?) = other is GroupRosterMember &&
        role == other.role && addedIn == other.addedIn && alias == other.alias && key.contentEquals(other.key)

    override fun hashCode(): Int {
        var result = role.hashCode()
        result = 31 * result + addedIn.hashCode()
        result = 31 * result + key.contentHashCode()
        return 31 * result + alias.hashCode()
    }

    override fun toString() = "GroupRosterMember(role=$role, addedIn=$addedIn, fields=redacted)"
}

/** Structural signed-roster bytes. The board supplies signature verification and fingerprint derivation. */
class GroupRoster(
    val groupId: ULong,
    val version: UInt,
    val previous: GroupDigest,
    val locationRule: GroupLocationRule,
    members: List<GroupRosterMember>,
    signature: ByteArray,
) {
    val members: List<GroupRosterMember> = Collections.unmodifiableList(members.toList())
    private val signatureBytes = copyGroupBytes(signature, 64)

    fun signature(): ByteArray = signatureBytes.copyOf()
    override fun equals(other: Any?) = other is GroupRoster && groupId == other.groupId &&
        version == other.version && previous == other.previous && locationRule == other.locationRule &&
        members == other.members && signatureBytes.contentEquals(other.signatureBytes)

    override fun hashCode(): Int {
        var result = groupId.hashCode()
        result = 31 * result + version.hashCode()
        result = 31 * result + previous.hashCode()
        result = 31 * result + locationRule.hashCode()
        result = 31 * result + members.hashCode()
        return 31 * result + signatureBytes.contentHashCode()
    }

    override fun toString() = "GroupRoster(version=" + version + ", members=" + members.size + ", fields=redacted)"
}

private fun copyGroupBytes(bytes: ByteArray, size: Int): ByteArray {
    require(bytes.size == size) { "Expected $size bytes" }
    return bytes.copyOf()
}

/**
 * The exact structural counterpart of firmware/components/identity/src/group_roster.cpp.
 * The parser establishes the signed format. The board supplies signature and trust evidence.
 */
object GroupRosterWire {
    const val MAX_MEMBERS = 6
    const val MAX_ALIAS_UTF8_BYTES = 16
    const val MAX_ROSTER_BYTES = 439
    private const val ROSTER_HEADER_BYTES = 51
    private const val MEMBER_FIXED_BYTES = 38
    private const val SIGNATURE_BYTES = 64
    private val rosterMagic = byteArrayOf(0x4f, 0x54, 0x47, 0x52)

    fun encode(roster: GroupRoster): ByteArray? {
        val aliases = validatedAliases(roster) ?: return null
        val size = ROSTER_HEADER_BYTES + SIGNATURE_BYTES + aliases.sumOf { MEMBER_FIXED_BYTES + it.size }
        val out = ByteArray(size)
        rosterMagic.copyInto(out)
        out[4] = 1
        putUnsigned(out, 5, 8, roster.groupId)
        putUnsigned(out, 13, 4, roster.version.toULong())
        roster.previous.bytes().copyInto(out, 17)
        out[49] = roster.locationRule.wireValue.toByte()
        out[50] = roster.members.size.toByte()
        var cursor = ROSTER_HEADER_BYTES
        roster.members.forEachIndexed { index, member ->
            out[cursor] = member.role.wireValue.toByte()
            putUnsigned(out, cursor + 1, 4, member.addedIn.toULong())
            member.publicKey().copyInto(out, cursor + 5)
            val alias = aliases[index]
            out[cursor + 37] = alias.size.toByte()
            alias.copyInto(out, cursor + MEMBER_FIXED_BYTES)
            cursor += MEMBER_FIXED_BYTES + alias.size
        }
        roster.signature().copyInto(out, cursor)
        return out
    }

    fun decode(encoded: ByteArray): GroupRoster? {
        if (encoded.size !in ROSTER_HEADER_BYTES + SIGNATURE_BYTES..MAX_ROSTER_BYTES) return null
        val bytes = encoded.copyOf()
        if (!rosterMagic.indices.all { bytes[it] == rosterMagic[it] } || unsignedByte(bytes[4]) != 1) return null
        val rule = GroupLocationRule.entries.find { it.wireValue == unsignedByte(bytes[49]) } ?: return null
        val memberCount = unsignedByte(bytes[50])
        if (memberCount !in 1..MAX_MEMBERS) return null
        val bodyEnd = bytes.size - SIGNATURE_BYTES
        var cursor = ROSTER_HEADER_BYTES
        val members = ArrayList<GroupRosterMember>(memberCount)
        repeat(memberCount) {
            if (bodyEnd - cursor < MEMBER_FIXED_BYTES) return null
            val role = GroupRosterRole.entries.find { it.wireValue == unsignedByte(bytes[cursor]) } ?: return null
            val addedIn = getUnsigned(bytes, cursor + 1, 4).toUInt()
            val key = bytes.copyOfRange(cursor + 5, cursor + 37)
            val aliasLength = unsignedByte(bytes[cursor + 37])
            cursor += MEMBER_FIXED_BYTES
            if (aliasLength !in 1..MAX_ALIAS_UTF8_BYTES || bodyEnd - cursor < aliasLength) return null
            val alias = decodeUtf8(bytes.copyOfRange(cursor, cursor + aliasLength)) ?: return null
            members.add(GroupRosterMember(role, addedIn, key, alias))
            cursor += aliasLength
        }
        if (cursor != bodyEnd) return null
        val roster = GroupRoster(
            getUnsigned(bytes, 5, 8), getUnsigned(bytes, 13, 4).toUInt(),
            GroupDigest(bytes.copyOfRange(17, 49)), rule, members, bytes.copyOfRange(bodyEnd, bytes.size),
        )
        return roster.takeIf { validatedAliases(it) != null }
    }

    private fun validatedAliases(roster: GroupRoster): List<ByteArray>? {
        if (roster.groupId == 0uL || roster.version == 0u ||
            (roster.version == 1u) != roster.previous.bytes().all { it == 0.toByte() } ||
            roster.members.size !in 1..MAX_MEMBERS ||
            roster.version == 1u && roster.members.size != 1
        ) return null
        var previousAdded = 0u
        val aliases = ArrayList<ByteArray>(roster.members.size)
        roster.members.forEachIndexed { index, member ->
            val expectedRole = if (index == 0) GroupRosterRole.ADMIN else GroupRosterRole.MEMBER
            if (member.role != expectedRole || index == 0 && member.addedIn != 1u ||
                member.addedIn <= previousAdded || member.addedIn > roster.version
            ) return null
            previousAdded = member.addedIn
            val alias = encodeUtf8(member.alias, MAX_ALIAS_UTF8_BYTES) ?: return null
            if (member.alias.any { it.code < 0x20 || it.code in 0x7f..0x9f }) return null
            if (roster.members.take(index).any {
                    it.publicKey().contentEquals(member.publicKey()) || it.alias == member.alias
                }
            ) return null
            aliases.add(alias)
        }
        return aliases
    }

    private fun encodeUtf8(text: String, maximum: Int): ByteArray? {
        if (text.isEmpty() || text.length > maximum) return null
        return try {
            val buffer = Charsets.UTF_8.newEncoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .encode(CharBuffer.wrap(text))
            if (buffer.remaining() !in 1..maximum) null
            else ByteArray(buffer.remaining()).also { buffer.get(it) }
        } catch (_: CharacterCodingException) {
            null
        }
    }

    private fun decodeUtf8(bytes: ByteArray): String? = try {
        Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(bytes)).toString()
    } catch (_: CharacterCodingException) {
        null
    }

    private fun unsignedByte(value: Byte) = value.toInt() and 255

    private fun getUnsigned(bytes: ByteArray, offset: Int, count: Int): ULong {
        var result = 0uL
        for (index in offset until offset + count) {
            result = (result shl 8) or unsignedByte(bytes[index]).toULong()
        }
        return result
    }

    private fun putUnsigned(out: ByteArray, offset: Int, count: Int, value: ULong) {
        var remaining = value
        for (index in offset + count - 1 downTo offset) {
            out[index] = remaining.toByte()
            remaining = remaining shr 8
        }
    }
}
