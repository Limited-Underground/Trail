package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import java.util.Locale
import kotlin.test.*

class V1OnboardingBindingTest {
    private class Fixture(val endpoint: String = "opaque-a", val nonce: Long = 7, val owner: Long = 1) {
        val companion = BleDiscoveredCompanion(endpoint, "Trail-23ABCD")
        var current = true
        var next = 1u
        val sent = mutableListOf<CompanionConfigurationFrame>()
        var nonReady: BleRuntimeState? = null
        val context = V1NameContext(owner,2,3,4,5,6,nonce.toUInt(),
            V1SetupLabel.create("Trail-23ABCD"),checkNotNull(V1SetupSessionToken.create(ByteArray(16){1})))
        val configuration = BleConfigurationSession(context,{current},{next++},{bytes ->
            sent += checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes,3));true
        },{ 0u to 1 },{},3)
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
        } }
        val scope get() = V1OnboardingScope.from(session())
        val stage get() = V1OnboardingProjection.stage(runtime())
        fun submit(command: V1OnboardingCommand) = binding.submit(scope,command)
        fun name(kind: CompanionNameKind, revision: ULong=0u, value: String="", exchange: UInt=sent.last().exchangeId) =
            configuration.receive(CompanionConfigurationFrame(0x86,nonce.toUInt(),exchange,
                checkNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(kind,revision,value))),3))
        fun region(kind: Int, revision: ULong=0u, selection: Int=0, exchange: UInt=sent.last().exchangeId) =
            configuration.receive(CompanionConfigurationFrame(0x88,nonce.toUInt(),exchange,
                checkNotNull(CompanionConfigurationCodec.encodeRegion(CompanionRegionPayload(kind,revision=revision,selectionId=selection))),3))
        fun named() {
            assertTrue(submit(V1OnboardingCommand.ReadName))
            assertTrue(name(CompanionNameKind.SNAPSHOT,1u,"Camp"))
        }
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
        assertFalse(V1OnboardingProjection.canComplete)
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
            assertFalse(V1OnboardingProjection.canComplete)
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
                assertFalse(V1OnboardingProjection.canComplete)
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
