package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import kotlin.test.*

class BlePublicProfileSettingsTest {
    private class Fixture(val minor: Int = 5) {
        val context=V1NameContext(1,2,3,4,5,6,7u,null,checkNotNull(V1SetupSessionToken.create(ByteArray(16){1})))
        var current=true; var next=11u; var sentOkay=true
        val sent=mutableListOf<CompanionConfigurationFrame>()
        val session=BleConfigurationSession(context,{current},{next++},{ bytes ->
            sent+=checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes,minor));sentOkay
        },{ 0u to 1 },{},minor)
        fun name() {
            assertTrue(session.readName())
            assertTrue(session.receive(CompanionConfigurationFrame(0x86,7u,sent.last().exchangeId,
                checkNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.SNAPSHOT,1u,"Camp"))),minor)))
        }
        fun region() {
            assertTrue(session.readRegion())
            assertTrue(session.receive(CompanionConfigurationFrame(0x88,7u,sent.last().exchangeId,
                checkNotNull(CompanionConfigurationCodec.encodeRegion(CompanionRegionPayload(0x81,revision=1u,selectionId=12))),minor)))
        }
        fun prepared() { name();region() }
        fun reply(value: CompanionPublicProfilePayload, nonce: UInt=7u, exchange: UInt=sent.last().exchangeId,
                  version: Int=minor) = CompanionConfigurationFrame(0x8a,nonce,exchange,
            checkNotNull(CompanionPublicProfileCodec.encode(value)),version)
        fun read(revision: ULong=0u, name: String="", visible: Boolean=true) {
            assertTrue(session.readPublicProfile())
            assertTrue(session.receive(reply(CompanionPublicProfilePayload(0x81,revision=revision,name=name,visible=visible))))
        }
    }

    @Test fun FreshNameRegionAndProfileReadAreRequiredBeforeAtomicWrite() {
        val f=Fixture()
        assertFalse(f.session.readPublicProfile());f.name()
        assertFalse(f.session.readPublicProfile());f.region()
        assertFalse(f.session.writePublicProfile("Trail Person",false))
        f.read();assertEquals(0uL,f.session.state.publicRevision);assertEquals(true,f.session.state.publiclyDiscoverable)
        assertTrue(f.session.writePublicProfile("Trail Person",false))
        val request=assertNotNull(CompanionPublicProfileCodec.decode(f.sent.last().payload))
        assertEquals(0uL,request.revision);assertFalse(request.visible)
        assertNull(f.session.state.publicName);assertNull(f.session.state.publiclyDiscoverable)
        assertTrue(f.session.receive(f.reply(CompanionPublicProfilePayload(0x82,revision=1u,name="Trail Person",visible=false))))
        assertEquals("Trail Person",f.session.state.publicName);assertEquals(false,f.session.state.publiclyDiscoverable)
        assertEquals(1uL,f.session.state.publicRevision)
        assertTrue(V1OnboardingProjection.publicVerified(f.session.state));assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
    }

    @Test fun WrongSessionProfileAndExchangeCannotPublishCurrentValues() {
        val f=Fixture();f.prepared();f.session.readPublicProfile()
        val value=CompanionPublicProfilePayload(0x81,revision=1u,name="Trail Person")
        assertFalse(f.session.receive(f.reply(value,nonce=8u)))
        assertFalse(f.session.receive(f.reply(value,version=3)))
        assertFalse(f.session.receive(f.reply(value,exchange=f.sent.last().exchangeId+1u)))
        assertNull(f.session.state.publicName);assertTrue(f.session.busy)
        val old=f.reply(value);f.session.lost(old.exchangeId);assertTrue(f.session.readPublicProfile())
        assertFalse(f.session.receive(old));assertNull(f.session.state.publicName)
        f.current=false;f.session.close();assertFalse(f.session.receive(f.reply(value)))
        assertNull(f.session.state.publicName);assertNull(f.session.state.publicRevision)
    }

    @Test fun UncertainCommitAndInvalidApplyNeverSupplyReusableAuthorityOrRetry() {
        for (fault in listOf("uncertain","revision","name","visibility","lost","send")) {
            val f=Fixture();f.prepared();f.read(4u,"Trail Person",true)
            if(fault=="send") f.sentOkay=false
            val started=f.session.writePublicProfile("Trail Person",false)
            if(fault=="send") assertFalse(started) else assertTrue(started)
            when(fault) {
                "uncertain" -> f.session.receive(f.reply(CompanionPublicProfilePayload(0x84,reason=4)))
                "revision" -> f.session.receive(f.reply(CompanionPublicProfilePayload(0x82,revision=6u,name="Trail Person")))
                "name" -> f.session.receive(f.reply(CompanionPublicProfilePayload(0x82,revision=5u,name="Another Person")))
                "visibility" -> f.session.receive(f.reply(CompanionPublicProfilePayload(0x82,revision=5u,name="Trail Person",visible=true)))
                "lost" -> f.session.lost(f.sent.last().exchangeId)
            }
            assertNull(f.session.state.publicRevision,fault);assertNull(f.session.state.publicName,fault)
            assertNull(f.session.state.publiclyDiscoverable,fault)
            val count=f.sent.size;assertFalse(f.session.writePublicProfile("Trail Person",false));assertEquals(count,f.sent.size)
        }
    }

    @Test fun GenericWireAliasDoesNotBypassProductSemanticValidation() {
        val f=Fixture();f.prepared();assertTrue(f.session.readPublicProfile())
        val invalidAlias=CompanionPublicProfilePayload(0x81,revision=1u,name="!")
        assertNotNull(CompanionPublicProfileCodec.encode(invalidAlias))
        assertTrue(f.session.receive(f.reply(invalidAlias)))
        assertNull(f.session.state.publicName);assertNull(f.session.state.publiclyDiscoverable);assertNull(f.session.state.publicRevision)
        assertFalse(f.session.writePublicProfile("!",true))
    }

    @Test fun LegacyProfileReconfigurationAndMaximumRevisionKeepClosedGates() {
        for(minor in listOf(2,3,CompanionConfirmationCodec.PROFILE)) {
            val f=Fixture(minor);assertFalse(f.session.state.publicProfileAvailable)
            assertFalse(f.session.readPublicProfile());assertFalse(f.session.writePublicProfile("Trail Person",true))
        }
        val f=Fixture();f.prepared();f.read(ULong.MAX_VALUE,"Trail Person",true)
        assertFalse(f.session.writePublicProfile("Trail Person",false))
        assertTrue(f.session.readName());assertNull(f.session.state.publicName);assertNull(f.session.state.publicRevision)
        assertFalse(f.session.readPublicProfile())
    }
}
