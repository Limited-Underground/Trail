package io.github.nbjelanovic.otclient

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

/** Android-local retained projection, never membership, a session or permission to send. */
internal class V1GroupSnapshot private constructor(
    val deviceReference: String,
    val localMember: V1GroupMemberToken,
    val revision: Long,
    val membership: V1ActiveGroupMembership?,
) {
    override fun toString() = "V1GroupSnapshot(revision=$revision, fields=redacted)"

    companion object {
        fun create(
            deviceReference: String,
            localMember: V1GroupMemberToken,
            revision: Long,
            membership: V1ActiveGroupMembership?,
        ): V1GroupSnapshot? {
            if (!validDeviceReference(deviceReference) || revision <= 0) return null
            val copied = membership?.let {
                val profile = V1GroupProfile.create(
                    it.profile.administratorToken, it.profile.locationRule, it.profile.members,
                ) ?: return null
                if (profile.members.none { member -> member.token == localMember }) return null
                V1ActiveGroupMembership(it.groupToken, profile)
            }
            return V1GroupSnapshot(deviceReference, localMember, revision, copied)
        }
    }
}

private fun validDeviceReference(value: String): Boolean =
    value.length in 2..256 && value.length % 2 == 0 &&
        value.all { it in '0'..'9' || it in 'a'..'f' } && value.any { it != '0' }

/** Bounded LOCAL schema. The checksum detects damage; it authenticates nothing. Not a wire codec. */
internal object V1GroupSnapshotCodec {
    const val MAX_BYTES = 2048
    private const val MAGIC = 0x4f544750
    private const val SCHEMA = 1
    private const val HASH_BYTES = 32

    fun encode(value: V1GroupSnapshot): ByteArray? {
        val checked = V1GroupSnapshot.create(
            value.deviceReference, value.localMember, value.revision, value.membership,
        ) ?: return null
        val bytes = ByteArrayOutputStream()
        DataOutputStream(bytes).use { out ->
            out.writeInt(MAGIC)
            out.writeByte(SCHEMA)
            val device = checked.deviceReference.toByteArray(Charsets.US_ASCII)
            out.writeShort(device.size)
            out.write(device)
            out.write(checked.localMember.persistenceBytes())
            out.writeLong(checked.revision)
            val membership = checked.membership
            out.writeBoolean(membership != null)
            if (membership != null) {
                out.write(membership.groupToken.persistenceBytes())
                out.write(membership.profile.administratorToken.persistenceBytes())
                out.writeByte(membership.profile.locationRule.ordinal)
                out.writeByte(membership.profile.members.size)
                for (member in membership.profile.members) {
                    out.write(member.token.persistenceBytes())
                    out.writeByte((if (member.isAdministrator) 1 else 0) or
                        (if (member.locationSharingEnabled) 2 else 0))
                    val name = member.displayName.value.toByteArray(Charsets.UTF_8)
                    out.writeByte(name.size)
                    out.write(name)
                }
            }
        }
        val body = bytes.toByteArray()
        if (body.size > MAX_BYTES - HASH_BYTES) return null
        return body + MessageDigest.getInstance("SHA-256").digest(body)
    }

    fun decode(encoded: ByteArray): V1GroupSnapshot? {
        if (encoded.size !in (HASH_BYTES + 1)..MAX_BYTES) return null
        val body = encoded.copyOfRange(0, encoded.size - HASH_BYTES)
        if (!MessageDigest.isEqual(MessageDigest.getInstance("SHA-256").digest(body),
                encoded.copyOfRange(body.size, encoded.size))) return null
        return try {
            DataInputStream(ByteArrayInputStream(body)).use { input ->
                if (input.readInt() != MAGIC || input.readUnsignedByte() != SCHEMA) return null
                val deviceLength = input.readUnsignedShort()
                if (deviceLength !in 2..256) return null
                val device = input.exact(deviceLength).toString(Charsets.US_ASCII)
                val local = V1GroupMemberToken.create(input.exact(V1GroupMemberToken.SIZE_BYTES)) ?: return null
                val revision = input.readLong()
                val present = input.readUnsignedByte()
                if (present !in 0..1) return null
                val membership = if (present == 0) null else {
                    val token = V1GroupToken.create(input.exact(V1GroupToken.SIZE_BYTES)) ?: return null
                    val admin = V1GroupMemberToken.create(input.exact(V1GroupMemberToken.SIZE_BYTES)) ?: return null
                    val rule = V1GroupLocationRule.entries.getOrNull(input.readUnsignedByte()) ?: return null
                    val count = input.readUnsignedByte()
                    if (count !in 1..V1_MAX_GROUP_MEMBERS) return null
                    val members = (1..count).map {
                        val member = V1GroupMemberToken.create(input.exact(V1GroupMemberToken.SIZE_BYTES)) ?: return null
                        val flags = input.readUnsignedByte()
                        if (flags and 3 != flags) return null
                        val size = input.readUnsignedByte()
                        if (size !in 1..160) return null
                        val name = Charsets.UTF_8.newDecoder()
                            .onMalformedInput(CodingErrorAction.REPORT)
                            .onUnmappableCharacter(CodingErrorAction.REPORT)
                            .decode(ByteBuffer.wrap(input.exact(size))).toString()
                        V1GroupMember.create(member, name, flags and 1 != 0, flags and 2 != 0) ?: return null
                    }
                    V1ActiveGroupMembership(token, V1GroupProfile.create(admin, rule, members) ?: return null)
                }
                if (input.available() != 0) return null
                V1GroupSnapshot.create(device, local, revision, membership)
            }
        } catch (_: Exception) {
            null
        }
    }

    private fun DataInputStream.exact(size: Int): ByteArray = ByteArray(size).also(::readFully)
}

/** This boundary must replace atomically, compare expected bytes and throw on uncertain I/O. */
internal interface V1GroupSnapshotStorage {
    fun read(): ByteArray?
    fun replace(expected: ByteArray?, replacement: ByteArray): Boolean
}

/** Reference issued ONLY by a future admitted device session; never decoded from retained bytes. */
internal class V1GroupDeviceContext(val deviceReference: String, val localMember: V1GroupMemberToken) {
    init { require(validDeviceReference(deviceReference)) }
    override fun toString() = "V1GroupDeviceContext(redacted)"
}

internal sealed interface V1GroupDeviceRead {
    data object Unavailable : V1GroupDeviceRead
    data object Refused : V1GroupDeviceRead
    class Admitted(val context: V1GroupDeviceContext, val snapshot: V1GroupSnapshot) : V1GroupDeviceRead
}

/**
 * A future adapter owns authenticated read/actor admission and fresh complete device state.
 * The positive result is a trust boundary, not cryptographic verification implemented here.
 * No production implementation/issuer exists. Host tests substitute only this external boundary.
 */
internal interface V1GroupDeviceSource : AutoCloseable {
    fun read(context: V1GroupDeviceContext): V1GroupDeviceRead
    override fun close() = Unit
}

internal object UnavailableV1GroupDeviceSource : V1GroupDeviceSource {
    override fun read(context: V1GroupDeviceContext) = V1GroupDeviceRead.Unavailable
}

internal enum class V1GroupSnapshotRefusal {
    SOURCE_UNAVAILABLE, SOURCE_REFUSED, SOURCE_FAILED, CONTEXT_CHANGED, BINDING_MISMATCH,
    INVALID_SNAPSHOT, RETAINED_INVALID, REVISION_ROLLBACK, REVISION_CONFLICT,
    STORAGE_READ_FAILED, STORAGE_WRITE_FAILED, STORAGE_CONFLICT, READBACK_MISMATCH,
    REENTRANT, SOURCE_CLOSE_FAILED,
}

/**
 * Construction-thread-confined, one-context projection owner. Every public operation/read
 * rejects another thread before touching state or I/O. Reload never presents cached membership. A fresh
 * admitted device read must agree with retained revision/binding before any current view exists.
 * No commands, signing, membership activation or radio API. Refusal is terminal for this owner.
 */
internal class V1GroupSnapshotCoordinator(
    private val context: V1GroupDeviceContext,
    private val currentContext: () -> V1GroupDeviceContext?,
    private val storage: V1GroupSnapshotStorage,
    private val source: V1GroupDeviceSource = UnavailableV1GroupDeviceSource,
) : AutoCloseable {
    private val ownerThread = Thread.currentThread()
    private var refusal: V1GroupSnapshotRefusal? = null
    private var cleanupFailure = false
    val firstRefusal: V1GroupSnapshotRefusal?
        get() { checkOwnerThread(); return refusal }
    val cleanupFailed: Boolean
        get() { checkOwnerThread(); return cleanupFailure }
    private var currentBytes: ByteArray? = null
    private var busy = false
    private var closed = false

    val currentSnapshot: V1GroupSnapshot?
        get() {
            checkOwnerThread()
            if (busy || closed || refusal != null) return null
            busy = true
            return try { if (checkCurrent()) currentBytes?.let(V1GroupSnapshotCodec::decode) else null }
            finally { busy = false }
        }

    fun refresh(): Boolean {
        checkOwnerThread()
        if (closed || refusal != null) return false
        if (busy) return refuse(V1GroupSnapshotRefusal.REENTRANT)
        busy = true
        currentBytes = null
        try {
            if (!checkCurrent()) return false
            val result = try { source.read(context) }
                catch (_: Exception) { return refuse(V1GroupSnapshotRefusal.SOURCE_FAILED) }
            if (!checkCurrent()) return false
            val admitted = when (result) {
                V1GroupDeviceRead.Unavailable -> return refuse(V1GroupSnapshotRefusal.SOURCE_UNAVAILABLE)
                V1GroupDeviceRead.Refused -> return refuse(V1GroupSnapshotRefusal.SOURCE_REFUSED)
                is V1GroupDeviceRead.Admitted -> result
            }
            val fresh = admitted.snapshot
            if (admitted.context !== context || fresh.deviceReference != context.deviceReference ||
                fresh.localMember != context.localMember) return refuse(V1GroupSnapshotRefusal.BINDING_MISMATCH)
            val bytes = V1GroupSnapshotCodec.encode(fresh) ?: return refuse(V1GroupSnapshotRefusal.INVALID_SNAPSHOT)
            val retained = try { storage.read()?.copyOf() }
                catch (_: Exception) { return refuse(V1GroupSnapshotRefusal.STORAGE_READ_FAILED) }
            if (!checkCurrent()) return false
            if (retained != null) {
                val old = V1GroupSnapshotCodec.decode(retained) ?: return refuse(V1GroupSnapshotRefusal.RETAINED_INVALID)
                if (old.deviceReference != context.deviceReference || old.localMember != context.localMember)
                    return refuse(V1GroupSnapshotRefusal.BINDING_MISMATCH)
                if (fresh.revision < old.revision) return refuse(V1GroupSnapshotRefusal.REVISION_ROLLBACK)
                if (fresh.revision == old.revision && !bytes.contentEquals(retained))
                    return refuse(V1GroupSnapshotRefusal.REVISION_CONFLICT)
            }
            if (retained == null || !bytes.contentEquals(retained)) {
                val replaced = try { storage.replace(retained?.copyOf(), bytes.copyOf()) }
                    catch (_: Exception) { return refuse(V1GroupSnapshotRefusal.STORAGE_WRITE_FAILED) }
                if (!checkCurrent()) return false
                if (!replaced) return refuse(V1GroupSnapshotRefusal.STORAGE_CONFLICT)
            }
            val readback = try { storage.read() }
                catch (_: Exception) { return refuse(V1GroupSnapshotRefusal.STORAGE_READ_FAILED) }
            if (!checkCurrent()) return false
            if (readback == null || !bytes.contentEquals(readback)) return refuse(V1GroupSnapshotRefusal.READBACK_MISMATCH)
            currentBytes = bytes
            return true
        } finally { busy = false }
    }

    override fun close() {
        checkOwnerThread()
        if (closed) return
        closed = true
        currentBytes = null
        try { source.close() }
        catch (_: Exception) { cleanupFailure = true; refuse(V1GroupSnapshotRefusal.SOURCE_CLOSE_FAILED) }
    }

    private fun checkCurrent(): Boolean {
        if (closed || refusal != null) return false
        val current = try { currentContext() } catch (_: Exception) { null }
        if (closed || refusal != null) return false
        return current === context || refuse(V1GroupSnapshotRefusal.CONTEXT_CHANGED)
    }

    private fun refuse(reason: V1GroupSnapshotRefusal): Boolean {
        if (refusal == null) refusal = reason
        currentBytes = null
        return false
    }

    private fun checkOwnerThread() {
        check(Thread.currentThread() === ownerThread) { "Group snapshot owner accessed from another thread" }
    }
}
