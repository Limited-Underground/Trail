package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class CompanionDirectConfigurationTest {
    private data class Row(val name: String, val type: String, val accepted: Boolean, val bytes: ByteArray)

    private val rows: List<Row> by lazy {
        val stream = assertNotNull(javaClass.getResourceAsStream("/companion_direct_configuration_v1.tsv"))
        stream.bufferedReader().readLines().filter { it.isNotBlank() && !it.startsWith("#") }.map { line ->
            val f = line.split('\t')
            Row(f[0], f[1], f[2] == "1", f[3].chunked(2).map { it.toInt(16).toByte() }.toByteArray())
        }
    }

    private fun row(name: String) = rows.single { it.name == name }.bytes

    private val peer = ByteArray(32) { (it + 1).toByte() }
    private val scope = ByteArray(32) { (it + 0x41).toByte() }
    private val binding = CompanionDirectBinding(peer, scope)

    @Test
    fun sharedVectorsMatchTheBoardCodec() {
        assertTrue(rows.size >= 50)
        for (r in rows) {
            when (r.type) {
                "command" -> {
                    val decoded = CompanionDirectConfigurationCodec.decodeCommand(r.bytes)
                    assertEquals(r.accepted, decoded != null, r.name)
                    if (decoded != null) {
                        assertContentEquals(r.bytes, CompanionDirectConfigurationCodec.encode(decoded), r.name)
                    }
                }
                "result" -> assertEquals(r.accepted, CompanionDirectConfigurationCodec.decodeResult(r.bytes) != null, r.name)
                else -> error("unknown row type ${r.type}")
            }
        }
    }

    @Test
    fun commandsEncodeExactly() {
        val codec = CompanionDirectConfigurationCodec
        assertContentEquals(row("begin-start"), codec.encode(CompanionDirectCommand.Begin(CompanionDirectRole.START, 123456)))
        assertContentEquals(row("accept"), codec.encode(CompanionDirectCommand.Accept(binding)))
        assertContentEquals(row("finish"), codec.encode(CompanionDirectCommand.Finish(binding)))
        assertContentEquals(row("block"), codec.encode(CompanionDirectCommand.Block(binding)))
        assertContentEquals(row("unblock"), codec.encode(CompanionDirectCommand.Unblock(peer)))
        assertContentEquals(
            row("send"),
            codec.encode(CompanionDirectCommand.Send(CompanionDirectText(3, 0x0807060504030201uL, 0x0a0b0c0du, "Meet at the ridge"))),
        )
        assertNull(codec.encode(CompanionDirectCommand.Begin(CompanionDirectRole.JOIN, 0)))
        assertNull(codec.encode(CompanionDirectCommand.Read(8)))
        assertNull(codec.encode(CompanionDirectCommand.Accept(CompanionDirectBinding(peer, ByteArray(32)))))
        assertNull(codec.encode(CompanionDirectCommand.Send(CompanionDirectText(0, 1uL, 1u, ""))))
        assertNull(codec.encode(CompanionDirectCommand.Send(CompanionDirectText(0, 1uL, 1u, "x".repeat(91)))))
        assertNull(codec.encode(CompanionDirectCommand.Send(CompanionDirectText(0, 0uL, 1u, "hi"))))
        // 30 three-byte characters fit; 31 do not.
        assertNotNull(codec.encode(CompanionDirectCommand.Send(CompanionDirectText(0, 1uL, 1u, "€".repeat(30)))))
        assertNull(codec.encode(CompanionDirectCommand.Send(CompanionDirectText(0, 1uL, 1u, "€".repeat(31)))))
    }

    @Test
    fun resultsCarryTheBoardFields() {
        val codec = CompanionDirectConfigurationCodec
        assertEquals(binding, assertIs<CompanionDirectResult.Reviewed>(codec.decodeResult(row("review-ok"))).binding)
        val ready = assertIs<CompanionDirectResult.Conversation>(codec.decodeResult(row("read-ready")))
        assertEquals(2, ready.slot)
        assertEquals(CompanionDirectState.READY, ready.state)
        assertEquals(0x0807060504030201uL, ready.generation)
        assertEquals(binding, ready.binding)
        val empty = assertIs<CompanionDirectResult.Conversation>(codec.decodeResult(row("read-empty")))
        assertNull(empty.binding)
        val busy = assertIs<CompanionDirectResult.Plain>(codec.decodeResult(row("finish-busy")))
        assertEquals(CompanionDirectOperation.FINISH, busy.operation)
        assertEquals(CompanionDirectStatus.BUSY, busy.status)
        val received = assertIs<CompanionDirectResult.Received>(codec.decodeResult(row("received")))
        assertEquals("Meet at the ridge", received.text.text)
        assertEquals(77u, received.text.messageId)
        val delivered = assertIs<CompanionDirectResult.Delivery>(codec.decodeResult(row("delivery-delivered")))
        assertEquals(CompanionDirectDelivery.DELIVERED, delivered.phase)
        assertEquals(5, delivered.slot)
    }

    @Test
    fun receivedTextMustBeUtf8() {
        val bytes = row("received").copyOf()
        bytes[bytes.size - 1] = 0xff.toByte()
        assertNull(CompanionDirectConfigurationCodec.decodeResult(bytes))
    }

    @Test
    fun checkCodeAndLogsStayRedacted() {
        assertEquals("0102 0304 0506", binding.peerCheckCode())
        assertEquals("CompanionDirectBinding(redacted)", binding.toString())
        assertTrue("Meet" !in CompanionDirectText(0, 1uL, 1u, "Meet").toString())
    }
}
