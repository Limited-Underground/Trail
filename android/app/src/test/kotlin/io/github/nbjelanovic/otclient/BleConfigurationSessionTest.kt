package io.github.nbjelanovic.otclient
import io.github.nbjelanovic.otprotocol.*
import kotlin.test.*

class BleConfigurationSessionTest {
    private class Fixture {
        val context=V1NameContext(1,2,3,4,5,6,7u,null,requireNotNull(V1SetupSessionToken.create(ByteArray(16){1})))
        var current=true;var next=11u;var writeResult=true
        val sent=mutableListOf<CompanionConfigurationFrame>()
        val session=BleConfigurationSession(context,{current},{next++},{bytes->
            sent+=checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes));writeResult
        },{ 86399u to 2 },{})
        fun name(kind:CompanionNameKind,revision:ULong=0u,value:String="") = CompanionConfigurationFrame(0x86,7u,sent.last().exchangeId,
            checkNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(kind,revision,value))))
        fun time(payload:CompanionTimePayload,id:UInt=sent.last().exchangeId)=CompanionConfigurationFrame(0x87,7u,id,
            checkNotNull(CompanionConfigurationCodec.encodeTime(payload)))
    }
    @Test fun readApplyUsesExactReadbackAndSharedIds() {
        val f=Fixture();assertFalse(f.session.writeName(requireNotNull(V1DeviceName.create("Camp"))))
        assertTrue(f.session.readName());assertEquals(11u,f.sent.last().exchangeId)
        assertFalse(f.session.synchronizeTime())
        assertTrue(f.session.receive(f.name(CompanionNameKind.SNAPSHOT)))
        assertTrue(f.session.writeName(requireNotNull(V1DeviceName.create("Camp"))))
        assertEquals(12u,f.sent.last().exchangeId)
        assertTrue(f.session.receive(f.name(CompanionNameKind.APPLIED,1u,"Camp")))
        assertEquals("Camp",f.session.state.deviceName);assertEquals(1uL,f.session.state.nameRevision)
    }
    @Test fun lateOrWrongResponseCannotConfirmCurrentOperation() {
        val f=Fixture();f.session.readName();val old=f.name(CompanionNameKind.SNAPSHOT)
        f.session.lost(old.exchangeId);f.session.readName()
        assertFalse(f.session.receive(old));assertTrue(f.session.busy)
        assertTrue(f.session.receive(f.name(CompanionNameKind.SNAPSHOT)))
        f.session.writeName(requireNotNull(V1DeviceName.create("Camp")))
        assertTrue(f.session.receive(f.name(CompanionNameKind.APPLIED,2u,"Camp")))
        assertNull(f.session.state.nameRevision);assertNull(f.session.state.deviceName)
    }
    @Test fun timeChallengeContinuationUsesFreshExchangeAndExactChallenge() {
        val f=Fixture();assertTrue(f.session.synchronizeTime());val request=f.sent.last()
        assertTrue(f.session.receive(f.time(CompanionTimePayload(2,challenge=ULong.MAX_VALUE))))
        val sample=f.sent.last();assertEquals(request.exchangeId+1u,sample.exchangeId)
        val decoded=assertNotNull(CompanionConfigurationCodec.decodeTime(sample.payload))
        assertEquals(ULong.MAX_VALUE,decoded.challenge);assertEquals(86399u,decoded.localSecond);assertEquals(2,decoded.format)
        assertFalse(f.session.receive(f.time(CompanionTimePayload(4,challenge=1u))))
        assertTrue(f.session.busy)
        assertTrue(f.session.receive(f.time(CompanionTimePayload(4,challenge=ULong.MAX_VALUE))))
        assertFalse(f.session.busy);assertEquals("Display clock synchronized.",f.session.state.notice)
    }
    @Test fun writeFailureRequiresReadAndNoAutomaticRetry() {
        val f=Fixture();f.writeResult=false;assertFalse(f.session.readName());assertFalse(f.session.busy)
        assertEquals(1,f.sent.size);assertNull(f.session.state.nameRevision)
        assertFalse(f.session.writeName(requireNotNull(V1DeviceName.create("Camp"))))
        f.writeResult=true;assertTrue(f.session.readName());assertEquals(2,f.sent.size)
    }
    @Test fun disconnectClosesQueuedWorkAndLateCallbacks() {
        val f=Fixture();f.session.readName();val response=f.name(CompanionNameKind.SNAPSHOT)
        f.current=false;f.session.close();assertFalse(f.session.receive(response))
        assertFalse(f.session.readName());assertFalse(f.session.synchronizeTime())
    }
    @Test fun staleTimeoutDoesNotCancelTimeContinuation() {
        val f=Fixture();f.session.synchronizeTime();val old=f.sent.last().exchangeId
        f.session.receive(f.time(CompanionTimePayload(2,challenge=12u)))
        f.session.lost(old);assertTrue(f.session.busy)
        f.session.lost(f.sent.last().exchangeId);assertFalse(f.session.busy)
    }
}
