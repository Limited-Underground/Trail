package io.github.nbjelanovic.otclient
import io.github.nbjelanovic.otprotocol.*
import java.nio.ByteBuffer
import kotlin.test.*

class BleConfigurationSessionTest {
    private class Fixture(val minor: Int = 2, label: String? = null) {
        val context=V1NameContext(1,2,3,4,5,6,7u,label?.let { V1SetupLabel.create(it) },requireNotNull(V1SetupSessionToken.create(ByteArray(16){1})))
        var current=true;var next=11u;var writeResult=true
        var immediateWrites = 0
        val sent=mutableListOf<CompanionConfigurationFrame>()
        val session=BleConfigurationSession(context,{current},{next++},{bytes->
            sent+=checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes,minor));writeResult
        },{ 86399u to 2 },{},minor, sendImmediateLocation = { bytes ->
            immediateWrites++
            sent += checkNotNull(CompanionConfigurationCodec.decodeFrame(bytes, minor))
            writeResult
        })
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
    @Test fun regionNoticesDescribeOnlyWhatTheBoardConfirmed() {
        val f = Fixture(3)
        assertTrue(f.session.readRegion())
        assertTrue(f.session.receive(f.region(CompanionRegionPayload(0x81))))
        assertEquals("No radio region is saved.", f.session.state.notice)

        assertTrue(f.session.writeRegion(1))
        assertTrue(f.session.receive(f.region(CompanionRegionPayload(0x82, revision = 1u, selectionId = 1))))
        assertEquals("Your device saved the region and will follow its radio rules.", f.session.state.notice)

        assertTrue(f.session.readRegion())
        assertTrue(f.session.receive(f.region(CompanionRegionPayload(0x81, revision = 1u, selectionId = 1))))
        assertEquals("Your device confirmed its saved region.", f.session.state.notice)

        assertTrue(f.session.readRegion())
        f.session.lost(f.sent.last().exchangeId)
        assertEquals("Couldn't confirm the radio region.\nCheck the saved region before trying again.", f.session.state.notice)
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
        assertFalse(f.session.busy);assertEquals("Your device clock is set.",f.session.state.notice)
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
    @Test fun locationDispatchUsesTheExactSignedRosterAndTheSharedLane() {
        val f = Fixture(CompanionPublicProfileCodec.PROFILE)
        val group = f.readLocationRoster(1)
        val binding = assertNotNull(f.session.groupLocationBinding(group))
        assertNull(f.session.groupLocationBinding(group.copy()))
        assertNull(Fixture(6).session.groupLocationBinding(group))
        val outcomes = mutableListOf<CompanionLocationReply?>()
        assertTrue(f.session.submitGroupLocation(group, binding, CompanionLocationOperation.CONSENT, 0, null, outcomes::add))
        val request = f.sent.last()
        assertEquals(CompanionLocationCodec.REQUEST_KIND, request.kind)
        assertEquals(112, request.payload.size)
        val bytes = ByteBuffer.wrap(request.payload)
        assertEquals(1, bytes.get(0).toInt())
        assertEquals(1, bytes.get(1).toInt())
        assertEquals(1, bytes.get(2).toInt())
        assertEquals(1, bytes.get(3).toInt())
        assertEquals(group.groupId, bytes.getLong(4).toULong())
        assertEquals(1L, bytes.getLong(12))
        assertTrue(request.payload.copyOfRange(20, 48).all { it == 0.toByte() })
        val scope = GroupRosterResponseFixture.scope()
        assertContentEquals(scope.copyOfRange(0, 32), request.payload.copyOfRange(48, 80))
        assertContentEquals(scope.copyOfRange(40, 72), request.payload.copyOfRange(80, 112))
        assertEquals(request.exchangeId, f.session.pendingExchange())
        assertFalse(f.session.readName())
        assertFalse(f.session.submitBoardGroupAction(V1BoardGroupAction.Refresh))
        assertFalse(f.session.receive(f.locationReply(1, nonce = 8u)))
        assertFalse(f.session.receive(f.locationReply(1, exchange = request.exchangeId + 1u)))
        assertFalse(f.session.receive(f.locationReply(1, generation = 2)))
        assertFalse(f.session.receive(f.locationReply(2, lease = 7)))
        assertTrue(outcomes.isEmpty())
        assertTrue(f.session.receive(f.locationReply(1)))
        assertEquals(CompanionLocationStatus.OK, outcomes.single()?.status)
        assertFalse(f.session.busy)

        val reopened = f.readLocationRoster(1)
        assertEquals(group, reopened)
        assertNull(f.session.groupLocationBinding(group))
        assertFalse(f.session.submitGroupLocation(group, binding, CompanionLocationOperation.CONSENT, 0, null, outcomes::add))
        assertNotEquals(binding, f.session.groupLocationBinding(reopened))
    }

    @Test fun startRenewAndDeferredWithdrawalCorrelateExactLeaseAndRetireOnLoss() {
        val f = Fixture(CompanionPublicProfileCodec.PROFILE)
        val group = f.readLocationRoster(0)
        val binding = assertNotNull(f.session.groupLocationBinding(group))
        val outcomes = mutableListOf<CompanionLocationReply?>()
        val fix = PhoneLocationFix(123456, -420001, 30_000, 10)
        assertTrue(f.session.submitGroupLocation(group, binding, CompanionLocationOperation.START, 0, fix, outcomes::add))
        assertEquals(1, f.immediateWrites)
        val encoded = ByteBuffer.wrap(f.sent.last().payload)
        assertEquals(30_000L, encoded.getLong(28))
        assertEquals(10, encoded.getInt(36))
        assertEquals(123456, encoded.getInt(40))
        assertEquals(-420001, encoded.getInt(44))
        assertTrue(f.session.receive(f.locationReply(2, lease = 7)))
        assertEquals(7uL, outcomes.single()?.lease)

        assertTrue(f.session.submitGroupLocation(group, binding, CompanionLocationOperation.RENEW, 7, null, outcomes::add))
        assertTrue(f.sent.last().payload.copyOfRange(28, 48).all { it == 0.toByte() })
        assertEquals(1, f.immediateWrites)
        assertFalse(f.session.receive(f.locationReply(3, lease = 8)))
        val renewal = f.locationReply(3, lease = 7)
        f.session.queueGroupLocationCleanup(group, binding, CompanionLocationOperation.STOP, 7)
        f.session.queueGroupLocationCleanup(group, binding, CompanionLocationOperation.WITHDRAW)
        assertFalse(f.session.flushLocationCleanup())
        f.session.lost(renewal.exchangeId)
        assertNull(outcomes.last())
        assertFalse(f.session.receive(renewal))
        assertTrue(f.session.flushLocationCleanup())
        assertEquals(5, f.sent.last().payload[1].toInt())
        assertTrue(f.sent.last().payload.copyOfRange(20, 48).all { it == 0.toByte() })
        assertTrue(f.session.receive(f.locationReply(5)))
        assertFalse(f.session.hasLocationCleanup)
        assertFalse(f.session.busy)
    }

    private fun Fixture.readLocationRoster(slot: Int): V1BoardGroupSummary {
        val wire = GroupRosterResponseFixture
        fun receive(payload: ByteArray) {
            assertTrue(session.receive(CompanionConfigurationFrame(
                CompanionGroupConfigurationCodec.RESPONSE_KIND, context.sessionNonce, sent.last().exchangeId, payload, minor,
            )))
        }
        assertTrue(session.submitBoardGroupAction(V1BoardGroupAction.Refresh))
        receive(wire.message(wire.DISCOVER))
        repeat(4) { index ->
            if (index == slot) {
                val roster = wire.rosterBytes()
                for (offset in roster.indices step 42) receive(wire.rosterResult(slot, offset, bytes = roster))
            } else {
                receive(wire.message(wire.READ_ROSTER, wire.MISSING))
            }
        }
        return session.boardGroupState().groups.single()
    }

    private fun Fixture.locationReply(
        operation: Int,
        lease: Long = 0,
        nonce: UInt = context.sessionNonce,
        exchange: UInt = sent.last().exchangeId,
        generation: Long = 1,
    ): CompanionConfigurationFrame {
        val payload = ByteBuffer.allocate(28).put(1).put(operation.toByte()).put(1).put(0)
            .putLong(GroupRosterResponseFixture.GROUP_ID.toLong()).putLong(generation).putLong(lease).array()
        return CompanionConfigurationFrame(CompanionLocationCodec.RESPONSE_KIND, nonce, exchange, payload, minor)
    }

    @Test fun staleTimeoutDoesNotCancelTimeContinuation() {
        val f=Fixture();f.session.synchronizeTime();val old=f.sent.last().exchangeId
        f.session.receive(f.time(CompanionTimePayload(2,challenge=12u)))
        f.session.lost(old);assertTrue(f.session.busy)
        f.session.lost(f.sent.last().exchangeId);assertFalse(f.session.busy)
    }
}
