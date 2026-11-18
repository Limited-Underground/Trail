package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionAuthorityCodec
import io.github.nbjelanovic.otprotocol.CompanionAuthorityRequest

/**
 * Volatile consumer of the protected owner channel's completed OTY0 exchange. This class cannot
 * authenticate its inputs: the service's protected owner must supply independently pinned identity,
 * membership and live attachment context. No event, journal cache or caller Boolean grants proof.
 * One instance serves at most one query/attachment. After invalidation, a fresh protected session
 * and a new owner-held instance are required. Production does not wire this provider.
 */
internal class BlePacketAuthorityAdapter(
    private val owner: Any,
    private val monotonicMillis: () -> Long,
) : BlePacketAuthorityProvider {
    enum class Invalidation {
        DISCONNECT, LEASE_CHANGE, OWNER_REPLACEMENT, RESTART_AMBIGUITY, REKEY,
        MEMBERSHIP_CHANGE, REVOCATION, INVALID_RESPONSE, EXPIRY_OR_ROLLBACK,
    }
    private class Pending(
        val query: BlePacketAuthorityQuery, val request: CompanionAuthorityRequest,
        val localPin: ByteArray, val membershipRevision: ULong, val started: Long,
    )
    private var pending: Pending? = null
    private var proof: BlePacketAuthority? = null
    private var spent = false
    private var retired = false
    private var lastClock: Long? = null

    /** Called only by the protected owner, before asynchronous I/O; never by provider.current. */
    @Synchronized fun begin(
        owner: Any, query: BlePacketAuthorityQuery, localIdentity: ByteArray,
        remoteIdentity: ByteArray, group: ULong, epoch: UInt, membershipRevision: ULong,
        exchange: UInt,
    ): ByteArray? {
        if (owner !== this.owner || spent || retired) return null
        spent = true
        val now = sample() ?: return null
        if (query.callbackGeneration <= 0 || query.peer.isBlank() || localIdentity.size != 32 ||
            localIdentity.all { it == 0.toByte() } || localIdentity.contentEquals(remoteIdentity) ||
            membershipRevision == 0uL) { invalidate(Invalidation.INVALID_RESPONSE); return null }
        val encoded = CompanionAuthorityCodec.encodeRequest(remoteIdentity, group, epoch, query.nonce, exchange)
            ?: run { invalidate(Invalidation.INVALID_RESPONSE); return null }
        val request = checkNotNull(CompanionAuthorityCodec.decodeRequest(encoded))
        pending = Pending(query, request, localIdentity.copyOf(), membershipRevision, now)
        return encoded
    }

    /** The owner routes only the response of that protected exchange here. A refusal retires it. */
    @Synchronized fun complete(owner: Any, lease: BleGattLease, callbackGeneration: Long, bytes: ByteArray) {
        if (retired) return
        val waiting = pending
        pending = null // Every completion consumes the slot, including malformed/refused responses.
        val now = sample()
        if (owner !== this.owner || waiting == null || now == null ||
            lease !== waiting.query.lease || callbackGeneration != waiting.query.callbackGeneration) {
            invalidate(Invalidation.INVALID_RESPONSE); return
        }
        val response = CompanionAuthorityCodec.decodeResponse(bytes)
        if (response == null || response.isRefusal ||
            !CompanionAuthorityCodec.matchesRequest(response, waiting.request, waiting.localPin) ||
            response.membershipRevision != waiting.membershipRevision) {
            invalidate(Invalidation.INVALID_RESPONSE); return
        }
        val deadline = CompanionAuthorityCodec.deadlineMillis(waiting.started, response.remainingMillis, now)
            ?: run { invalidate(Invalidation.EXPIRY_OR_ROLLBACK); return }
        // Flags3 was strictly decoded; these values are derived from the wire, not Boolean admission.
        proof = BlePacketAuthority(lease, callbackGeneration, response.nonce, waiting.query.peer,
            response.group, response.sessionGeneration, response.deviceGeneration, deadline, wireBinding = response)
    }

    @Synchronized override fun current(query: BlePacketAuthorityQuery): BlePacketAuthority? {
        if (retired) return null
        val value = proof ?: return null
        val now = sample() ?: return null
        if (value.lease !== query.lease || value.callbackGeneration != query.callbackGeneration ||
            value.nonce != query.nonce || value.peer != query.peer) {
            invalidate(Invalidation.LEASE_CHANGE); return null
        }
        if (now >= value.expiresAtMillis) { invalidate(Invalidation.EXPIRY_OR_ROLLBACK); return null }
        return value
    }

    /** Must be dispatched by the real owner for every listed lifecycle boundary, before reuse. */
    @Synchronized fun invalidate(reason: Invalidation) {
        retired = true
        pending = null
        proof = null
        lastInvalidation = reason
    }
    var lastInvalidation: Invalidation? = null
        private set

    private fun sample(): Long? {
        val now = try { monotonicMillis() } catch (_: Exception) { -1L }
        if (retired) return null // A callback may synchronously retire the owner.
        val prior = lastClock
        if (now < 0 || prior != null && now < prior) {
            invalidate(Invalidation.EXPIRY_OR_ROLLBACK); return null
        }
        lastClock = now
        return now
    }
    override fun toString() = "BlePacketAuthorityAdapter(fields=redacted)"
}
