package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionAuthorityCodec
import java.lang.reflect.Proxy
import kotlin.test.*

/** Protected-owner contract fixtures only; these bytes are not a target issuer/authentication. */
class BlePacketAuthorityAdapterTest {
    private val success = hex("4f54433000068b000403020108070605000174004f545930010103000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f400100000000000000020000000300000000000000030000000000000004000000000000000403020130750000")
    private val refusal = hex("4f54433000068b000403020108070605000174004f54593001010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000403020100000000")
    private val request = hex("4f544330000609000403020108070605000138004f545930010100002122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f4001000000000000000200000004030201")
    private val local = ByteArray(32) { (it + 1).toByte() }
    private val remote = ByteArray(32) { (it + 33).toByte() }
    private fun lease(): BleGattLease = Proxy.newProxyInstance(BleGattLease::class.java.classLoader,
        arrayOf(BleGattLease::class.java)) { _, _, _ -> error("Adapter must not perform GATT I/O") } as BleGattLease
    private inner class Fixture {
        val owner = Any()
        var now = 1000L
        val lease = lease()
        val query = BlePacketAuthorityQuery(lease, 3, 0x01020304u, "fixture-endpoint")
        var onClock: (() -> Unit)? = null
        val adapter = BlePacketAuthorityAdapter(owner) { onClock?.invoke(); now }
        fun start(pin: ByteArray = local, revision: ULong = 4uL): ByteArray? = adapter.begin(owner,
            query, pin, remote, 1uL, 2u, revision, 0x05060708u)
        fun complete(bytes: ByteArray = success) {
            now = 1001
            val decoded = CompanionAuthorityCodec.decodeResponse(bytes)
            val encoded = if (decoded == null) bytes else CompanionAuthorityCodec.encodeResponse(decoded)
            adapter.complete(owner, lease, 3, encoded)
        }
        fun ready() { assertContentEquals(request, start()); complete(); assertNotNull(adapter.current(query)) }
    }
    @Test fun exactProducerWireCompletesOnceWithFixedDeadline() {
        val f = Fixture(); f.ready()
        val proof = assertNotNull(f.adapter.current(f.query))
        assertSame(f.lease, proof.lease)
        assertEquals(3uL, proof.deviceGeneration)
        assertEquals(3uL, proof.admittedSession)
        assertEquals(1uL, proof.group)
        assertEquals(31000L, proof.expiresAtMillis)
        f.now = 30999; assertNotNull(f.adapter.current(f.query))
        f.now = 31000; assertNull(f.adapter.current(f.query))
    }
    @Test fun requestOrReceivedMetadataCannotBootstrapAuthority() {
        val f = Fixture(); assertNull(f.adapter.current(f.query)); f.start()
        assertNull(f.adapter.current(f.query))
        val other = Fixture(); other.complete(); assertNull(other.adapter.current(other.query))
    }
    @Test fun state2ProducerRefusalHasNoAuthorityOrFallback() {
        val f = Fixture(); f.start(); f.complete(refusal)
        assertNull(f.adapter.current(f.query)); assertNull(f.start())
    }
    @Test fun state3OldLeaseAndCallbackRefuseClosed() {
        for (changeLease in listOf(false, true)) {
            val f = Fixture(); f.start()
            f.adapter.complete(f.owner, if (changeLease) lease() else f.lease, if (changeLease) 3 else 2, success)
            assertNull(f.adapter.current(f.query))
        }
    }
    @Test fun state3DuplicateNeverRenewsOrRetainsProof() {
        val f = Fixture(); f.ready(); f.complete(); assertNull(f.adapter.current(f.query))
        assertNull(f.start())
    }
    @Test fun allSevenLifecycleInvalidationsRetirePendingAndCompletedExchange() {
        val reasons = listOf(BlePacketAuthorityAdapter.Invalidation.DISCONNECT,
            BlePacketAuthorityAdapter.Invalidation.LEASE_CHANGE, BlePacketAuthorityAdapter.Invalidation.OWNER_REPLACEMENT,
            BlePacketAuthorityAdapter.Invalidation.RESTART_AMBIGUITY, BlePacketAuthorityAdapter.Invalidation.REKEY,
            BlePacketAuthorityAdapter.Invalidation.MEMBERSHIP_CHANGE, BlePacketAuthorityAdapter.Invalidation.REVOCATION)
        for (reason in reasons) for (completed in listOf(false, true)) {
            val f = Fixture(); f.start(); if (completed) f.complete()
            f.adapter.invalidate(reason); f.complete()
            assertNull(f.adapter.current(f.query)); assertEquals(reason, f.adapter.lastInvalidation)
            assertNull(f.start())
        }
    }
    @Test fun state4WrongOwnerOrMembershipRevisionCannotComplete() {
        val ownerMismatch = Fixture(); ownerMismatch.start()
        ownerMismatch.adapter.complete(Any(), ownerMismatch.lease, 3, success)
        assertNull(ownerMismatch.adapter.current(ownerMismatch.query))
        val member = Fixture(); member.start(revision = 5uL); member.complete()
        assertNull(member.adapter.current(member.query))
    }
    @Test fun state5LostInvalidationStillExpiresLocally() {
        val f = Fixture(); f.ready(); f.now = 31001
        assertNull(f.adapter.current(f.query))
        f.now = 1001; assertNull(f.adapter.current(f.query))
    }
    @Test fun state6RestartCannotResumeProofFromRetainedMetadata() {
        val f = Fixture(); f.ready()
        val restarted = BlePacketAuthorityAdapter(f.owner) { f.now }
        assertNull(restarted.current(f.query)); restarted.complete(f.owner, f.lease, 3, success)
        assertNull(restarted.current(f.query))
    }
    @Test fun leaseCallbackNonceAndPeerMismatchRetiresCurrentProof() {
        for (kind in 0..3) {
            val f = Fixture(); f.ready()
            val wrong = when (kind) {
                0 -> f.query.copy(lease = lease())
                1 -> f.query.copy(callbackGeneration = 4)
                2 -> f.query.copy(nonce = 1u)
                else -> f.query.copy(peer = "different-fixture")
            }
            assertNull(f.adapter.current(wrong)); assertNull(f.adapter.current(f.query))
        }
    }
    @Test fun fullIdentityPinMustMatch() {
        val f = Fixture(); f.start(local.copyOf().also { it[31] = 0 }); f.complete()
        assertNull(f.adapter.current(f.query))
    }
    @Test fun nonceExchangeAndMalformedRecordsRefuse() {
        for (offset in listOf(8, 12, 24, 25, 26, 27, 128)) {
            val f = Fixture(); f.start(); f.complete(success.copyOf().also { it[offset] = if (offset == 27) 1 else 0 })
            assertNull(f.adapter.current(f.query), "offset $offset")
        }
        for (b in listOf(success.copyOf(135), success + 0.toByte())) {
            val f = Fixture(); f.start(); f.complete(b); assertNull(f.adapter.current(f.query))
        }
    }
    @Test fun lateReplyRollbackAndOverflowRefuse() {
        for (clock in listOf(999L, 31000L, 31001L)) {
            val f = Fixture(); f.start(); f.now = clock
            f.adapter.complete(f.owner, f.lease, 3, success); assertNull(f.adapter.current(f.query))
        }
        val f = Fixture(); f.now = Long.MAX_VALUE - 10; f.start()
        f.adapter.complete(f.owner, f.lease, 3, success); assertNull(f.adapter.current(f.query))
        val g = Fixture(); g.ready(); g.now = 999; assertNull(g.adapter.current(g.query))
    }
    @Test fun invalidBeginAndClockExceptionCannotCreateProof() {
        val f = Fixture(); assertNull(f.start(ByteArray(32))); f.complete(); assertNull(f.adapter.current(f.query))
        val broken = BlePacketAuthorityAdapter(f.owner) { error("fixture-clock-failure") }
        assertNull(broken.begin(f.owner, f.query, local, remote, 1uL, 2u, 4uL, 1u))
        assertNull(broken.current(f.query))
    }
    @Test fun clockCallbackRetirementCannotReturnPriorProof() {
        val f = Fixture(); f.ready()
        f.onClock = { f.adapter.invalidate(BlePacketAuthorityAdapter.Invalidation.REVOCATION) }
        assertNull(f.adapter.current(f.query))
        assertEquals(BlePacketAuthorityAdapter.Invalidation.REVOCATION, f.adapter.lastInvalidation)
    }
    private fun hex(v: String) = v.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
