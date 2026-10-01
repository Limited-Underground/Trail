package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import java.util.Locale
import kotlin.test.*

class V1OnboardingBindingTest {
    private class Fixture(val endpoint: String = "opaque-a", val nonce: Long = 7, val owner: Long = 1, val profile: Int = 3) {
        val companion = BleDiscoveredCompanion(endpoint, "Trail-23ABCD")
        var current = true
        var next = 1u
        val sent = mutableListOf<CompanionConfigurationFrame>()
        var nonReady: BleRuntimeState? = null
        val context = V1NameContext(owner,2,3,4,5,6,nonce.toUInt(),
            V1SetupLabel.create("Trail-23ABCD"),checkNotNull(V1SetupSessionToken.create(ByteArray(16){1})))
        val configuration = BleConfigurationSession(context,{current},{next++},{bytes ->
            sent += checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes,profile));true
        },{ 0u to 1 },{},profile)
        fun session() = BleActiveSession(companion,nonce,
            CompanionStatusSnapshot(1,CompanionRadioState.UNKNOWN,CompanionGnssState.UNKNOWN,
                CompanionPowerState.UNKNOWN,CompanionPositionSharingState.STOPPED,0),
            CompanionProtocolInfo(capabilities=0),
            checkNotNull(GroupLocationSnapshot.authoritativeUnavailable(1,CompanionPositionSharingState.STOPPED)),
            configuration=configuration.state)
        fun runtime(): BleRuntimeState = nonReady ?: BleRuntimeState.Ready(session())
        val binding = V1OnboardingBinding({runtime()}) { command -> when(command) {
            V1OnboardingCommand.ReadName -> configuration.readName()
            is V1OnboardingCommand.WriteName -> configuration.writeName(checkNotNull(V1DeviceName.create(command.value)))
            V1OnboardingCommand.ReadRegion -> configuration.readRegion()
            is V1OnboardingCommand.WriteRegion -> configuration.writeRegion(command.selectionId)
            V1OnboardingCommand.ReadPublicProfile -> configuration.readPublicProfile()
            is V1OnboardingCommand.WritePublicProfile -> configuration.writePublicProfile(command.name,command.visible)
        } }
        val scope get() = V1OnboardingScope.from(session())
        val stage get() = V1OnboardingProjection.stage(runtime())
        fun submit(command: V1OnboardingCommand) = binding.submit(scope,command)
        fun name(kind: CompanionNameKind, revision: ULong=0u, value: String="", exchange: UInt=sent.last().exchangeId) =
            configuration.receive(CompanionConfigurationFrame(0x86,nonce.toUInt(),exchange,
                checkNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(kind,revision,value))),profile))
        fun region(kind: Int, revision: ULong=0u, selection: Int=0, exchange: UInt=sent.last().exchangeId) =
            configuration.receive(CompanionConfigurationFrame(0x88,nonce.toUInt(),exchange,
                checkNotNull(CompanionConfigurationCodec.encodeRegion(CompanionRegionPayload(kind,revision=revision,selectionId=selection))),profile))
        fun public(kind: Int=0x81, revision: ULong=1u, name: String="Rider", visible: Boolean=false,
                   exchange: UInt=sent.last().exchangeId) = configuration.receive(
            CompanionConfigurationFrame(0x8a,nonce.toUInt(),exchange,
                checkNotNull(CompanionPublicProfileCodec.encode(CompanionPublicProfilePayload(kind,revision=revision,name=name,visible=visible))),profile))
        fun configured() {
            named();assertTrue(submit(V1OnboardingCommand.ReadRegion));assertTrue(region(0x81,1u,12))
            assertTrue(submit(V1OnboardingCommand.ReadPublicProfile));assertTrue(public())
        }
        fun named() {
            assertTrue(submit(V1OnboardingCommand.ReadName))
            assertTrue(name(CompanionNameKind.SNAPSHOT,1u,"Camp"))
        }
    }

    @Test fun verifiedPublicOffCompletesAndOrdinarySettingsRemainUsable() {
        val f=Fixture(profile=5);f.configured()
        assertEquals(V1SetupStage.COMPLETE,f.stage);assertTrue(V1OnboardingProjection.canComplete(f.runtime()))
        assertEquals(false,f.configuration.state.publiclyDiscoverable)
        assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
        assertTrue(f.binding.submit(f.scope,V1OnboardingCommand.ReadPublicProfile,onboarding=false))
        assertEquals(V1SetupStage.PUBLIC_PROFILE,f.stage)
        assertTrue(f.public());assertEquals(V1SetupStage.COMPLETE,f.stage)
        assertTrue(f.binding.submit(f.scope,V1OnboardingCommand.WritePublicProfile("Rider",true),onboarding=false))
        assertEquals(V1SetupStage.PUBLIC_PROFILE,f.stage)
        assertTrue(f.public(kind=0x82,revision=2u,visible=true))
        assertEquals(V1SetupStage.COMPLETE,f.stage)
    }

    @Test fun interruptionAtEveryStepResumesFreshEarliestReadbackAndNeverUsesAnOldCompletion() {
        for(step in 0..3) {
            val old=Fixture(profile=5);old.configured();val oldScope=old.scope
            old.current=false;old.configuration.close()
            val f=Fixture(nonce=8,owner=2,profile=5)
            assertEquals(V1SetupStage.NAME_DEVICE,f.stage)
            assertFalse(f.binding.submit(oldScope,V1OnboardingCommand.ReadPublicProfile))
            assertTrue(f.submit(V1OnboardingCommand.ReadName));assertTrue(f.name(CompanionNameKind.SNAPSHOT,
                if(step>0)1u else 0u,if(step>0)"Camp" else ""))
            if(step==0) { assertEquals(V1SetupStage.NAME_DEVICE,f.stage);continue }
            assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,f.stage)
            assertTrue(f.submit(V1OnboardingCommand.ReadRegion));assertTrue(f.region(0x81,if(step>1)1u else 0u,if(step>1)12 else 0))
            if(step==1) { assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,f.stage);continue }
            assertEquals(V1SetupStage.PUBLIC_PROFILE,f.stage)
            assertTrue(f.submit(V1OnboardingCommand.ReadPublicProfile))
            assertTrue(f.public(revision=if(step>2)1u else 0u,name=if(step>2)"Rider" else "",visible=step<=2))
            assertEquals(if(step>2)V1SetupStage.COMPLETE else V1SetupStage.PUBLIC_PROFILE,f.stage)
            assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
        }
    }

    @Test fun navigationRechecksExactLiveCompletionAfterUncertaintyAndClockWork() {
        val f=Fixture(profile=5);f.configured();val scope=f.scope
        val navigation=V1HomeLaunchState.initial(TrailAppUiState.ChooseMode)
        assertTrue(f.submit(V1OnboardingCommand.WritePublicProfile("Rider",true)))
        assertSame(navigation,navigation.finish(scope,f.runtime()))
        val pending=f.sent.last().exchangeId;f.configuration.lost(pending)
        assertFalse(f.public(kind=0x82,revision=2u,visible=true,exchange=pending))
        assertSame(navigation,navigation.finish(scope,f.runtime()))
        assertEquals(V1SetupStage.PUBLIC_PROFILE,f.stage)
        assertTrue(f.submit(V1OnboardingCommand.ReadPublicProfile));assertTrue(f.public())
        assertTrue(f.configuration.synchronizeTime())
        assertSame(navigation,navigation.finish(scope,f.runtime()))
        f.configuration.lost(f.sent.last().exchangeId)
        val completed=navigation.finish(scope,f.runtime())
        assertEquals(V1AppDestination.MESSAGES,completed.destination);assertTrue(completed.completionHandled)
        assertSame(completed,completed.finish(scope,f.runtime()))
        val rotated=assertNotNull(V1HomeLaunchState.restore(completed.save()))
        assertEquals(V1AppDestination.MESSAGES,rotated.destination)
        assertFalse(rotated.beginSetup(f.runtime()).setupArmed)
        assertSame(navigation,navigation.finish(scope,BleRuntimeState.Reconnecting(f.companion,1,3)))
        for(replacement in listOf(Fixture(endpoint="opaque-b",profile=5),Fixture(nonce=8,profile=5),Fixture(owner=9,profile=5))) {
            replacement.configured();assertSame(navigation,navigation.finish(scope,replacement.runtime()))
        }
    }

    @Test fun completeReadbacksNeverCreateAuthorityOutsideProtectedReadyOrFromLocale() {
        val f=Fixture(profile=5);f.configured()
        val scope=f.scope;val navigation=V1HomeLaunchState.initial(TrailAppUiState.ChooseMode)
        for(runtime in listOf(BleRuntimeState.Inactive,BleRuntimeState.Idle,
            BleRuntimeState.Connecting(f.companion),BleRuntimeState.AwaitingAuthorization(f.companion),
            BleRuntimeState.Reconnecting(f.companion,1,3),BleRuntimeState.FindingReturningOwner,
            BleRuntimeState.FactoryResetComplete(true),BleRuntimeState.ScanComplete(emptyList()))) {
            assertFalse(V1OnboardingProjection.canComplete(runtime))
            assertSame(navigation,navigation.finish(scope,runtime))
        }
        val previous=Locale.getDefault()
        try {
            for(locale in listOf(Locale.US,Locale.JAPAN,Locale.FRANCE)) {
                Locale.setDefault(locale)
                val ready=Fixture(profile=5);ready.configured()
                assertEquals(12,ready.configuration.state.regionSelectionId)
                assertTrue(V1OnboardingProjection.canComplete(ready.runtime()))
                assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
                assertEquals(listOf(4,6,8),ready.sent.map { it.kind })
            }
        } finally { Locale.setDefault(previous) }
    }

    @Test fun delayedComposedCompletionRechecksLiveControllerGetterAtDispatch() {
        for(fault in listOf("write","disconnect","replacement")) {
            val f=Fixture(profile=5);f.configured()
            val composed=f.runtime();val scope=f.scope
            val navigation=V1HomeLaunchState.initial(TrailAppUiState.ChooseMode)
            var liveRuntime: BleRuntimeState?=composed
            var reads=0
            val delayedEffect={ navigation.finish(scope) { reads++;liveRuntime } }
            when(fault) {
                "write" -> {
                    assertTrue(f.submit(V1OnboardingCommand.WritePublicProfile("Rider",true)))
                    liveRuntime=f.runtime()
                }
                "disconnect" -> liveRuntime=BleRuntimeState.Reconnecting(f.companion,1,3)
                "replacement" -> {
                    val replacement=Fixture(nonce=8,owner=2,profile=5);replacement.configured()
                    liveRuntime=replacement.runtime()
                }
            }
            // The unchanged composed snapshot is still COMPLETE, but the live dispatch must refuse.
            assertTrue(V1OnboardingProjection.canComplete(composed))
            assertSame(navigation,delayedEffect(),fault);assertEquals(1,reads)
            assertEquals(V1AppDestination.DEVICE,navigation.destination)
        }
    }

    @Test fun explicitTabsAndRestoredNavigationMarkersCannotSupplyCompletionAuthority() {
        val f=Fixture(profile=5);f.configured();val scope=f.scope
        for(destination in V1AppDestination.entries) {
            val explicit=V1HomeLaunchState.initial(TrailAppUiState.ChooseMode).select(destination)
            val rotated=assertNotNull(V1HomeLaunchState.restore(explicit.save()))
            assertSame(rotated,rotated.finish(scope,f.runtime()));assertEquals(destination,rotated.destination)
            assertSame(rotated,rotated.finish(scope,BleRuntimeState.Reconnecting(f.companion,1,3)))
        }
        val resumed=assertNotNull(V1HomeLaunchState.restore(V1HomeLaunchState.initial(TrailAppUiState.ChooseMode).save()))
        val fresh=Fixture(owner=50,nonce=70,profile=5)
        assertSame(resumed,resumed.finish(scope,fresh.runtime()))
        assertFalse(V1OnboardingProjection.canComplete(fresh.runtime()))
        assertFalse(resumed.save().toString().contains(f.companion.endpointToken))
        assertNull(V1HomeLaunchState.restore(listOf("DEVICE",true,true)))
    }

    @Test fun publicSettingsCommandsBindFreshProfileEndpointNonceAndEditorOwner() {
        fun prepared(f: Fixture): Fixture {
            f.named();assertTrue(f.submit(V1OnboardingCommand.ReadRegion));assertTrue(f.region(0x81,1u,12))
            return f
        }
        fun publicReply(f: Fixture, revision: ULong, name: String, visible: Boolean): Boolean =
            f.configuration.receive(CompanionConfigurationFrame(0x8a,f.nonce.toUInt(),f.sent.last().exchangeId,
                checkNotNull(CompanionPublicProfileCodec.encode(CompanionPublicProfilePayload(0x81,revision=revision,name=name,visible=visible))),5))
        val original=prepared(Fixture(profile=5));val oldScope=original.scope
        assertFalse(original.submit(V1OnboardingCommand.WritePublicProfile("Rider",false)))
        assertTrue(original.submit(V1OnboardingCommand.ReadPublicProfile));assertTrue(publicReply(original,1u,"Rider",true))
        assertTrue(original.submit(V1OnboardingCommand.WritePublicProfile("Rider",false)))
        val write=original.sent.last();assertEquals(8,write.kind)
        val values=checkNotNull(CompanionPublicProfileCodec.decode(write.payload))
        assertEquals(1uL,values.revision);assertFalse(values.visible)
        original.configuration.lost(write.exchangeId)
        assertNull(original.configuration.state.publicRevision)
        assertFalse(original.submit(V1OnboardingCommand.WritePublicProfile("Rider",false)))
        for(replacement in listOf(Fixture(endpoint="opaque-b",profile=5),Fixture(nonce=8,profile=5),Fixture(owner=2,profile=5))) {
            prepared(replacement);assertTrue(replacement.submit(V1OnboardingCommand.ReadPublicProfile))
            assertTrue(publicReply(replacement,1u,"Rider",true))
            val count=replacement.sent.size
            assertFalse(replacement.binding.submit(oldScope,V1OnboardingCommand.ReadPublicProfile))
            assertFalse(replacement.binding.submit(oldScope,V1OnboardingCommand.WritePublicProfile("Rider",false),onboarding=false))
            assertEquals(count,replacement.sent.size)
            assertTrue(replacement.submit(V1OnboardingCommand.WritePublicProfile("Rider",false)))
        }
        original.current=false;original.configuration.close()
        original.nonReady=BleRuntimeState.Reconnecting(original.companion,1,3)
        assertFalse(original.binding.submit(oldScope,V1OnboardingCommand.ReadPublicProfile))
        assertNull(original.configuration.state.publicName)
        assertFalse(V1OnboardingProjection.canComplete(original.runtime()));assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
    }

    @Test fun orderedActionsUseRealProtectedConfigurationReadbacksAndNeverCompleteUnsupportedProfile() {
        val f=Fixture()
        assertEquals(V1SetupStage.NAME_DEVICE,f.stage)
        assertFalse(f.submit(V1OnboardingCommand.ReadRegion))
        assertFalse(f.submit(V1OnboardingCommand.WriteRegion(1)))
        assertFalse(f.submit(V1OnboardingCommand.WriteName("Camp")))
        assertTrue(f.submit(V1OnboardingCommand.ReadName))
        assertTrue(f.name(CompanionNameKind.SNAPSHOT))
        assertEquals(V1SetupStage.NAME_DEVICE,f.stage)
        assertTrue(f.submit(V1OnboardingCommand.WriteName("Camp")))
        assertNull(f.configuration.state.nameRevision)
        assertEquals(V1SetupStage.NAME_DEVICE,f.stage)
        assertTrue(f.name(CompanionNameKind.APPLIED,1u,"Camp"))
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,f.stage)
        assertFalse(f.submit(V1OnboardingCommand.WriteName("Skip back")))
        assertFalse(f.submit(V1OnboardingCommand.WriteRegion(1)))
        assertTrue(f.submit(V1OnboardingCommand.ReadRegion))
        assertTrue(f.region(0x81))
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,f.stage)
        assertTrue(f.submit(V1OnboardingCommand.WriteRegion(1)))
        assertTrue(f.region(0x82,1u,1))
        // Applied alone is not a reusable readback revision. Require a fresh read.
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,f.stage)
        assertTrue(f.submit(V1OnboardingCommand.ReadRegion))
        assertTrue(f.region(0x81,1u,1))
        assertEquals(V1SetupStage.PUBLIC_PROFILE,f.stage)
        assertFalse(V1OnboardingProjection.canComplete(f.runtime()))
        assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
    }

    @Test fun interruptedStepsResumeOnlyFromFreshReadbacksOfCurrentDevice() {
        for(completedStep in 0..3) {
            val original=Fixture(); val oldScope=original.scope
            if(completedStep>=1)original.named()
            if(completedStep>=2){original.submit(V1OnboardingCommand.ReadRegion);original.region(0x81,1u,1)}
            original.current=false;original.configuration.close()
            original.nonReady=BleRuntimeState.Reconnecting(original.companion,1,3)
            assertEquals(V1SetupStage.SECURE_PAIR_AND_AUTHORIZE,original.stage)
            assertFalse(original.binding.submit(oldScope,V1OnboardingCommand.ReadName))
            val resumed=Fixture(nonce=8,owner=20)
            assertEquals(V1SetupStage.NAME_DEVICE,resumed.stage)
            assertFalse(resumed.binding.submit(oldScope,V1OnboardingCommand.ReadName))
            assertTrue(resumed.submit(V1OnboardingCommand.ReadName))
            assertTrue(resumed.name(CompanionNameKind.SNAPSHOT,if(completedStep>=1)1u else 0u,if(completedStep>=1)"Camp" else ""))
            if(completedStep==0)assertEquals(V1SetupStage.NAME_DEVICE,resumed.stage)
            else {
                assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,resumed.stage)
                assertTrue(resumed.submit(V1OnboardingCommand.ReadRegion))
                assertTrue(resumed.region(0x81,if(completedStep>=2)1u else 0u,if(completedStep>=2)1 else 0))
                assertEquals(if(completedStep>=2)V1SetupStage.PUBLIC_PROFILE else V1SetupStage.CONFIRM_RADIO_REGION,resumed.stage)
            }
            assertFalse(V1OnboardingProjection.canComplete(resumed.runtime()))
        }
    }

    @Test fun oldDeviceNonceAndConfigurationOwnerCannotBeUsedByStaleUiActions() {
        val first=Fixture();val scope=first.scope
        for(replacement in listOf(Fixture(endpoint="opaque-b"),Fixture(nonce=8),Fixture(owner=2))) {
            assertFalse(replacement.binding.submit(scope,V1OnboardingCommand.ReadName))
            assertFalse(replacement.binding.submit(scope,V1OnboardingCommand.WriteName("Camp"),onboarding=false))
            assertTrue(replacement.sent.isEmpty())
        }
        assertFalse(scope.toString().contains("opaque-a"))
    }

    @Test fun uncertaintyAndOldResponsesNeverAdvanceTheNextStep() {
        val f=Fixture();f.named()
        assertTrue(f.binding.submit(f.scope,V1OnboardingCommand.WriteName("Changed"),onboarding=false))
        assertEquals(V1SetupStage.NAME_DEVICE,f.stage)
        val old=f.sent.last().exchangeId;f.configuration.lost(old)
        assertFalse(f.name(CompanionNameKind.APPLIED,2u,"Changed",old))
        assertFalse(f.submit(V1OnboardingCommand.ReadRegion))
        f.named()
        assertTrue(f.submit(V1OnboardingCommand.ReadRegion));f.region(0x81)
        assertTrue(f.submit(V1OnboardingCommand.WriteRegion(1)))
        val pending=f.sent.last().exchangeId;f.configuration.lost(pending)
        assertFalse(f.region(0x82,1u,1,pending))
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION,f.stage)
        assertFalse(f.submit(V1OnboardingCommand.WriteRegion(1)))
    }

    @Test fun settingsCannotSkipNameOrMutateWhileBusyAndAllCatalogSelectionsRemainExplicit() {
        val f=Fixture()
        assertFalse(f.binding.submit(f.scope,V1OnboardingCommand.ReadRegion,onboarding=false))
        assertTrue(f.submit(V1OnboardingCommand.ReadName))
        assertFalse(f.binding.submit(f.scope,V1OnboardingCommand.ReadName,onboarding=false))
        f.name(CompanionNameKind.SNAPSHOT,1u,"Camp")
        assertTrue(f.binding.submit(f.scope,V1OnboardingCommand.ReadRegion,onboarding=false))
        f.region(0x81)
        assertFalse(f.submit(V1OnboardingCommand.WriteRegion(65535)))
        assertTrue(f.submit(V1OnboardingCommand.WriteRegion(RegionSelectionCatalog.entries.last().id)))
    }

    @Test fun localDraftAndPhoneLocaleCannotProvideVerificationOrTransmitPermission() {
        val previous=Locale.getDefault()
        try {
            for(locale in listOf(Locale.US,Locale.JAPAN,Locale.FRANCE)) {
                Locale.setDefault(locale)
                assertNotNull(V1SetupDraft.create("Camp","Brian",V1RadioRegion.US915,false))
                val f=Fixture()
                assertEquals(V1SetupStage.NAME_DEVICE,f.stage)
                assertTrue(f.sent.isEmpty())
                assertFalse(V1OnboardingProjection.radioTransmissionAllowed)
                assertFalse(V1OnboardingProjection.canComplete(f.runtime()))
            }
        } finally { Locale.setDefault(previous) }
    }

    @Test fun disconnectedResetAndPairingStagesCannotPublishConfigurationProgress() {
        val f=Fixture()
        for(runtime in listOf(BleRuntimeState.Inactive,BleRuntimeState.Idle,
            BleRuntimeState.FactoryResetComplete(true),BleRuntimeState.Scanning(emptyList()))) {
            assertEquals(V1SetupStage.MATCH_DEVICE,V1OnboardingProjection.stage(runtime))
            f.nonReady=runtime;assertFalse(f.submit(V1OnboardingCommand.ReadName))
        }
        for(runtime in listOf(BleRuntimeState.Connecting(f.companion),BleRuntimeState.AwaitingAuthorization(f.companion),
            BleRuntimeState.FindingReturningOwner))assertEquals(V1SetupStage.SECURE_PAIR_AND_AUTHORIZE,V1OnboardingProjection.stage(runtime))
    }
}
