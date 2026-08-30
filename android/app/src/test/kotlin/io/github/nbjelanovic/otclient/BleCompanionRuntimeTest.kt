package io.github.nbjelanovic.otclient

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import io.github.nbjelanovic.otprotocol.COMPANION_MINIMUM_ATT_MTU
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionFragment
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class BleCompanionRuntimeTest {
    @Test
    fun gattContractIdentifiersAreExactAndBrandNeutral() {
        assertEquals("5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.SERVICE_UUID)
        assertEquals("5e0f2a01-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.PROTOCOL_INFO_UUID)
        assertEquals("5e0f2a02-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.COMMAND_UUID)
        assertEquals("5e0f2a03-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.STREAM_UUID)
        assertTrue(
            listOf(
                CompanionGattV0Contract.SERVICE_UUID,
                CompanionGattV0Contract.PROTOCOL_INFO_UUID,
                CompanionGattV0Contract.COMMAND_UUID,
                CompanionGattV0Contract.STREAM_UUID,
            ).none { it.contains("trail", ignoreCase = true) || it.contains("limited", ignoreCase = true) },
        )
    }

    @Test
    fun productionDefaultIsBlockedBeforeScanAndCannotCreateAuthority() {
        val runtime = BleCompanionRuntime(DisabledAndroidBluetoothFacade(), TestRuntimeScheduler())
        runtime.onLifecycleStart()
        runtime.requestScan()

        assertEquals(
            BleRuntimeBlock.ADAPTER_IMPLEMENTATION_MISSING,
            assertIs<BleRuntimeState.Blocked>(runtime.state).reason,
        )
        assertFalse(runtime.submitAction(quickStatus()))
    }

    @Test
    fun scanIsExplicitBoundedAndLifecycleStopReleasesAndInvalidatesIt() {
        val facade = TestBluetoothFacade()
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
        runtime.onLifecycleStart()
        runtime.requestScan()
        val scan = facade.scans.single()
        assertTrue(scan.started)

        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        scan.emit(BleScanEvent.Candidate(CANDIDATE.copy(publicLabel = "Updated public label")))
        scan.emit(BleScanEvent.Candidate(BleDiscoveredCompanion("x".repeat(MAX_ENDPOINT_TOKEN_CHARS + 1), "Too long")))
        scan.emit(BleScanEvent.Candidate(BleDiscoveredCompanion("valid-token", "x".repeat(MAX_PUBLIC_LABEL_CHARS + 1))))
        repeat(MAX_DISCOVERED_COMPANIONS + 2) { index ->
            scan.emit(BleScanEvent.Candidate(BleDiscoveredCompanion("candidate-$index", "Candidate $index")))
        }
        val candidates = assertIs<BleRuntimeState.Scanning>(runtime.state).candidates
        assertEquals(MAX_DISCOVERED_COMPANIONS, candidates.size)
        assertEquals("Updated public label", candidates.single { it.endpointToken == CANDIDATE.endpointToken }.publicLabel)

        runtime.onLifecycleStop()
        assertTrue(scan.closed)
        assertIs<BleRuntimeState.Inactive>(runtime.state)
        scan.emit(BleScanEvent.Failed(BleGattFailure.PLATFORM_FAILURE))
        assertIs<BleRuntimeState.Inactive>(runtime.state)
    }

    @Test
    fun selectionMustMatchCurrentScanAndNegotiationRunsInAcceptedOrder() {
        val fixture = Fixture()
        fixture.startScanWithCandidate()
        assertEquals(null, fixture.runtime.beginAuthorization("not-discovered"))
        assertEquals(0, fixture.facade.connections.size)

        val gatt = fixture.selectAndGatt()
        assertIs<BleRuntimeState.Connecting>(fixture.runtime.state)
        gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
        assertEquals(listOf(COMPANION_MINIMUM_ATT_MTU), gatt.requestedMtus)
        assertPhase(fixture.runtime, BleNegotiationPhase.ATT_MTU)

        gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU))
        assertEquals(1, gatt.protocolInfoReads)
        assertPhase(fixture.runtime, BleNegotiationPhase.PROTOCOL_INFO)

        gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes()))
        assertEquals(1, gatt.streamSubscriptions)
        assertPhase(fixture.runtime, BleNegotiationPhase.STREAM_SUBSCRIPTION)

        gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
        assertPhase(fixture.runtime, BleNegotiationPhase.INITIAL_SNAPSHOT)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce = 7, eventId = 11)))

        val ready = assertIs<BleRuntimeState.Ready>(fixture.runtime.state)
        assertEquals(7, ready.session.sessionNonce)
        assertEquals(9, ready.session.snapshot.revision)
        assertEquals(CANDIDATE, ready.session.companion)
        assertEquals(GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT, ready.session.groupLocation.provenance)
        assertEquals(GroupPositionState.UNAVAILABLE, ready.session.groupLocation.selfPosition.state)
        assertTrue(ready.session.groupLocation.peers.isEmpty())
    }

    @Test
    fun incompleteSecurityLowMtuAndOutOfOrderCallbacksFailClosed() {
        run {
            val fixture = Fixture()
            val gatt = fixture.connectGatt()
            gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK.copy(applicationAuthorized = false)))
            assertEquals(
                BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
            assertTrue(gatt.closed)
        }
        run {
            val fixture = Fixture()
            val gatt = fixture.connectGatt()
            gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
            gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU - 1))
            assertEquals(
                BleRuntimeFailure.MTU_NEGOTIATION_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
        }
        run {
            val fixture = Fixture()
            val gatt = fixture.connectGatt()
            gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes()))
            assertEquals(
                BleRuntimeFailure.PROTOCOL_VIOLATION,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
        }
    }

    @Test
    fun incompatibleProtocolInfoAndMalformedInitialSnapshotFailClosed() {
        run {
            val fixture = Fixture()
            val gatt = fixture.connectGatt()
            fixture.advanceToProtocolInfo(gatt)
            gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes(capabilities = 0x03)))
            assertEquals(
                BleRuntimeFailure.PROTOCOL_INFO_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
        }
        run {
            val fixture = Fixture()
            val gatt = fixture.connectGatt()
            fixture.advanceToProtocolInfo(gatt)
            gatt.emit(
                BleGattEvent.ProtocolInfoRead(
                    protocolInfoBytes(maxFragmentPayloadBytes = 31),
                ),
            )
            assertEquals(
                BleRuntimeFailure.PROTOCOL_INFO_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
        }
        run {
            val fixture = Fixture()
            val gatt = fixture.connectGatt()
            fixture.advanceToInitialSnapshot(gatt)
            gatt.emit(BleGattEvent.StreamIndication(byteArrayOf(1, 2, 3)))
            assertEquals(
                BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
        }
    }

    @Test
    fun actionWriteUsesExactSessionAndResultCorrelationWithoutDeliveryClaim() {
        val fixture = Fixture()
        val gatt = fixture.readyGatt(sessionNonce = 17)
        assertFalse(
            fixture.runtime.submitAction(
                CompanionActionRequest(CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT),
            ),
        )
        assertIs<BleRuntimeState.Ready>(fixture.runtime.state)
        assertFalse(gatt.closed)
        assertTrue(gatt.commands.isEmpty())
        val request = quickStatus(CompanionQuickStatus.NEED_ASSISTANCE)
        assertTrue(fixture.runtime.submitAction(request))
        assertFalse(fixture.runtime.submitAction(quickStatus()))

        val command = CompanionProtocolCodec.decodeFragment(gatt.commands.single()).value!!
        assertEquals(CompanionFrameKind.ACTION_REQUEST, command.kind)
        assertEquals(17, command.sessionNonce)
        assertEquals(1, command.exchangeId)
        assertEquals(request, CompanionSemanticCodec.decodeActionRequest(command.payload).value)

        val result = CompanionActionResult(
            kind = CompanionActionKind.QUICK_STATUS,
            quickStatus = CompanionQuickStatus.NEED_ASSISTANCE,
            disposition = CompanionActionDisposition.QUEUED,
        )
        gatt.emit(BleGattEvent.StreamIndication(actionResultEnvelope(17, 1, result)))
        val ready = assertIs<BleRuntimeState.Ready>(fixture.runtime.state)
        assertEquals(CompanionActionDisposition.QUEUED, ready.session.lastActionResult?.disposition)
    }

    @Test
    fun wrongSessionOrActionEchoClosesLeaseWithoutPublishingResult() {
        val fixture = Fixture()
        val gatt = fixture.readyGatt(sessionNonce = 17)
        assertTrue(fixture.runtime.submitAction(quickStatus(CompanionQuickStatus.OK)))
        val wrong = CompanionActionResult(
            kind = CompanionActionKind.QUICK_STATUS,
            quickStatus = CompanionQuickStatus.AVAILABLE_TO_HELP,
            disposition = CompanionActionDisposition.QUEUED,
        )
        gatt.emit(BleGattEvent.StreamIndication(actionResultEnvelope(18, 1, wrong)))

        assertEquals(
            BleRuntimeFailure.PROTOCOL_VIOLATION,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
        assertTrue(gatt.closed)
    }

    @Test
    fun snapshotsRequireSameSessionAndIncreasingDeviceEventIdentity() {
        val fixture = Fixture()
        val gatt = fixture.readyGatt(sessionNonce = 21, initialEventId = 5)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(21, 6, snapshot(revision = 10))))
        val updated = assertIs<BleRuntimeState.Ready>(fixture.runtime.state).session
        assertEquals(10, updated.snapshot.revision)
        assertEquals(10, updated.groupLocation.sourceSnapshotRevision)
        assertEquals(GroupPositionState.UNAVAILABLE, updated.groupLocation.selfPosition.state)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(21, 6, snapshot(revision = 11))))
        assertEquals(
            BleRuntimeFailure.PROTOCOL_VIOLATION,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
    }

    @Test
    fun reconnectIsBoundedAcceptsBootLocalNonceResetAndIgnoresReleasedLeaseCallbacks() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(scheduler = scheduler, maximumReconnectAttempts = 2)
        val first = fixture.readyGatt(sessionNonce = 31)
        first.emit(BleGattEvent.Disconnected)
        assertEquals(1, assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).attempt)
        assertTrue(first.closed)

        scheduler.runNext()
        val second = fixture.facade.connections.last()
        first.emit(BleGattEvent.StreamIndication(snapshotEnvelope(99, 99)))
        assertTrue(fixture.runtime.state is BleRuntimeState.Reconnecting)
        fixture.advanceToInitialSnapshot(second)
        second.emit(BleGattEvent.StreamIndication(snapshotEnvelope(31, 12)))
        assertEquals(31, assertIs<BleRuntimeState.Ready>(fixture.runtime.state).session.sessionNonce)

        // The phone cannot distinguish a device reboot from a same-valued boot-local nonce.
        second.emit(BleGattEvent.Disconnected)
        scheduler.runNext()
        val third = fixture.facade.connections.last()
        third.emit(BleGattEvent.Failed(BleGattFailure.TRANSIENT_LINK))
        scheduler.runNext()
        val fourth = fixture.facade.connections.last()
        fourth.emit(BleGattEvent.Failed(BleGattFailure.TRANSIENT_LINK))
        assertEquals(
            BleRuntimeFailure.RECONNECT_EXHAUSTED,
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
        )
    }

    @Test
    fun lifecycleStopCancelsReconnectAndStartReopensRememberedSelection() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(scheduler = scheduler)
        val first = fixture.readyGatt(sessionNonce = 40)
        first.emit(BleGattEvent.Disconnected)
        val scheduled = scheduler.nextOpen()
        fixture.runtime.onLifecycleStop()
        assertTrue(scheduled.closed)
        assertIs<BleRuntimeState.Inactive>(fixture.runtime.state)
        scheduled.run()
        assertEquals(1, fixture.facade.connections.size)

        fixture.runtime.onLifecycleStart()
        assertEquals(2, fixture.facade.connections.size)
        assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state)
        fixture.runtime.close()
        assertTrue(fixture.facade.connections.last().closed)
        assertIs<BleRuntimeState.Closed>(fixture.runtime.state)
    }

    @Test
    fun requestIdExhaustionNeverWrapsOrWritesSecondAction() {
        val fixture = Fixture(firstRequestId = 0xffff_ffffL)
        val gatt = fixture.readyGatt(sessionNonce = 51)
        val request = quickStatus()
        assertTrue(fixture.runtime.submitAction(request))
        val result = CompanionActionResult(
            kind = CompanionActionKind.QUICK_STATUS,
            quickStatus = CompanionQuickStatus.OK,
            disposition = CompanionActionDisposition.QUEUED,
        )
        gatt.emit(BleGattEvent.StreamIndication(actionResultEnvelope(51, 0xffff_ffffL, result)))
        assertFalse(fixture.runtime.submitAction(request))
        assertEquals(1, gatt.commands.size)
        assertEquals(1, assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).attempt)
    }

    @Test
    fun negotiationAndActionResultWaitsOwnCancellableBoundedTimeouts() {
        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            fixture.connectGatt()
            val timeout = scheduler.nextOpen()
            assertEquals(NEGOTIATION_STEP_TIMEOUT_MILLIS, timeout.delayMillis)
            timeout.run()
            assertEquals(
                BleRuntimeFailure.NEGOTIATION_TIMEOUT,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason,
            )
        }
        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            val gatt = fixture.readyGatt(sessionNonce = 52)
            assertTrue(fixture.runtime.submitAction(quickStatus()))
            val timeout = scheduler.nextOpen()
            assertEquals(ACTION_RESULT_TIMEOUT_MILLIS, timeout.delayMillis)
            timeout.run()
            assertEquals(1, assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).attempt)
            assertTrue(gatt.closed)
        }
    }

    @Test
    fun phaseAdvanceValidResultAndLifecycleStopCancelAndSuppressTheirTimers() {
        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            val gatt = fixture.connectGatt()
            val securityTimeout = scheduler.nextOpen()
            gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
            assertTrue(securityTimeout.closed)
            securityTimeout.run()
            assertPhase(fixture.runtime, BleNegotiationPhase.ATT_MTU)

            val mtuTimeout = scheduler.nextOpen()
            fixture.runtime.onLifecycleStop()
            assertTrue(mtuTimeout.closed)
            mtuTimeout.run()
            assertIs<BleRuntimeState.Inactive>(fixture.runtime.state)
        }
        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            val gatt = fixture.readyGatt(sessionNonce = 53)
            assertTrue(fixture.runtime.submitAction(quickStatus()))
            val actionTimeout = scheduler.nextOpen()
            val result = CompanionActionResult(
                kind = CompanionActionKind.QUICK_STATUS,
                quickStatus = CompanionQuickStatus.OK,
                disposition = CompanionActionDisposition.QUEUED,
            )
            gatt.emit(BleGattEvent.StreamIndication(actionResultEnvelope(53, 1, result)))
            assertTrue(actionTimeout.closed)
            val accepted = fixture.runtime.state
            actionTimeout.run()
            assertEquals(accepted, fixture.runtime.state)
        }
    }

    @Test
    fun observerCannotReenterAndOrphanScanOrGattLeasesDuringPublication() {
        val facade = TestBluetoothFacade()
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
        runtime.onLifecycleStart()
        var scanCancellationAttempted = false
        runtime.observe { state ->
            if (state is BleRuntimeState.Scanning && !scanCancellationAttempted) {
                scanCancellationAttempted = true
                runtime.disconnect()
                runtime.close()
                facade.scans.single().emit(
                    BleScanEvent.Candidate(BleDiscoveredCompanion("reentrant", "Reentrant")),
                )
            }
        }
        runtime.requestScan()
        val scan = facade.scans.single()
        assertTrue(scanCancellationAttempted)
        assertFalse(scan.started)
        assertTrue(scan.closed)
        assertIs<BleRuntimeState.Closed>(runtime.state)

        val connectionFacade = TestBluetoothFacade()
        val connectionRuntime = BleCompanionRuntime(connectionFacade, TestRuntimeScheduler())
        connectionRuntime.onLifecycleStart()
        connectionRuntime.requestScan()
        val connectionScan = connectionFacade.scans.single()
        connectionScan.emit(BleScanEvent.Candidate(CANDIDATE))
        var connectionCancellationAttempted = false
        connectionRuntime.observe { state ->
            if (state is BleRuntimeState.Connecting && !connectionCancellationAttempted) {
                connectionCancellationAttempted = true
                connectionRuntime.disconnect()
                connectionRuntime.close()
            }
        }
        assertEquals(CANDIDATE, connectionRuntime.beginAuthorization(CANDIDATE.endpointToken))
        assertTrue(connectionRuntime.authorizationAccepted(CANDIDATE.endpointToken))
        val gatt = connectionFacade.connections.single()
        assertTrue(connectionCancellationAttempted)
        assertFalse(gatt.started)
        assertTrue(gatt.closed)
        assertIs<BleRuntimeState.Closed>(connectionRuntime.state)
    }

    @Test
    fun AndroidLifecycleBindingStartsStopsAndDestroysRuntime() {
        val facade = TestBluetoothFacade()
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
        val owner = TestLifecycleOwner()
        BleCompanionLifecycleBinding(owner.lifecycle, runtime)

        owner.registry.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)
        owner.registry.handleLifecycleEvent(Lifecycle.Event.ON_START)
        assertIs<BleRuntimeState.Idle>(runtime.state)
        runtime.requestScan()
        val scan = facade.scans.single()
        owner.registry.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
        assertTrue(scan.closed)
        assertIs<BleRuntimeState.Inactive>(runtime.state)
        owner.registry.handleLifecycleEvent(Lifecycle.Event.ON_DESTROY)
        assertIs<BleRuntimeState.Closed>(runtime.state)
    }

    @Test
    fun facadeCallbacksMustRunOnTheCreationThreadAndCannotAdvanceStateOtherwise() {
        val facade = TestBluetoothFacade()
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
        runtime.onLifecycleStart()
        runtime.requestScan()
        val scan = facade.scans.single()
        var callbackFailure: Throwable? = null
        Thread {
            try {
                scan.emit(BleScanEvent.Candidate(CANDIDATE))
            } catch (failure: Throwable) {
                callbackFailure = failure
            }
        }.also { thread ->
            thread.start()
            thread.join()
        }

        assertIs<IllegalStateException>(callbackFailure)
        assertTrue(assertIs<BleRuntimeState.Scanning>(runtime.state).candidates.isEmpty())
        runtime.onLifecycleStop()
        assertTrue(scan.closed)
    }

    @Test
    fun synchronousTerminalBondStartFailureCannotScheduleAutomaticRetry() {
        val cases = listOf(
            BleGattFailure.BOND_REQUIRED to BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
            BleGattFailure.PERMISSION_REVOKED to BleRuntimeFailure.CONNECTION_START_FAILED,
        )
        for ((gattFailure, runtimeFailure) in cases) {
            val scheduler = TestRuntimeScheduler()
            val facade = TestBluetoothFacade(
                gattStartEvent = BleGattEvent.Failed(gattFailure),
                gattStartResult = false,
            )
            val runtime = BleCompanionRuntime(facade, scheduler)
            runtime.onLifecycleStart()
            runtime.requestScan()
            facade.scans.single().emit(BleScanEvent.Candidate(CANDIDATE))
            assertEquals(CANDIDATE, runtime.beginAuthorization(CANDIDATE.endpointToken))
            assertTrue(runtime.authorizationAccepted(CANDIDATE.endpointToken))

            assertEquals(runtimeFailure, assertIs<BleRuntimeState.Failed>(runtime.state).reason)
            assertEquals(1, facade.connections.size)
            assertTrue(facade.connections.single().closed)
            assertTrue(scheduler.pending.none { !it.closed && !it.ran })
        }
    }

    private class Fixture(
        val facade: TestBluetoothFacade = TestBluetoothFacade(),
        scheduler: TestRuntimeScheduler = TestRuntimeScheduler(),
        maximumReconnectAttempts: Int = DEFAULT_MAX_RECONNECT_ATTEMPTS,
        firstRequestId: Long = 1,
    ) {
        val runtime = BleCompanionRuntime(facade, scheduler, maximumReconnectAttempts, firstRequestId)

        init {
            runtime.onLifecycleStart()
        }

        fun startScanWithCandidate() {
            runtime.requestScan()
            facade.scans.last().emit(BleScanEvent.Candidate(CANDIDATE))
        }

        fun selectAndGatt(): TestGattLease {
            assertEquals(CANDIDATE, runtime.beginAuthorization(CANDIDATE.endpointToken))
            assertTrue(runtime.authorizationAccepted(CANDIDATE.endpointToken))
            return facade.connections.last()
        }

        fun connectGatt(): TestGattLease {
            startScanWithCandidate()
            return selectAndGatt()
        }

        fun advanceToProtocolInfo(gatt: TestGattLease) {
            gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
            gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU))
        }

        fun advanceToInitialSnapshot(gatt: TestGattLease) {
            advanceToProtocolInfo(gatt)
            gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes()))
            gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
        }

        fun readyGatt(sessionNonce: Long, initialEventId: Long = 1): TestGattLease {
            val gatt = connectGatt()
            advanceToInitialSnapshot(gatt)
            gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce, initialEventId)))
            assertIs<BleRuntimeState.Ready>(runtime.state)
            return gatt
        }
    }

    private class TestBluetoothFacade(
        private val gattStartEvent: BleGattEvent? = null,
        private val gattStartResult: Boolean = true,
    ) : AndroidBluetoothFacade {
        var preflight = BlePreflight()
        val scans = mutableListOf<TestScanLease>()
        val connections = mutableListOf<TestGattLease>()

        override fun preflight(): BlePreflight = preflight

        override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease =
            TestScanLease(observer).also(scans::add)

        override fun createConnection(endpointToken: String, observer: (BleGattEvent) -> Unit): BleGattLease =
            TestGattLease(endpointToken, observer, gattStartEvent, gattStartResult).also(connections::add)
    }

    private class TestScanLease(private val observer: (BleScanEvent) -> Unit) : BleScanLease {
        var started = false
        var closed = false
        override fun start(): Boolean = true.also { started = true }
        override fun close() { closed = true }
        fun emit(event: BleScanEvent) = observer(event)
    }

    private class TestGattLease(
        val endpointToken: String,
        private val observer: (BleGattEvent) -> Unit,
        private val startEvent: BleGattEvent? = null,
        private val startResult: Boolean = true,
    ) : BleGattLease {
        var started = false
        var closed = false
        val requestedMtus = mutableListOf<Int>()
        var protocolInfoReads = 0
        var streamSubscriptions = 0
        val commands = mutableListOf<ByteArray>()

        override fun start(): Boolean {
            started = true
            startEvent?.let(observer)
            return startResult
        }
        override fun requestMtu(mtu: Int): Boolean = true.also { requestedMtus += mtu }
        override fun readProtocolInfo(): Boolean = true.also { protocolInfoReads += 1 }
        override fun subscribeStreamIndications(): Boolean = true.also { streamSubscriptions += 1 }
        override fun writeCommandWithResponse(value: ByteArray): Boolean = true.also { commands += value.copyOf() }
        override fun close() { closed = true }
        fun emit(event: BleGattEvent) = observer(event)
    }

    private class TestRuntimeScheduler : BleRuntimeScheduler {
        val pending = mutableListOf<TestReconnectLease>()
        override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease =
            TestReconnectLease(delayMillis, callback).also(pending::add)

        fun runNext() {
            val next = nextOpen()
            next.run()
        }

        fun nextOpen(): TestReconnectLease =
            pending.firstOrNull { !it.closed && !it.ran }
                ?: error("Expected a pending runtime timer")
    }

    private class TestReconnectLease(
        val delayMillis: Long,
        private val callback: () -> Unit,
    ) : BleReconnectLease {
        var closed = false
        var ran = false
        override fun close() { closed = true }
        fun run() {
            if (closed || ran) return
            ran = true
            callback()
        }
    }

    private class TestLifecycleOwner : LifecycleOwner {
        val registry = LifecycleRegistry.createUnsafe(this)
        override val lifecycle: Lifecycle get() = registry
    }

    companion object {
        private val CANDIDATE = BleDiscoveredCompanion("opaque-test-token", "Test companion")
        private val SECURE_LINK = BleSecurityEvidence(
            encrypted = true,
            authenticatedBond = true,
            applicationAuthorized = true,
        )

        private fun protocolInfoBytes(
            capabilities: Int = REQUIRED_ACTION_CAPABILITIES,
            maxFragmentPayloadBytes: Int = 128,
        ): ByteArray = CompanionProtocolCodec.encodeProtocolInfo(
            CompanionProtocolInfo(
                capabilities = capabilities,
                maxFragmentPayloadBytes = maxFragmentPayloadBytes,
            ),
        ).value!!

        private fun snapshot(revision: Long = 9) = CompanionStatusSnapshot(
            revision = revision,
            radio = CompanionRadioState.READY,
            gnss = CompanionGnssState.SEARCHING,
            power = CompanionPowerState.NORMAL,
            positionSharing = CompanionPositionSharingState.STOPPED,
            queuedActionCount = 0,
        )

        private fun snapshotEnvelope(
            sessionNonce: Long,
            eventId: Long,
            snapshot: CompanionStatusSnapshot = snapshot(),
        ): ByteArray = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.SNAPSHOT,
                sessionNonce = sessionNonce,
                exchangeId = eventId,
                payload = CompanionSemanticCodec.encodeStatusSnapshot(snapshot).value!!,
            ),
        ).value!!

        private fun actionResultEnvelope(
            sessionNonce: Long,
            exchangeId: Long,
            result: CompanionActionResult,
        ): ByteArray = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.ACTION_RESULT,
                sessionNonce = sessionNonce,
                exchangeId = exchangeId,
                payload = CompanionSemanticCodec.encodeActionResult(result).value!!,
            ),
        ).value!!

        private fun quickStatus(status: CompanionQuickStatus = CompanionQuickStatus.OK) =
            CompanionActionRequest(CompanionActionKind.QUICK_STATUS, status)

        private fun assertPhase(runtime: BleCompanionRuntime, phase: BleNegotiationPhase) {
            assertEquals(phase, assertIs<BleRuntimeState.Negotiating>(runtime.state).phase)
        }
    }
}
