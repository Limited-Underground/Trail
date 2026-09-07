package io.github.nbjelanovic.otclient
import io.github.nbjelanovic.otprotocol.*
import kotlin.test.*

class BleConfigurationSessionTest {
    private class Fixture(val minor: Int = 2, label: String? = null) {
        val context=V1NameContext(1,2,3,4,5,6,7u,label?.let { V1SetupLabel.create(it) },requireNotNull(V1SetupSessionToken.create(ByteArray(16){1})))
        var current=true;var next=11u;var writeResult=true
        val sent=mutableListOf<CompanionConfigurationFrame>()
        val session=BleConfigurationSession(context,{current},{next++},{bytes->
            sent+=checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes,minor));writeResult
        },{ 86399u to 2 },{},minor)
        fun name(kind:CompanionNameKind,revision:ULong=0u,value:String="") = CompanionConfigurationFrame(0x86,7u,sent.last().exchangeId,
            checkNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(kind,revision,value))))
        fun time(payload:CompanionTimePayload,id:UInt=sent.last().exchangeId)=CompanionConfigurationFrame(0x87,7u,id,
            checkNotNull(CompanionConfigurationCodec.encodeTime(payload)))
    }
    private fun Fixture.region(value: CompanionRegionPayload, exchange: UInt=sent.last().exchangeId, nonce: UInt=7u, version: Int=minor) =
        CompanionConfigurationFrame(0x88,nonce,exchange,checkNotNull(CompanionConfigurationCodec.encodeRegion(value)),version)
    @Test fun suggestionRequiresCurrentEmptyReadbackAndOnlyExplicitApplyPersists() {
        val f=Fixture(label="Trail-23ABCD")
        assertNull(f.session.state.suggestedDeviceName)
        f.session.readName();val response=f.name(CompanionNameKind.SNAPSHOT)
        assertFalse(f.session.receive(CompanionConfigurationFrame(response.kind,8u,response.exchangeId,response.payload)))
        assertNull(f.session.state.suggestedDeviceName)
        assertTrue(f.session.receive(response))
        assertEquals("Trail-23ABCD",f.session.state.suggestedDeviceName)
        assertNull(f.session.state.deviceName);assertEquals(1,f.sent.size)
        assertTrue(f.session.writeName(requireNotNull(V1DeviceName.create("Trail-23ABCD"))))
        assertTrue(f.session.receive(f.name(CompanionNameKind.APPLIED,1u,"Trail-23ABCD")))
        assertEquals("Trail-23ABCD",f.session.state.deviceName)
        assertNull(f.session.state.suggestedDeviceName)
    }
    @Test fun suggestionNeverOverridesSavedNameAndIsRemovedOnSessionClose() {
        val f=Fixture(label="Trail-23ABCD")
        f.session.readName();f.session.receive(f.name(CompanionNameKind.SNAPSHOT,1u,"Camp"))
        assertNull(f.session.state.suggestedDeviceName)
        f.session.readName();f.session.receive(f.name(CompanionNameKind.SNAPSHOT))
        assertNotNull(f.session.state.suggestedDeviceName)
        f.session.close();assertNull(f.session.state.suggestedDeviceName)
        val ordinary=Fixture();ordinary.session.readName();ordinary.session.receive(ordinary.name(CompanionNameKind.SNAPSHOT))
        assertNull(ordinary.session.state.suggestedDeviceName)
    }
    @Test fun regionRequiresFreshReadAndExactRevisionSelectionAndCurrentResponse() {
        val f=Fixture(3)
        assertFalse(f.session.writeRegion(1));assertTrue(f.session.readRegion())
        val old=f.sent.last().exchangeId
        assertFalse(f.session.receive(f.region(CompanionRegionPayload(0x81),nonce=8u)))
        assertFalse(f.session.receive(f.region(CompanionRegionPayload(0x81),version=2)))
        assertTrue(f.session.receive(f.region(CompanionRegionPayload(0x81))))
        assertTrue(f.session.writeRegion(12));assertFalse(f.session.readName())
        assertFalse(f.session.receive(f.region(CompanionRegionPayload(0x81),exchange=old)))
        assertTrue(f.session.receive(f.region(CompanionRegionPayload(0x82,revision=1u,selectionId=11))))
        assertNull(f.session.state.regionRevision);assertNull(f.session.state.regionSelectionId)
        assertFalse(f.session.writeRegion(12))
        assertTrue(f.session.readRegion())
        assertTrue(f.session.receive(f.region(CompanionRegionPayload(0x81,revision=ULong.MAX_VALUE,selectionId=12))))
        assertFalse(f.session.writeRegion(1))
    }
    @Test fun regionUncertaintyLossAndReconnectNeverInventConfirmation() {
        val f=Fixture(3);f.session.readRegion();f.session.receive(f.region(CompanionRegionPayload(0x81)))
        f.session.writeRegion(1);val pending=f.sent.last().exchangeId
        f.session.lost(pending);assertFalse(f.session.busy);assertNull(f.session.state.regionRevision)
        f.session.readRegion();f.session.lost(pending);assertTrue(f.session.busy)
        f.session.receive(f.region(CompanionRegionPayload(0x81,revision=1u,selectionId=65535)))
        assertNull(f.session.state.regionRevision)
        f.session.readRegion();val late=f.region(CompanionRegionPayload(0x81))
        f.current=false;f.session.close();assertFalse(f.session.receive(late));assertFalse(f.session.readRegion())
        assertNull(Fixture(3).session.state.regionRevision)
        assertFalse(Fixture().session.readRegion())
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
