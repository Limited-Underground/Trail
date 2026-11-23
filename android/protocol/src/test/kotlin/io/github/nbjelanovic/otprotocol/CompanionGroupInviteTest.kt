package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * F8 Invite, Read Invite and Join. The hex vectors were produced by the firmware codec
 * (companion_configuration_codec.cpp and group_qr_invite.cpp) so both sides stay byte-identical.
 */
class CompanionGroupInviteTest {
    private val codec = CompanionGroupConfigurationCodec
    private val scope = CompanionGroupScope(ByteArray(32) { 0x11 }, 0x0807060504030201uL, ByteArray(32) { 0x22 })
    private val inviteId = ByteArray(16) { (0xa0 + it).toByte() }
    private val wireScope = "11".repeat(32) + "0102030405060708" + "22".repeat(32)
    private val descriptor = hex(
        "4f54475101010200" + "11".repeat(32) + "0807060504030201" + "00000003" + "22".repeat(32) +
            "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf" + "01010a" + "547261696c2063726577" + "5c".repeat(64),
    )

    @Test
    fun commandsMatchFirmwareBytes() {
        assertEquals(177, descriptor.size)
        assertContentEquals(
            hex("4f54474301050000" + wireScope + "0a547261696c2063726577"),
            assertNotNull(codec.invite(scope, "Trail crew")),
        )
        assertContentEquals(
            hex("4f54474301060000a0a1a2a3a4a5a6a7a8a9aaabacadaeaf6000"),
            assertNotNull(codec.readInvite(inviteId, 96)),
        )
        val last = assertNotNull(codec.joinPage(descriptor, 96))
        assertContentEquals(hex("4f544743010700006000b100") + descriptor.copyOfRange(96, 177), last)
        assertEquals(96 + 12, assertNotNull(codec.joinPage(descriptor, 0)).size)
        listOf(codec.invite(scope, "Trail crew"), codec.readInvite(inviteId, 96), last).forEach {
            assertTrue(codec.validCommand(assertNotNull(it)))
        }
    }

    @Test
    fun commandsRefuseBadInput() {
        assertNull(codec.invite(scope, ""))
        assertNull(codec.invite(scope, " padded"))
        assertNull(codec.invite(scope, "a".repeat(48)))
        assertNull(codec.invite(CompanionGroupScope(ByteArray(32), 1uL, ByteArray(32) { 1 }), "Crew"))
        assertNull(codec.readInvite(ByteArray(16), 0))
        assertNull(codec.readInvite(inviteId, 42))
        assertNull(codec.readInvite(inviteId, 288))
        assertNull(codec.joinPage(descriptor, 42))
        assertNull(codec.joinPage(descriptor, 192))
        assertNull(codec.joinPage(descriptor.copyOf(166), 0))
        assertNull(codec.joinPage(ByteArray(288), 0))
        val page = assertNotNull(codec.joinPage(descriptor, 96))
        assertTrue(!codec.validCommand(page + 0))
        assertTrue(!codec.validCommand(page.copyOf(page.size - 1)))
    }

    @Test
    fun resultsMatchFirmwareBytes() {
        val issued = assertNotNull(codec.decodeResult(hex("4f54474301050000" + wireScope + "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb100")))
        assertEquals(CompanionGroupOperation.INVITE, issued.operation)
        assertEquals(scope, issued.scope)
        assertContentEquals(inviteId, assertNotNull(issued.invite).inviteId)
        assertEquals(177, issued.invite?.totalBytes)

        val page = assertNotNull(codec.decodeResult(
            hex("4f54474301060000a0a1a2a3a4a5a6a7a8a9aaabacadaeaf6000b100") + descriptor.copyOfRange(96, 177)))
        assertEquals(96, page.invite?.offset)
        assertContentEquals(descriptor.copyOfRange(96, 177), assertNotNull(page.invite).bytes)

        val middle = assertNotNull(codec.decodeResult(hex("4f544743010700000000b100")))
        assertNull(middle.scope)
        assertNull(middle.invite?.inviteId)
        val final = assertNotNull(codec.decodeResult(hex("4f544743010700006000b100" + wireScope + "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")))
        assertEquals(scope, final.scope)
        assertContentEquals(inviteId, final.invite?.inviteId)
        assertEquals(CompanionGroupStatus.STALE, codec.decodeResult(hex("4f54474301070300"))?.status)

        // A last-page result without its scope, a middle page with one, a short read page,
        // a failure with a body and MISSING all fail.
        assertNull(codec.decodeResult(hex("4f544743010700006000b100")))
        assertNull(codec.decodeResult(hex("4f544743010700000000b100" + wireScope + "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")))
        assertNull(codec.decodeResult(hex("4f54474301060000a0a1a2a3a4a5a6a7a8a9aaabacadaeaf6000b100") + descriptor.copyOfRange(96, 176)))
        assertNull(codec.decodeResult(hex("4f5447430105020000")))
        assertNull(codec.decodeResult(hex("4f54474301050500")))
    }

    @Test
    fun resultFramesPassConfigurationFrameValidation() {
        val frame = CompanionConfigurationFrame(codec.RESPONSE_KIND, 9u, 3u, hex("4f544743010700000000b100"), 5)
        val bytes = assertNotNull(CompanionConfigurationCodec.encodeFrame(frame))
        assertNotNull(CompanionConfigurationCodec.decodeFrame(bytes, expectedMinorVersion = 5))
    }

    @Test
    fun decodesInviteForReview() {
        val invite = assertNotNull(codec.decodeInvite(descriptor))
        assertContentEquals(ByteArray(32) { 0x11 }, invite.adminKey)
        assertEquals(0x0807060504030201uL, invite.groupId)
        assertEquals(3u, invite.rosterVersion)
        assertEquals(1, invite.locationRule)
        assertEquals("Trail crew", invite.label)
        assertContentEquals(inviteId, invite.inviteId)
        assertNull(codec.decodeInvite(descriptor.copyOf(176)))
        assertNull(codec.decodeInvite(descriptor.copyOf().also { it[3] = 0x52 }))
        assertNull(codec.decodeInvite(descriptor.copyOf().also { it[100] = 3 }))
        assertNull(codec.decodeInvite(descriptor.copyOf().also { it[101] = 0 }))
        assertNull(codec.decodeInvite(descriptor.copyOf().also { it[102] = 11 }))
        assertNull(codec.decodeInvite(descriptor.copyOf().also { it.fill(0, 84, 100) }))
    }

    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
