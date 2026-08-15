package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimOutcome
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimResult
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimState
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimStatus
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationCorrelation
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationDenyReason
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfoCodec
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationPurpose
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationWireCodec
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionFragment
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class BleAuthorizationRuntimeTest {
    @Test
    fun exactAuthorizeFlowPromotesThenRequestsSnapshotOnSameSession() {
        val fixture = Fixture()
        lateinit var lease: DeviceAuthorizationClaimLease
        val events = mutableListOf<DeviceAuthorizationClaimEvent>()
        lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) { event ->
            events += event
            if (event is DeviceAuthorizationClaimEvent.Accepted) {
                lease.close()
                assertTrue(fixture.runtime.authorizationAccepted(CANDIDATE.endpointToken))
            }
        }
        assertTrue(lease.start())
        val gatt = fixture.facade.connections.single()
        fixture.advanceToClaim(gatt)

        val start = CompanionProtocolCodec.decodeFragment(gatt.commands.single()).value!!
        assertEquals(CompanionFrameKind.AUTHORIZATION_CLAIM_START, start.kind)
        assertEquals(SESSION, start.sessionNonce)
        assertEquals(1, start.exchangeId)
        assertContentEquals(
            CompanionAuthorizationWireCodec.encodeClaimStart(
                io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimStart(
                    CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
                ),
            ),
            start.payload,
        )

        gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
        val pending = assertIs<DeviceAuthorizationClaimEvent.Pending>(events.single())
        assertTrue(pending.claimToken.startsWith("wire-claim-"))
        assertFalse(pending.claimToken.contains("a0a1"))

        gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
        assertIs<DeviceAuthorizationClaimEvent.Accepted>(events.last())
        assertFalse(gatt.closed)
        val snapshotRequest = CompanionProtocolCodec.decodeFragment(gatt.commands.last()).value!!
        assertEquals(CompanionFrameKind.SNAPSHOT_REQUEST, snapshotRequest.kind)
        assertEquals(SESSION, snapshotRequest.sessionNonce)
        assertEquals(1, snapshotRequest.exchangeId)
        assertContentEquals(CompanionSemanticCodec.encodeSnapshotRequest(), snapshotRequest.payload)

        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope()))
        assertIs<BleRuntimeState.Ready>(fixture.runtime.state)
        assertFalse(gatt.closed)
        assertTrue(fixture.runtime.submitAction(CompanionActionRequest(CompanionActionKind.QUICK_STATUS)))
        assertEquals(2, CompanionProtocolCodec.decodeFragment(gatt.commands.last()).value!!.exchangeId)
    }

    @Test
    fun deniedAndLocalTimeoutNeverInventAuthorizationOrUnsupported() {
        run {
            val fixture = Fixture()
            val events = mutableListOf<DeviceAuthorizationClaimEvent>()
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE, events::add)
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
            assertFalse(fixture.runtime.submitAction(CompanionActionRequest(CompanionActionKind.QUICK_STATUS)))
            gatt.emit(
                BleGattEvent.StreamIndication(
                    resultEnvelope(
                        CompanionAuthorizationClaimOutcome.DENIED,
                        CompanionAuthorizationDenyReason.POLICY_DENIED,
                    ),
                ),
            )
            assertIs<DeviceAuthorizationClaimEvent.Denied>(events.last())
            assertIs<BleRuntimeState.Idle>(fixture.runtime.state)
            assertTrue(gatt.closed)
            assertFalse(fixture.runtime.submitAction(io.github.nbjelanovic.otprotocol.CompanionActionRequest(
                io.github.nbjelanovic.otprotocol.CompanionActionKind.QUICK_STATUS,
            )))
        }

        run {
            val fixture = Fixture()
            val events = mutableListOf<DeviceAuthorizationClaimEvent>()
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE, events::add)
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            assertEquals(NEGOTIATION_STEP_TIMEOUT_MILLIS, fixture.scheduler.nextOpen().delayMillis)
            gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
            assertEquals(AUTHORIZATION_RESULT_TIMEOUT_MILLIS, fixture.scheduler.nextOpen().delayMillis)
            fixture.scheduler.runNext()
            assertEquals(1, events.size)
            assertIs<DeviceAuthorizationClaimEvent.Pending>(events.single())
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state)
            assertTrue(gatt.closed)
        }
    }

    @Test
    fun staleMalformedAndOutOfOrderResultsCannotPromoteOrMutate() {
        val fixture = Fixture()
        val events = mutableListOf<DeviceAuthorizationClaimEvent>()
        val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE, events::add)
        assertTrue(lease.start())
        val gatt = fixture.facade.connections.single()
        fixture.advanceToClaim(gatt)
        gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
        assertTrue(events.isEmpty())
        assertEquals(
            BleRuntimeFailure.PROTOCOL_VIOLATION,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
        assertTrue(gatt.closed)
    }

    @Test
    fun provisionalPathRequiresBondEncryptionAndExactClaimCapability() {
        run {
            val fixture = Fixture()
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) {}
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            gatt.emit(BleGattEvent.SecurityEstablished(BleSecurityEvidence(true, false, false)))
            assertEquals(
                BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
            assertEquals(0, gatt.protocolInfoReads)
            assertEquals(1, gatt.closeCount)
        }

        run {
            val fixture = Fixture()
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) {}
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            gatt.emit(BleGattEvent.SecurityEstablished(BleSecurityEvidence(true, true, false)))
            gatt.emit(
                BleGattEvent.ProtocolInfoRead(
                    io.github.nbjelanovic.otprotocol.CompanionProtocolCodec.encodeProtocolInfo(
                        io.github.nbjelanovic.otprotocol.CompanionProtocolInfo(capabilities = 0x0f),
                    ).value!!,
                ),
            )
            assertEquals(
                BleRuntimeFailure.PROTOCOL_INFO_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
            assertTrue(gatt.requestedMtus.isEmpty())
            assertEquals(1, gatt.closeCount)
        }
    }

    @Test
    fun postPromotionSnapshotMustMatchExactProvisionalSession() {
        val fixture = Fixture()
        lateinit var lease: DeviceAuthorizationClaimLease
        lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) { event ->
            if (event is DeviceAuthorizationClaimEvent.Accepted) {
                lease.close()
                assertTrue(fixture.runtime.authorizationAccepted(CANDIDATE.endpointToken))
            }
        }
        assertTrue(lease.start())
        val gatt = fixture.facade.connections.single()
        fixture.advanceToClaim(gatt)
        gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
        gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce = SESSION + 1)))
        assertEquals(
            BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
        assertEquals(1, gatt.closeCount)
        assertFalse(fixture.runtime.submitAction(CompanionActionRequest(CompanionActionKind.QUICK_STATUS)))
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope()))
        assertIs<BleRuntimeState.Failed>(fixture.runtime.state)
    }

    @Test
    fun lifecycleStopDuringPendingOrTerminalClosesExactGattAndSuppressesContinuation() {
        run {
            val fixture = Fixture()
            val events = mutableListOf<DeviceAuthorizationClaimEvent>()
            lateinit var gatt: TestGatt
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) { event ->
                events += event
                if (event is DeviceAuthorizationClaimEvent.Pending) {
                    fixture.runtime.onLifecycleStop()
                    gatt.emit(
                        BleGattEvent.StreamIndication(
                            resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED),
                        ),
                    )
                }
            }
            assertTrue(lease.start())
            gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
            assertIs<BleRuntimeState.Inactive>(fixture.runtime.state)
            assertEquals(1, gatt.closeCount)
            gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
            assertEquals(1, gatt.closeCount)
            assertEquals(1, events.size)
        }

        run {
            val fixture = Fixture()
            lateinit var gatt: TestGatt
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) { event ->
                if (event is DeviceAuthorizationClaimEvent.Pending) {
                    fixture.runtime.close()
                    gatt.emit(
                        BleGattEvent.StreamIndication(
                            resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED),
                        ),
                    )
                }
            }
            assertTrue(lease.start())
            gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
            assertIs<BleRuntimeState.Closed>(fixture.runtime.state)
            assertEquals(1, gatt.closeCount)
        }

        run {
            val fixture = Fixture()
            lateinit var lease: DeviceAuthorizationClaimLease
            lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) { event ->
                if (event is DeviceAuthorizationClaimEvent.Accepted) {
                    lease.close()
                    assertTrue(fixture.runtime.authorizationAccepted(CANDIDATE.endpointToken))
                    fixture.runtime.onLifecycleStop()
                }
            }
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
            gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
            assertIs<BleRuntimeState.Inactive>(fixture.runtime.state)
            assertEquals(1, gatt.closeCount)
            assertEquals(1, gatt.commands.size)
        }
    }

    @Test
    fun synchronousTerminalReentryFromPendingObserverFailsClosed() {
        val fixture = Fixture()
        lateinit var gatt: TestGatt
        val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) { event ->
            if (event is DeviceAuthorizationClaimEvent.Pending) {
                gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
            }
        }
        assertTrue(lease.start())
        gatt = fixture.facade.connections.single()
        fixture.advanceToClaim(gatt)
        gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
        assertEquals(
            BleRuntimeFailure.PROTOCOL_VIOLATION,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
        assertEquals(1, gatt.closeCount)
        assertEquals(1, gatt.commands.size)
    }

    @Test
    fun snapshotRequestCounterExhaustionFailsBeforeNormalWrite() {
        val fixture = Fixture(firstRequestId = 0xffff_ffffL)
        val events = mutableListOf<DeviceAuthorizationClaimEvent>()
        val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE, events::add)
        assertTrue(lease.start())
        val gatt = fixture.facade.connections.single()
        fixture.advanceToClaim(gatt)
        gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope(exchangeId = 0xffff_ffffL)))
        gatt.emit(
            BleGattEvent.StreamIndication(
                resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED, exchangeId = 0xffff_ffffL),
            ),
        )
        assertIs<DeviceAuthorizationClaimEvent.Accepted>(events.last())
        assertEquals(
            BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
        assertEquals(1, gatt.commands.size)
        assertEquals(1, gatt.closeCount)
    }

    @Test
    fun claimDisconnectNeverReconnectsOrReissuesAuthorizeOrReplace() {
        run {
            val fixture = Fixture()
            val events = mutableListOf<DeviceAuthorizationClaimEvent>()
            val lease = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE, events::add)
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            gatt.emit(BleGattEvent.StreamIndication(pendingEnvelope()))
            gatt.emit(BleGattEvent.Disconnected)
            assertEquals(
                BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
            assertEquals(1, fixture.facade.connections.size)
            assertEquals(1, gatt.commands.size)
            assertEquals(1, gatt.closeCount)
            assertFalse(fixture.scheduler.hasOpenTimers())

            fixture.runtime.requestScan()
            fixture.facade.scan!!.emit(BleScanEvent.Candidate(CANDIDATE))
            assertEquals(CANDIDATE, fixture.runtime.beginAuthorization(CANDIDATE.endpointToken))
            val retry = fixture.createClaim(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE, events::add)
            assertTrue(retry.start())
            val freshGatt = fixture.facade.connections.last()
            assertEquals(2, fixture.facade.connections.size)
            gatt.emit(BleGattEvent.StreamIndication(resultEnvelope(CompanionAuthorizationClaimOutcome.ACCEPTED)))
            assertIs<BleRuntimeState.Connecting>(fixture.runtime.state)
            assertFalse(freshGatt.closed)
        }

        run {
            val fixture = Fixture()
            lateinit var lease: DeviceAuthorizationClaimLease
            lease = fixture.createClaim(DeviceAuthorizationPurpose.REPLACE_LOST_PHONE) { event ->
                if (event is DeviceAuthorizationClaimEvent.Replaced) {
                    lease.close()
                    assertTrue(fixture.runtime.authorizationAccepted(CANDIDATE.endpointToken))
                }
            }
            assertTrue(lease.start())
            val gatt = fixture.facade.connections.single()
            fixture.advanceToClaim(gatt)
            val start = CompanionProtocolCodec.decodeFragment(gatt.commands.single()).value!!
            assertEquals(
                CompanionAuthorizationPurpose.REPLACE_CONTROLLER,
                CompanionAuthorizationWireCodec.decodeClaimStart(start.payload).value!!.purpose,
            )
            gatt.emit(
                BleGattEvent.StreamIndication(
                    pendingEnvelope(CompanionAuthorizationPurpose.REPLACE_CONTROLLER),
                ),
            )
            gatt.emit(
                BleGattEvent.StreamIndication(
                    resultEnvelope(
                        CompanionAuthorizationClaimOutcome.REPLACED,
                        purpose = CompanionAuthorizationPurpose.REPLACE_CONTROLLER,
                    ),
                ),
            )
            assertEquals(2, gatt.commands.size)
            gatt.emit(BleGattEvent.Disconnected)
            assertEquals(
                BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
            assertEquals(1, fixture.facade.connections.size)
            assertEquals(2, gatt.commands.size)
            assertEquals(1, gatt.closeCount)
            assertFalse(fixture.scheduler.hasOpenTimers())
        }
    }

    private class Fixture(firstRequestId: Long = 1) {
        val facade = TestFacade()
        val scheduler = TestScheduler()
        val runtime = BleCompanionRuntime(facade, scheduler, firstRequestId = firstRequestId)

        init {
            runtime.onLifecycleStart()
            runtime.requestScan()
            facade.scan!!.emit(BleScanEvent.Candidate(CANDIDATE))
            assertEquals(CANDIDATE, runtime.beginAuthorization(CANDIDATE.endpointToken))
        }

        fun createClaim(
            purpose: DeviceAuthorizationPurpose,
            observer: (DeviceAuthorizationClaimEvent) -> Unit,
        ): DeviceAuthorizationClaimLease = checkNotNull(
            RuntimeDeviceAuthorizationClaimClient(runtime).createClaim(CANDIDATE.endpointToken, purpose, observer),
        )

        fun advanceToClaim(gatt: TestGatt) {
            gatt.emit(BleGattEvent.SecurityEstablished(BleSecurityEvidence(true, true, false)))
            assertEquals(1, gatt.protocolInfoReads)
            assertTrue(gatt.requestedMtus.isEmpty())
            gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfo()))
            assertEquals(listOf(151), gatt.requestedMtus)
            gatt.emit(BleGattEvent.MtuChanged(151))
            assertEquals(1, gatt.subscriptions)
            gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
            assertEquals(1, gatt.commands.size)
            assertEquals(BleNegotiationPhase.AUTHORIZATION_CLAIM, assertIs<BleRuntimeState.Negotiating>(runtime.state).phase)
        }
    }

    private class TestFacade : AndroidBluetoothFacade {
        var scan: TestScan? = null
        val connections = mutableListOf<TestGatt>()
        override fun preflight() = BlePreflight()
        override fun createScan(observer: (BleScanEvent) -> Unit) = TestScan(observer).also { scan = it }
        override fun createConnection(endpointToken: String, observer: (BleGattEvent) -> Unit) =
            TestGatt(observer).also(connections::add)
    }

    private class TestScan(private val observer: (BleScanEvent) -> Unit) : BleScanLease {
        override fun start() = true
        override fun close() = Unit
        fun emit(event: BleScanEvent) = observer(event)
    }

    private class TestGatt(private val observer: (BleGattEvent) -> Unit) : BleGattLease {
        var closeCount = 0
        val requestedMtus = mutableListOf<Int>()
        var protocolInfoReads = 0
        var subscriptions = 0
        val commands = mutableListOf<ByteArray>()
        val closed: Boolean get() = closeCount > 0
        override fun start() = true
        override fun requestMtu(mtu: Int) = true.also { requestedMtus += mtu }
        override fun readProtocolInfo() = true.also { protocolInfoReads += 1 }
        override fun subscribeStreamIndications() = true.also { subscriptions += 1 }
        override fun writeCommandWithResponse(value: ByteArray) = true.also { commands += value.copyOf() }
        override fun close() { closeCount += 1 }
        fun emit(event: BleGattEvent) = observer(event)
    }

    private class TestScheduler : BleRuntimeScheduler {
        val timers = mutableListOf<TestTimer>()
        override fun schedule(delayMillis: Long, callback: () -> Unit) =
            TestTimer(delayMillis, callback).also(timers::add)
        fun nextOpen() = timers.first { !it.closed && !it.ran }
        fun runNext() = nextOpen().run()
        fun hasOpenTimers() = timers.any { !it.closed && !it.ran }
    }

    private class TestTimer(val delayMillis: Long, private val callback: () -> Unit) : BleReconnectLease {
        var closed = false
        var ran = false
        override fun close() { closed = true }
        fun run() {
            if (closed || ran) return
            ran = true
            callback()
        }
    }

    companion object {
        private val CANDIDATE = BleDiscoveredCompanion("opaque-endpoint", "Candidate")
        private const val SESSION = 0x11223344L
        private const val EXCHANGE = 1L
        private val CORRELATION = CompanionAuthorizationCorrelation(ByteArray(16) { (0xa0 + it).toByte() })

        private fun protocolInfo() = CompanionAuthorizationProtocolInfoCodec.encode(
            CompanionAuthorizationProtocolInfo(provisionalSessionNonce = SESSION),
        ).value!!

        private fun pendingEnvelope(
            purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
            exchangeId: Long = EXCHANGE,
        ) = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS,
                SESSION,
                exchangeId,
                payload = CompanionAuthorizationWireCodec.encodeClaimStatus(
                    CompanionAuthorizationClaimStatus(
                        purpose,
                        CompanionAuthorizationClaimState.PENDING,
                        CORRELATION,
                    ),
                ).value!!,
            ),
        ).value!!

        private fun resultEnvelope(
            outcome: CompanionAuthorizationClaimOutcome,
            reason: CompanionAuthorizationDenyReason = CompanionAuthorizationDenyReason.NONE,
            exchangeId: Long = EXCHANGE,
            purpose: CompanionAuthorizationPurpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
        ) = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT,
                SESSION,
                exchangeId,
                payload = CompanionAuthorizationWireCodec.encodeClaimResult(
                    CompanionAuthorizationClaimResult(
                        purpose,
                        outcome,
                        reason,
                        CORRELATION,
                    ),
                ).value!!,
            ),
        ).value!!

        private fun snapshotEnvelope(sessionNonce: Long = SESSION) = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                CompanionFrameKind.SNAPSHOT,
                sessionNonce,
                EXCHANGE,
                payload = CompanionSemanticCodec.encodeStatusSnapshot(
                    CompanionStatusSnapshot(
                        revision = 1,
                        radio = CompanionRadioState.READY,
                        gnss = CompanionGnssState.SEARCHING,
                        power = CompanionPowerState.NORMAL,
                        positionSharing = CompanionPositionSharingState.STOPPED,
                        queuedActionCount = 0,
                    ),
                ).value!!,
            ),
        ).value!!
    }
}
