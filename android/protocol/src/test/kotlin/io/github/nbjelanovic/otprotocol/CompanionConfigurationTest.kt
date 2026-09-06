package io.github.nbjelanovic.otprotocol

import kotlin.test.*

class CompanionConfigurationTest {
    private val codec = CompanionConfigurationCodec
    private fun hex(text: String): ByteArray {
        assertTrue(text.length%2==0 && text.all { it in "0123456789abcdefABCDEF" })
        return text.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }

    @Test fun sharedCorpusChecksSemanticFieldsAndExactReencoding() {
        val stream=assertNotNull(javaClass.getResourceAsStream("/companion_configuration_v02.tsv"))
        val rows=stream.bufferedReader(Charsets.UTF_8).use { it.readLines() }.filter { it.isNotBlank() && !it.startsWith('#') }
        assertTrue(rows.isNotEmpty());val ids=mutableSetOf<String>()
        for(row in rows) {
            val cells=row.split('\t');assertEquals(5,cells.size,row);assertTrue(ids.add(cells[0]),row)
            val bytes=hex(cells[3]);val accepted=cells[2]=="1"
            assertTrue(cells[2] in listOf("0","1"),row)
            val fields=cells[4].split('|')
            when(cells[1]) {
                "info" -> {
                    val value=codec.decodeInfo(bytes)
                    if(accepted) {
                        assertEquals(cells[4].toInt(),assertNotNull(value,cells[0]).capabilities,cells[0])
                        assertContentEquals(bytes,codec.encodeInfo(value),cells[0])
                        assertFalse(CompanionProtocolCodec.decodeProtocolInfo(bytes).isSuccess,cells[0])
                    } else assertNull(value,cells[0])
                }
                "time" -> {
                    val value=codec.decodeTime(bytes)
                    if(accepted) {
                        assertNotNull(value,cells[0]);assertEquals(5,fields.size)
                        assertEquals(fields[0].toInt(),value.kind,cells[0]);assertEquals(fields[1].toInt(),value.code,cells[0])
                        assertEquals(fields[2].toInt(),value.format,cells[0]);assertEquals(fields[3].toULong(),value.challenge,cells[0])
                        assertEquals(fields[4].toUInt(),value.localSecond,cells[0]);assertContentEquals(bytes,codec.encodeTime(value),cells[0])
                    } else assertNull(value,cells[0])
                }
                "frame" -> {
                    val value=codec.decodeFrame(bytes)
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

    @Test fun compatibilityIsStrictCapacityAndFeatureCheckOnly() {
        val both=CompanionConfigurationInfo(0xef)
        fun compatible(info: CompanionConfigurationInfo=both,feature: Int=0x40,mtu: Int=151,
                       send: Int=148,receive: Int=148,indications: Boolean=true) =
            codec.transportCompatible(info,feature,mtu,send,receive,indications)
        assertTrue(compatible());assertTrue(compatible(feature=0x80,mtu=65535))
        for(mtu in listOf(-1,0,150,65536,Int.MAX_VALUE)) assertFalse(compatible(mtu=mtu))
        for(size in listOf(-1,0,147)) { assertFalse(compatible(send=size));assertFalse(compatible(receive=size)) }
        assertFalse(compatible(indications=false))
        for(feature in listOf(-1,0,1,0xc0,0x100)) assertFalse(compatible(feature=feature))
        assertFalse(compatible(info=CompanionConfigurationInfo(0x40),feature=0x80))
        assertFalse(compatible(info=CompanionConfigurationInfo(0x80),feature=0x40))
        for(info in listOf(both.copy(role=2),both.copy(capabilities=0xff),both.copy(capabilities=0x2f),
            both.copy(capabilities=-1),both.copy(capabilities=0x140),both.copy(maxPayloadBytes=127),
            both.copy(minimumAttMtu=150),both.copy(fragmentCount=2),both.copy(controllerCount=0))) {
            assertFalse(compatible(info=info));assertNull(codec.encodeInfo(info))
        }
    }

    @Test fun frameOwnsInputDecodeAndReturnedPayloadBytes() {
        val source=byteArrayOf(1,2,3);val frame=CompanionConfigurationFrame(1,1u,2u,source)
        source.fill(0);assertContentEquals(byteArrayOf(1,2,3),frame.payload)
        val exported=frame.payload;exported.fill(0);assertContentEquals(byteArrayOf(1,2,3),frame.payload)
        val wire=assertNotNull(codec.encodeFrame(frame));val decoded=assertNotNull(codec.decodeFrame(wire))
        wire.fill(0);assertContentEquals(byteArrayOf(1,2,3),decoded.payload)
        decoded.payload.fill(0);assertContentEquals(byteArrayOf(1,2,3),decoded.payload)
    }

    @Test fun requestResultDirectionsCannotBeSwapped() {
        val nameRead=assertNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.READ)))
        val nameApplied=assertNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.APPLIED,1u,"A")))
        val timeRequest=assertNotNull(codec.encodeTime(CompanionTimePayload(1)))
        val timeChallenge=assertNotNull(codec.encodeTime(CompanionTimePayload(2,challenge=1u)))
        for((kind,payload) in listOf(4 to nameRead,0x86 to nameApplied,5 to timeRequest,0x87 to timeChallenge)) {
            assertNotNull(codec.encodeFrame(CompanionConfigurationFrame(kind,1u,1u,payload)))
        }
        for((kind,payload) in listOf(4 to nameApplied,0x86 to nameRead,5 to timeChallenge,0x87 to timeRequest)) {
            assertNull(codec.encodeFrame(CompanionConfigurationFrame(kind,1u,1u,payload)))
        }
        for(kind in listOf(3,0x84,0x85,0,6,255,-1,257)) assertNull(codec.encodeFrame(CompanionConfigurationFrame(kind,1u,1u,byteArrayOf())))
    }

    @Test fun frameBoundsAndIdentityAreExactAndBasePayloadRemainsOpaque() {
        for(kind in listOf(1,2,0x81,0x82,0x83)) {
            assertNotNull(codec.encodeFrame(CompanionConfigurationFrame(kind,UInt.MAX_VALUE,UInt.MAX_VALUE,ByteArray(128) { 0xff.toByte() })))
            assertNotNull(codec.encodeFrame(CompanionConfigurationFrame(kind,1u,1u,byteArrayOf())))
        }
        assertNull(codec.encodeFrame(CompanionConfigurationFrame(1,1u,1u,ByteArray(129))))
        assertNull(codec.encodeFrame(CompanionConfigurationFrame(1,0u,1u,byteArrayOf())))
        assertNull(codec.encodeFrame(CompanionConfigurationFrame(1,1u,0u,byteArrayOf())))
    }

    @Test fun timeSemanticBoundariesAndUnsignedChallengeAreExact() {
        for(format in listOf(1,2)) for(second in listOf(0u,86399u)) {
            val sample=CompanionTimePayload(3,format=format,challenge=ULong.MAX_VALUE,localSecond=second)
            assertEquals(sample,codec.decodeTime(assertNotNull(codec.encodeTime(sample))))
        }
        for(code in 1..8) for(id in listOf(0uL,ULong.MAX_VALUE)) {
            assertNotNull(codec.encodeTime(CompanionTimePayload(4,code=code,challenge=id)))
        }
        for(value in listOf(CompanionTimePayload(0),CompanionTimePayload(257),CompanionTimePayload(1,code=1),
            CompanionTimePayload(1,challenge=1u),CompanionTimePayload(2),CompanionTimePayload(2,challenge=1u,localSecond=1u),
            CompanionTimePayload(3,format=0,challenge=1u),CompanionTimePayload(3,format=3,challenge=1u),
            CompanionTimePayload(3,format=2,challenge=1u,localSecond=86400u),CompanionTimePayload(4),
            CompanionTimePayload(4,code=9),CompanionTimePayload(4,code=-1),CompanionTimePayload(4,code=1,format=1))) {
            assertNull(codec.encodeTime(value),value.toString())
        }
    }

    @Test fun allTruncationsAndTrailingBytesRejectWithoutThrowing() {
        val info=assertNotNull(codec.encodeInfo(CompanionConfigurationInfo(0xc0)))
        val time=assertNotNull(codec.encodeTime(CompanionTimePayload(1)))
        val frame=assertNotNull(codec.encodeFrame(CompanionConfigurationFrame(5,1u,2u,time)))
        for(size in info.indices) assertNull(codec.decodeInfo(info.copyOf(size)))
        for(size in time.indices) assertNull(codec.decodeTime(time.copyOf(size)))
        for(size in frame.indices) assertNull(codec.decodeFrame(frame.copyOf(size)))
        assertNull(codec.decodeInfo(info+0));assertNull(codec.decodeTime(time+0));assertNull(codec.decodeFrame(frame+0))
    }
}
