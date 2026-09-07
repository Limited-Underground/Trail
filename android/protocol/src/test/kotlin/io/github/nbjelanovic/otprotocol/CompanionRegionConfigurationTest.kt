package io.github.nbjelanovic.otprotocol

import kotlin.test.*

class CompanionRegionConfigurationTest {
    private val codec=CompanionConfigurationCodec
    private fun hex(text: String): ByteArray {
        assertTrue(text.length%2==0 && text.all { it in "0123456789abcdefABCDEF" })
        return text.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }

    @Test fun sharedCorpusChecksSemanticFieldsAndExactReencoding() {
        val stream=assertNotNull(javaClass.getResourceAsStream("/companion_configuration_v03.tsv"))
        val rows=stream.bufferedReader(Charsets.UTF_8).use { it.readLines() }.filter { it.isNotBlank() && !it.startsWith('#') }
        assertTrue(rows.isNotEmpty());val ids=mutableSetOf<String>()
        for(row in rows) {
            val cells=row.split('\t');assertEquals(5,cells.size,row);assertTrue(ids.add(cells[0]),row)
            val bytes=hex(cells[3]);val accepted=cells[2]=="1"
            assertTrue(cells[2] in listOf("0","1"),row)
            val fields=cells[4].split('|')
            when(cells[1]) {
                "info3" -> {
                    val value=codec.decodeInfo(bytes,3)
                    if(accepted) {
                        assertEquals(cells[4].toInt(),assertNotNull(value,cells[0]).capabilities,cells[0])
                        assertContentEquals(bytes,codec.encodeInfo(value),cells[0])
                        assertFalse(CompanionProtocolCodec.decodeProtocolInfo(bytes).isSuccess,cells[0])
                    } else assertNull(value,cells[0])
                }
                "region" -> {
                    val value=codec.decodeRegion(bytes)
                    if(accepted) {
                        assertNotNull(value,cells[0]);assertEquals(4,fields.size)
                        assertEquals(fields[0].toInt(),value.kind,cells[0]);assertEquals(fields[1].toInt(),value.status,cells[0])
                        assertEquals(fields[2].toULong(),value.revision,cells[0]);assertEquals(fields[3].toInt(),value.selectionId,cells[0])
                        assertContentEquals(bytes,codec.encodeRegion(value),cells[0])
                    } else assertNull(value,cells[0])
                }
                "frame3" -> {
                    val value=codec.decodeFrame(bytes,3)
                    if(accepted) {
                        assertNotNull(value,cells[0]);assertEquals(4,fields.size)
                        assertEquals(fields[0].toInt(),value.kind,cells[0]);assertEquals(fields[1].toUInt(),value.sessionNonce,cells[0])
                        assertEquals(fields[2].toUInt(),value.exchangeId,cells[0])
                        assertContentEquals(if(fields[3]=="-") byteArrayOf() else hex(fields[3]),value.payload,cells[0])
                        assertContentEquals(bytes,codec.encodeFrame(value),cells[0])
                        assertFalse(CompanionProtocolCodec.decodeFragment(bytes).isSuccess,cells[0])
                    } else assertNull(value,cells[0])
                }
                else -> fail("Unknown fixture type: ${cells[1]}")
            }
        }
    }

    @Test fun explicitVersionAndCapabilitiesRemainStrict() {
        val info=CompanionConfigurationInfo(0xff,minorVersion=3)
        val wire=assertNotNull(codec.encodeInfo(info))
        assertNull(codec.decodeInfo(wire));assertEquals(info,codec.decodeInfo(wire,3))
        assertNull(codec.encodeInfo(info.copy(capabilities=0xef)))
        assertNull(codec.encodeInfo(info.copy(minorVersion=2)))
        assertTrue(codec.transportCompatible(info,0x10,151,148,148,true))
        assertFalse(codec.transportCompatible(CompanionConfigurationInfo(0xef),0x10,151,148,148,true))
        assertFalse(codec.transportCompatible(info,0x10,150,148,148,true))
        assertFalse(codec.transportCompatible(info,0x10,151,147,148,true))
    }
    @Test fun allCatalogSelectionsAndUint64RevisionRoundTripWithOwnedFrames() {
        assertEquals(12,RegionSelectionCatalog.entries.size)
        for(selection in RegionSelectionCatalog.entries) for(revision in listOf(0uL,1uL,ULong.MAX_VALUE)) {
            val value=CompanionRegionPayload(2,revision=revision,selectionId=selection.id)
            val bytes=assertNotNull(codec.encodeRegion(value))
            assertEquals(value,codec.decodeRegion(bytes))
            assertEquals(24,bytes.size)
            val frame=CompanionConfigurationFrame(6,UInt.MAX_VALUE,UInt.MAX_VALUE,bytes,3)
            bytes.fill(0)
            val encoded=assertNotNull(codec.encodeFrame(frame))
            assertNull(codec.decodeFrame(encoded));assertEquals(value,codec.decodeRegion(assertNotNull(codec.decodeFrame(encoded,3)).payload))
            assertNull(codec.encodeFrame(CompanionConfigurationFrame(6,1u,1u,frame.payload)))
            assertNull(codec.encodeFrame(CompanionConfigurationFrame(0x88,1u,1u,frame.payload,3)))
        }
        assertNotNull(codec.encodeRegion(CompanionRegionPayload(2,selectionId=65535)))
        assertNull(RegionSelectionCatalog.find(65535)) // Decode and supported selection admission are distinct.
    }
    @Test fun regionFieldCoherenceAndReservedBytesReject() {
        val valid=listOf(CompanionRegionPayload(1),CompanionRegionPayload(0x81),
            CompanionRegionPayload(0x81,revision=1u,selectionId=1),CompanionRegionPayload(0x82,revision=ULong.MAX_VALUE,selectionId=12),
            CompanionRegionPayload(0x84,status=5))+(1..4).map { CompanionRegionPayload(0x83,status=it) }
        for(value in valid) {
            val wire=assertNotNull(codec.encodeRegion(value));assertEquals(value,codec.decodeRegion(wire))
            for(index in listOf(4,7,18,19,20,21,22,23)) {
                val bad=wire.copyOf();bad[index]=2;assertNull(codec.decodeRegion(bad))
            }
            assertNull(codec.decodeRegion(wire.copyOf(23)));assertNull(codec.decodeRegion(wire.copyOf(25)))
        }
        for(value in listOf(CompanionRegionPayload(0),CompanionRegionPayload(1,revision=1u),CompanionRegionPayload(2),
            CompanionRegionPayload(0x81,revision=1u),CompanionRegionPayload(0x81,selectionId=1),
            CompanionRegionPayload(0x82,selectionId=1),CompanionRegionPayload(0x83),CompanionRegionPayload(0x84,status=4),
            CompanionRegionPayload(2,selectionId=-1),CompanionRegionPayload(2,selectionId=65536))) assertNull(codec.encodeRegion(value))
    }
}
