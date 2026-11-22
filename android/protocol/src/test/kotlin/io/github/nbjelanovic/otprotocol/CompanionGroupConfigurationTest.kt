package io.github.nbjelanovic.otprotocol

import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class CompanionGroupConfigurationTest {
    private val codec = CompanionGroupConfigurationCodec
    private val admin = ByteArray(32) { 0x23 }
    private val group = 0xf102030405060708uL
    private val digest = hex("86c78b57b4e887d5a5358af1e7e8bd778b5540b8bd37f9f67fc3db0d9e8ff105")
    private val scope = CompanionGroupScope(admin, group, digest)
    private val wireScope = admin + hex("08070605040302f1") + digest
    // This fixture exercises complete signed bytes, but does not supply a valid Ed25519 signature.
    private val signedRoster = hex(
        "4f54475201f10203040506070800000001" + "00".repeat(32) + "01010100000001" +
            "23".repeat(32) + "0541646d696e" + "5a".repeat(64),
    )

    @Test
    fun createsCanonicalCommandBodies() {
        assertContentEquals(hex("4f54474301010000"), codec.discover())
        assertContentEquals(hex("4f5447430102000002"), assertNotNull(codec.create(2)))
        assertContentEquals(hex("4f54474301030000") + wireScope, assertNotNull(codec.stopTrust(scope)))
        assertContentEquals(hex("4f5447430104000003000000"), assertNotNull(codec.readRoster(3)))
        assertContentEquals(
            hex("4f544743010400000102a401") + wireScope,
            assertNotNull(codec.readRoster(1, 420, scope, CompanionGroupRosterState.DISTRUSTED)),
        )
        assertNull(codec.create(3))
        assertNull(codec.readRoster(4))
        assertNull(codec.readRoster(0, 1, scope, CompanionGroupRosterState.ACTIVE))
        assertNull(codec.readRoster(0, 42, scope))
        assertNull(codec.readRoster(0, 0, scope, CompanionGroupRosterState.ACTIVE))
        assertNull(codec.readRoster(0, 462, scope, CompanionGroupRosterState.ACTIVE))
        assertNull(codec.stopTrust(CompanionGroupScope(admin, 0uL, digest)))
    }

    @Test
    fun framesOnlyAdmitCanonicalGroupPayloadsOnMinorFive() {
        val command = CompanionConfigurationFrame(codec.REQUEST_KIND, 87u, 23u, codec.discover(), 5)
        val bytes = assertNotNull(CompanionConfigurationCodec.encodeFrame(command))
        val decoded = assertNotNull(CompanionConfigurationCodec.decodeFrame(bytes, expectedMinorVersion = 5))
        assertEquals(87u, decoded.sessionNonce)
        assertEquals(23u, decoded.exchangeId)
        assertContentEquals(codec.discover(), decoded.payload)
        for (minor in listOf(2, 3, 6, 127)) {
            assertNull(
                CompanionConfigurationCodec.encodeFrame(
                    CompanionConfigurationFrame(codec.REQUEST_KIND, 87u, 23u, codec.discover(), minor),
                    allowProfile06 = true,
                ),
            )
        }
        val malformedHeader = bytes.copyOf().also { it[27] = 1 }
        assertNull(CompanionConfigurationCodec.decodeFrame(malformedHeader, expectedMinorVersion = 5))
        val reply = CompanionConfigurationFrame(
            codec.RESPONSE_KIND, 87u, 23u, hex("4f54474301020000") + wireScope, 5,
        )
        assertNotNull(CompanionConfigurationCodec.encodeFrame(reply))
        assertNull(
            CompanionConfigurationCodec.encodeFrame(
                CompanionConfigurationFrame(codec.RESPONSE_KIND, 87u, 23u, reply.payload, 6),
                allowProfile06 = true,
            ),
        )
    }

    @Test
    fun resultKeepsFullScopeAndRejectsIllegalStatusAndSize() {
        val create = hex("4f54474301020000") + wireScope
        val created = assertNotNull(codec.decodeResult(create))
        assertEquals(CompanionGroupOperation.CREATE, created.operation)
        assertEquals(CompanionGroupStatus.OK, created.status)
        assertEquals(scope, created.scope)
        assertNull(created.chunk)
        assertNull(codec.decodeResult(create.copyOf(create.size - 1)))
        assertNull(codec.decodeResult(create + byteArrayOf(0)))
        assertNull(codec.decodeResult(create.copyOf().also { it.fill(0, 8, 40) }))
        assertNull(codec.decodeResult(hex("4f54474301010400")))
        assertNull(codec.decodeResult(hex("4f54474301030500")))
        assertNull(codec.decodeResult(hex("4f54474301040600")))
        assertNull(codec.decodeResult(hex("4f54474301030400") + wireScope))
        assertEquals(
            CompanionGroupResult(CompanionGroupOperation.STOP_TRUST, CompanionGroupStatus.UNCERTAIN),
            codec.decodeResult(hex("4f54474301030400")),
        )
        assertEquals(
            CompanionGroupResult(CompanionGroupOperation.READ_ROSTER, CompanionGroupStatus.MISSING),
            codec.decodeResult(hex("4f54474301040500")),
        )
    }

    @Test
    fun readFragmentsRequireCanonicalOffsetsStateAndExactRemainingLength() {
        val payload = hex("4f54474301040000030100009e00") + wireScope + signedRoster.copyOfRange(0, 42)
        val result = assertNotNull(codec.decodeResult(payload))
        assertEquals(scope, result.scope)
        val chunk = assertNotNull(result.chunk)
        assertEquals(3, chunk.slot)
        assertEquals(0, chunk.offset)
        assertEquals(158, chunk.totalBytes)
        assertEquals(CompanionGroupRosterState.ACTIVE, chunk.state)
        assertContentEquals(signedRoster.copyOfRange(0, 42), chunk.bytes)
        for ((index, wrong) in listOf(8 to 4, 9 to 0, 10 to 1, 12 to 0, 13 to 2)) {
            assertNull(codec.decodeResult(payload.copyOf().also { it[index] = wrong.toByte() }))
        }
        assertNull(codec.decodeResult(payload.copyOf(payload.size - 1)))
        assertNull(codec.decodeResult(payload + byteArrayOf(0)))

        val last = hex("4f5447430104000003027e009e00") + wireScope + signedRoster.copyOfRange(126, 158)
        val lastChunk = assertNotNull(assertNotNull(codec.decodeResult(last)).chunk)
        assertEquals(CompanionGroupRosterState.DISTRUSTED, lastChunk.state)
        assertEquals(126, lastChunk.offset)
        assertEquals(32, lastChunk.bytes.size)
        val tooLarge = last.copyOf().also { it[12] = 0xb8.toByte(); it[13] = 1 }
        assertNull(codec.decodeResult(tooLarge))
    }

    @Test
    fun decodedRosterUsesEveryByteOfTheProtectedScope() {
        val decoded = assertNotNull(codec.decodeRoster(signedRoster, scope))
        assertEquals(1u, decoded.version)
        assertEquals(1, decoded.locationRule)
        assertEquals("Admin", decoded.members.single().alias)
        assertEquals(true, decoded.members.single().administrator)
        assertContentEquals(admin, decoded.members.single().fingerprint)

        assertNull(codec.decodeRoster(signedRoster, CompanionGroupScope(admin, group - 1uL, digest)))
        assertNull(codec.decodeRoster(signedRoster, CompanionGroupScope(ByteArray(32) { 1 }, group, digest)))
        val altered = signedRoster.copyOf().also { it[it.lastIndex] = 1 }
        assertNull(codec.decodeRoster(altered, scope))
    }

    @Test
    fun matchingDigestDoesNotAcceptMalformedSignedRosterFormat() {
        for (bad in listOf(
            signedRoster.copyOf(signedRoster.size - 1),
            signedRoster + byteArrayOf(0),
            signedRoster.copyOf().also { it[51] = 2 },
            signedRoster.copyOf().also { it[89] = 0 },
            signedRoster.copyOf().also { it[17] = 1 },
        )) {
            val sha = MessageDigest.getInstance("SHA-256").apply {
                update("OpenTrail group roster digest v1".encodeToByteArray())
            }.digest(bad)
            assertNull(codec.decodeRoster(bad, CompanionGroupScope(admin, group, sha)))
        }
    }

    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
