package io.github.nbjelanovic.otclient

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import io.github.nbjelanovic.otprotocol.COMPANION_MINIMUM_ATT_MTU
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
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class BleCompanionRuntimeTest {
    @Test
    fun gattContractIdentifiersAreExactAndBrandNeutral() {
        assertEquals("5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.SERVICE_UUID)
        assertEquals(
            "5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d1",
            CompanionGattV0Contract.PAIRABLE_ADVERTISING_UUID,
        )
        assertEquals("5e0f2a01-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.PROTOCOL_INFO_UUID)
        assertEquals("5e0f2a02-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.COMMAND_UUID)
        assertEquals("5e0f2a03-7c6b-4ea3-a210-0c4f1f43b7d0", CompanionGattV0Contract.STREAM_UUID)
        assertTrue(
            listOf(
                CompanionGattV0Contract.SERVICE_UUID,
                CompanionGattV0Contract.PAIRABLE_ADVERTISING_UUID,
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
    fun oneReturningOwnerCandidateReconnectsThroughFreshProtectedClaimWithoutAnotherPin() {
        val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())

        runtime.onLifecycleStart()
        val returningScan = facade.returningOwnerScans.single()
        assertTrue(returningScan.started)
        assertIs<BleRuntimeState.FindingReturningOwner>(runtime.state)

        returningScan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertTrue(facade.connections.isEmpty())
        returningScan.emit(BleScanEvent.Complete)

        val gatt = facade.connections.single()
        assertEquals(CANDIDATE.endpointToken, gatt.endpointToken)
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, gatt.purpose)
        assertIs<BleRuntimeState.Connecting>(runtime.state)
        gatt.emit(BleGattEvent.ProfileReady)
        assertEquals(1, gatt.protocolInfoReads)
        assertPhase(runtime, BleNegotiationPhase.PROTOCOL_INFO)

        gatt.emit(BleGattEvent.ProtectedProtocolInfoRead(authorizationProtocolInfoBytes(17)))
        assertEquals(listOf(COMPANION_MINIMUM_ATT_MTU), gatt.requestedMtus)
        assertPhase(runtime, BleNegotiationPhase.ATT_MTU)
        gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU))
        assertEquals(1, gatt.streamSubscriptions)
        assertPhase(runtime, BleNegotiationPhase.STREAM_SUBSCRIPTION)
        gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
        assertPhase(runtime, BleNegotiationPhase.AUTHORIZATION_CLAIM)
        assertEquals(1, gatt.commands.size)
        assertEquals(
            CompanionFrameKind.AUTHORIZATION_CLAIM_START,
            CompanionProtocolCodec.decodeFragment(gatt.commands.single()).value?.kind,
        )
        gatt.emit(BleGattEvent.StreamIndication(authorizationPendingEnvelope(17)))
        gatt.emit(BleGattEvent.StreamIndication(authorizationAcceptedEnvelope(17)))
        assertPhase(runtime, BleNegotiationPhase.INITIAL_SNAPSHOT)
        assertEquals(2, gatt.commands.size)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce = 17, eventId = 1)))

        val ready = assertIs<BleRuntimeState.Ready>(runtime.state)
        assertEquals(CANDIDATE, ready.session.companion)
        assertEquals(17, ready.session.sessionNonce)
    }

    @Test
    fun returningOwnerDiscoveryWaitsForCompletionAndFailsClosedOnAmbiguity() {
        val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
        runtime.onLifecycleStart()
        val returningScan = facade.returningOwnerScans.single()

        returningScan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertTrue(facade.connections.isEmpty())
        returningScan.emit(
            BleScanEvent.Candidate(BleDiscoveredCompanion("second-authorized", "Authorized device 2")),
        )

        assertTrue(returningScan.closed)
        assertEquals(
            BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS,
            assertIs<BleRuntimeState.Failed>(runtime.state).reason,
        )
        assertTrue(facade.connections.isEmpty())
        returningScan.emit(BleScanEvent.Complete)
        assertIs<BleRuntimeState.Failed>(runtime.state)
    }

    @Test
    fun returningOwnerNoMatchReturnsIdleAndWrongOwnerProtectedReadCannotReachReady() {
        run {
            val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
            val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
            runtime.onLifecycleStart()
            facade.returningOwnerScans.single().emit(BleScanEvent.Complete)
            assertIs<BleRuntimeState.Idle>(runtime.state)
            assertTrue(facade.connections.isEmpty())
        }
        run {
            val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
            val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
            runtime.onLifecycleStart()
            facade.returningOwnerScans.single().apply {
                emit(BleScanEvent.Candidate(CANDIDATE))
                emit(BleScanEvent.Complete)
            }
            val gatt = facade.connections.single()
            gatt.emit(BleGattEvent.ProfileReady)
            gatt.emit(BleGattEvent.Failed(BleGattFailure.AUTHORIZATION_REJECTED))
            assertEquals(
                BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE,
                assertIs<BleRuntimeState.Failed>(runtime.state).reason,
            )
            assertTrue(gatt.closed)
        }
    }

    @Test
    fun returningOwnerNullConnectionFailsWithoutRetry() {
        val scheduler = TestRuntimeScheduler()
        val facade = TestBluetoothFacade(
            connectionCreationSupported = false,
            returningOwnerScanSupported = true,
        )
        val runtime = BleCompanionRuntime(facade, scheduler)

        runtime.onLifecycleStart()
        facade.returningOwnerScans.single().apply {
            emit(BleScanEvent.Candidate(CANDIDATE))
            emit(BleScanEvent.Complete)
        }

        val failed = assertIs<BleRuntimeState.Failed>(runtime.state)
        assertEquals(BleRuntimeFailure.CONNECTION_START_FAILED, failed.reason)
        assertEquals(BleConnectionDiagnostic.LEASE_UNAVAILABLE, failed.connectionDiagnostic)
        assertTrue(facade.connections.isEmpty())
        assertFalse(scheduler.hasOpenTimers())
    }

    @Test
    fun returningOwnerStartFailureSchedulesBoundedRetry() {
        val scheduler = TestRuntimeScheduler()
        val facade = TestBluetoothFacade(
            gattStartResult = false,
            returningOwnerScanSupported = true,
        )
        val runtime = BleCompanionRuntime(facade, scheduler)

        runtime.onLifecycleStart()
        facade.returningOwnerScans.single().apply {
            emit(BleScanEvent.Candidate(CANDIDATE))
            emit(BleScanEvent.Complete)
        }

        val first = facade.connections.single()
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, first.purpose)
        assertTrue(first.started)
        assertTrue(first.closed)
        val firstRetry = assertIs<BleRuntimeState.Reconnecting>(runtime.state)
        assertEquals(1, firstRetry.attempt)
        assertEquals(BleConnectionDiagnostic.START_REJECTED, firstRetry.connectionDiagnostic)
        assertEquals(1, scheduler.pending.count { !it.closed && !it.ran })
        assertEquals(1_000L, scheduler.nextOpen().delayMillis)

        scheduler.runNext()
        assertEquals(2, facade.connections.size)
        assertTrue(facade.connections.last().closed)
        val secondRetry = assertIs<BleRuntimeState.Reconnecting>(runtime.state)
        assertEquals(2, secondRetry.attempt)
        assertEquals(BleConnectionDiagnostic.START_REJECTED, secondRetry.connectionDiagnostic)
        assertEquals(1, scheduler.pending.count { !it.closed && !it.ran })
        assertEquals(2_000L, scheduler.nextOpen().delayMillis)
    }

    @Test
    fun returningOwnerEarlyDisconnectCancelsProfileTimerAndSchedulesRetry() {
        val scheduler = TestRuntimeScheduler()
        val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
        val runtime = BleCompanionRuntime(facade, scheduler)

        runtime.onLifecycleStart()
        facade.returningOwnerScans.single().apply {
            emit(BleScanEvent.Candidate(CANDIDATE))
            emit(BleScanEvent.Complete)
        }
        val first = facade.connections.single()
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, first.purpose)
        assertIs<BleRuntimeState.Connecting>(runtime.state)
        assertEquals(1, scheduler.pending.count { !it.closed && !it.ran })

        first.emit(BleGattEvent.Disconnected)

        assertTrue(first.closed)
        val retry = assertIs<BleRuntimeState.Reconnecting>(runtime.state)
        assertEquals(1, retry.attempt)
        assertEquals(BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE, retry.connectionDiagnostic)
        assertEquals(1, scheduler.pending.count { !it.closed && !it.ran })
        assertEquals(1_000L, scheduler.nextOpen().delayMillis)

        scheduler.runNext()
        assertEquals(2, facade.connections.size)
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, facade.connections.last().purpose)
        assertEquals(
            BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE,
            assertIs<BleRuntimeState.Reconnecting>(runtime.state).connectionDiagnostic,
        )
    }

    @Test
    fun returningOwnerGattFailuresRetainExactIdentityFreeDiagnosticAndExistingPolicy() {
        data class Case(
            val gattFailure: BleGattFailure,
            val diagnostic: BleConnectionDiagnostic,
            val runtimeFailure: BleRuntimeFailure,
            val retries: Boolean,
        )

        val cases = listOf(
            Case(
                BleGattFailure.TRANSIENT_LINK,
                BleConnectionDiagnostic.GATT_TRANSIENT_LINK,
                BleRuntimeFailure.CONNECTION_START_FAILED,
                retries = true,
            ),
            Case(
                BleGattFailure.PERMISSION_REVOKED,
                BleConnectionDiagnostic.GATT_PERMISSION_REVOKED,
                BleRuntimeFailure.CONNECTION_START_FAILED,
                retries = false,
            ),
            Case(
                BleGattFailure.SECURITY_REJECTED,
                BleConnectionDiagnostic.GATT_SECURITY_REJECTED,
                BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
                retries = false,
            ),
            Case(
                BleGattFailure.BOND_REQUIRED,
                BleConnectionDiagnostic.GATT_BOND_REQUIRED,
                BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
                retries = false,
            ),
            Case(
                BleGattFailure.AUTHORIZATION_REJECTED,
                BleConnectionDiagnostic.GATT_AUTHORIZATION_REJECTED,
                BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE,
                retries = false,
            ),
            Case(
                BleGattFailure.PLATFORM_FAILURE,
                BleConnectionDiagnostic.GATT_PLATFORM_FAILURE,
                BleRuntimeFailure.CONNECTION_START_FAILED,
                retries = false,
            ),
        )

        for (case in cases) {
            val scheduler = TestRuntimeScheduler()
            val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
            val runtime = BleCompanionRuntime(facade, scheduler)
            runtime.onLifecycleStart()
            facade.returningOwnerScans.single().apply {
                emit(BleScanEvent.Candidate(CANDIDATE))
                emit(BleScanEvent.Complete)
            }

            facade.connections.single().emit(BleGattEvent.Failed(case.gattFailure))

            if (case.retries) {
                val reconnecting = assertIs<BleRuntimeState.Reconnecting>(runtime.state)
                assertEquals(case.diagnostic, reconnecting.connectionDiagnostic)
                assertEquals(1, scheduler.pending.count { !it.closed && !it.ran })
            } else {
                val failed = assertIs<BleRuntimeState.Failed>(runtime.state)
                assertEquals(case.runtimeFailure, failed.reason)
                assertEquals(case.diagnostic, failed.connectionDiagnostic)
                assertFalse(scheduler.hasOpenTimers())
            }
        }
    }

    @Test
    fun returningOwnerCannotSubstituteInjectedSecurityOrPlainProtocolInfoForProtectedOwnerProof() {
        run {
            val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
            val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
            runtime.onLifecycleStart()
            facade.returningOwnerScans.single().apply {
                emit(BleScanEvent.Candidate(CANDIDATE))
                emit(BleScanEvent.Complete)
            }
            facade.connections.single().emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
            assertEquals(
                BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
                assertIs<BleRuntimeState.Failed>(runtime.state).reason,
            )
        }
        run {
            val facade = TestBluetoothFacade(returningOwnerScanSupported = true)
            val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
            runtime.onLifecycleStart()
            facade.returningOwnerScans.single().apply {
                emit(BleScanEvent.Candidate(CANDIDATE))
                emit(BleScanEvent.Complete)
            }
            facade.connections.single().apply {
                emit(BleGattEvent.ProfileReady)
                emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes()))
            }
            assertEquals(
                BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
                assertIs<BleRuntimeState.Failed>(runtime.state).reason,
            )
        }
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
    fun completedScanIsTruthfulReleasesItsLeaseAndCanRescanWithoutOverlap() {
        val facade = TestBluetoothFacade()
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())
        runtime.onLifecycleStart()
        runtime.requestScan()
        val first = facade.scans.single()

        first.emit(BleScanEvent.Complete)

        assertTrue(first.closed)
        assertTrue(assertIs<BleRuntimeState.ScanComplete>(runtime.state).candidates.isEmpty())

        runtime.requestScan()
        val second = facade.scans.last()
        assertEquals(2, facade.scans.size)
        assertTrue(second.started)
        assertIs<BleRuntimeState.Scanning>(runtime.state)

        first.emit(BleScanEvent.Candidate(CANDIDATE))
        assertTrue(assertIs<BleRuntimeState.Scanning>(runtime.state).candidates.isEmpty())
        second.emit(BleScanEvent.Candidate(CANDIDATE))
        second.emit(BleScanEvent.Complete)

        assertTrue(second.closed)
        val complete = assertIs<BleRuntimeState.ScanComplete>(runtime.state)
        assertEquals(listOf(CANDIDATE), complete.candidates)
        assertEquals(CANDIDATE, runtime.beginAuthorization(CANDIDATE.endpointToken))
        assertIs<BleRuntimeState.AwaitingAuthorization>(runtime.state)
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
        assertEquals(BleConnectionPurpose.INITIAL_AUTHORIZATION, first.purpose)
        first.emit(BleGattEvent.Disconnected)
        assertEquals(1, assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).attempt)
        assertTrue(first.closed)

        scheduler.runNext()
        val second = fixture.facade.connections.last()
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, second.purpose)
        first.emit(BleGattEvent.StreamIndication(snapshotEnvelope(99, 99)))
        assertTrue(fixture.runtime.state is BleRuntimeState.Reconnecting)
        fixture.advanceReturningOwnerToInitialSnapshot(second, sessionNonce = 31)
        second.emit(BleGattEvent.StreamIndication(snapshotEnvelope(31, 1)))
        assertEquals(31, assertIs<BleRuntimeState.Ready>(fixture.runtime.state).session.sessionNonce)

        // The phone cannot distinguish a device reboot from a same-valued boot-local nonce.
        second.emit(BleGattEvent.Disconnected)
        scheduler.runNext()
        val third = fixture.facade.connections.last()
        third.emit(BleGattEvent.Failed(BleGattFailure.TRANSIENT_LINK))
        scheduler.runNext()
        val fourth = fixture.facade.connections.last()
        fourth.emit(BleGattEvent.Failed(BleGattFailure.TRANSIENT_LINK))
        val waiting = assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state)
        assertTrue(waiting.periodic)
        assertEquals(BleConnectionDiagnostic.GATT_TRANSIENT_LINK, waiting.connectionDiagnostic)
        assertEquals(15_000L, scheduler.nextOpen().delayMillis)
    }

    @Test
    fun periodicRecoveryWaitsFifteenSecondsRefreshesEndpointAndRequiresFreshSnapshot() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
        fixture.exhaustFastRetries(scheduler)
        val initialScans = fixture.facade.returningOwnerScans.size
        scheduler.advanceBy(14_999)
        assertEquals(initialScans, fixture.facade.returningOwnerScans.size)
        scheduler.advanceBy(1)
        assertEquals(initialScans + 1, fixture.facade.returningOwnerScans.size)
        fixture.facade.returningOwnerScans.last().emit(BleScanEvent.Complete)
        assertTrue(assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).periodic)
        assertEquals(15_000L, scheduler.nextOpen().delayMillis)
        scheduler.advanceBy(15_000)
        val scan = fixture.facade.returningOwnerScans.last()
        val refreshed = CANDIDATE.copy(endpointToken = "fresh-returning-endpoint")
        scan.emit(BleScanEvent.Candidate(refreshed))
        scan.emit(BleScanEvent.Complete)
        val gatt = fixture.facade.connections.last()
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, gatt.purpose)
        assertEquals(refreshed, assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).companion)
        fixture.advanceReturningOwnerToInitialSnapshot(gatt, sessionNonce = 73)
        assertFalse(fixture.runtime.state is BleRuntimeState.Ready)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(73, 1)))
        assertIs<BleRuntimeState.Ready>(fixture.runtime.state)
        assertFalse(scheduler.hasOpenTimers())
        gatt.emit(BleGattEvent.Disconnected)
        val fast = assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state)
        assertFalse(fast.periodic)
        assertEquals(1, fast.attempt)
        assertEquals(1_000L, scheduler.nextOpen().delayMillis)
    }

    @Test
    fun repeatedPeriodicFailuresNeverRestartFastBurstOrAccumulateLeases() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
        fixture.exhaustFastRetries(scheduler)
        repeat(100) {
            assertEquals(15_000L, scheduler.nextOpen().delayMillis)
            scheduler.advanceBy(15_000)
            val scan = fixture.facade.returningOwnerScans.last()
            scan.emit(BleScanEvent.Candidate(CANDIDATE))
            scan.emit(BleScanEvent.Complete)
            fixture.facade.connections.last().emit(BleGattEvent.Failed(BleGattFailure.TRANSIENT_LINK))
            assertTrue(assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).periodic)
            assertEquals(3, assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).attempt)
            assertEquals(1, scheduler.pending.count { !it.closed && !it.ran })
            assertTrue(fixture.facade.connections.all { it.closed })
        }
    }

    @Test
    fun disconnectStopAndCloseCancelEveryPeriodicPhase() {
        for (phase in 0..2) for (control in 0..2) {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
            fixture.exhaustFastRetries(scheduler)
            if (phase >= 1) scheduler.advanceBy(15_000)
            val scan = fixture.facade.returningOwnerScans.last()
            if (phase == 2) {
                scan.emit(BleScanEvent.Candidate(CANDIDATE))
                scan.emit(BleScanEvent.Complete)
            }
            when (control) {
                0 -> fixture.runtime.disconnect()
                1 -> fixture.runtime.onLifecycleStop()
                else -> fixture.runtime.close()
            }
            val count = fixture.facade.connections.size
            scheduler.advanceBy(120_000)
            scan.emit(BleScanEvent.Candidate(CANDIDATE))
            scan.emit(BleScanEvent.Complete)
            assertEquals(count, fixture.facade.connections.size)
            assertFalse(scheduler.hasOpenTimers())
            assertTrue(fixture.facade.connections.all { it.closed })
        }
    }

    @Test
    fun periodicScanAmbiguityAndBluetoothLossStopRecovery() {
        for (ambiguous in listOf(false, true)) {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
            fixture.exhaustFastRetries(scheduler)
            if (!ambiguous) fixture.facade.preflight = BlePreflight(BleRuntimeBlock.BLUETOOTH_UNAVAILABLE)
            scheduler.advanceBy(15_000)
            if (ambiguous) {
                val scan = fixture.facade.returningOwnerScans.last()
                scan.emit(BleScanEvent.Candidate(CANDIDATE))
                scan.emit(BleScanEvent.Candidate(CANDIDATE.copy(endpointToken = "another-owner")))
                assertEquals(BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS, assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason)
            } else assertIs<BleRuntimeState.Blocked>(fixture.runtime.state)
            assertFalse(scheduler.hasOpenTimers())
        }
    }

    @Test
    fun periodicLifecycleResumeRefreshesDiscoveryAndIgnoresPreviousScan() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
        fixture.exhaustFastRetries(scheduler)
        scheduler.advanceBy(15_000)
        val oldScan = fixture.facade.returningOwnerScans.last()
        fixture.runtime.onLifecycleStop()
        val count = fixture.facade.connections.size
        fixture.runtime.onLifecycleStart()
        assertIs<BleRuntimeState.FindingReturningOwner>(fixture.runtime.state)
        assertEquals(count, fixture.facade.connections.size)
        oldScan.emit(BleScanEvent.Candidate(CANDIDATE))
        oldScan.emit(BleScanEvent.Complete)
        assertEquals(count, fixture.facade.connections.size)
        fixture.facade.returningOwnerScans.last().emit(BleScanEvent.Complete)
        assertEquals(15_000L, scheduler.nextOpen().delayMillis)
    }

    @Test
    fun periodicSecurityFailuresAndPendingAuthorizationLossNeverRetry() {
        for (failure in listOf(BleGattFailure.SECURITY_REJECTED, BleGattFailure.PERMISSION_REVOKED,
            BleGattFailure.BOND_REQUIRED, BleGattFailure.AUTHORIZATION_REJECTED, null)) {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
            fixture.exhaustFastRetries(scheduler)
            scheduler.advanceBy(15_000)
            val scan = fixture.facade.returningOwnerScans.last()
            scan.emit(BleScanEvent.Candidate(CANDIDATE))
            scan.emit(BleScanEvent.Complete)
            val gatt = fixture.facade.connections.last()
            if (failure == null) {
                gatt.emit(BleGattEvent.ProfileReady)
                gatt.emit(BleGattEvent.ProtectedProtocolInfoRead(authorizationProtocolInfoBytes(79)))
                gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU))
                gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
                gatt.emit(BleGattEvent.StreamIndication(authorizationPendingEnvelope(79)))
                gatt.emit(BleGattEvent.Disconnected)
            } else gatt.emit(BleGattEvent.Failed(failure))
            assertIs<BleRuntimeState.Failed>(fixture.runtime.state)
            assertFalse(scheduler.hasOpenTimers())
        }
    }

    @Test
    fun periodicProfileTimeoutAndTransientScanFailureReturnToSlowCadence() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
        fixture.exhaustFastRetries(scheduler)
        scheduler.advanceBy(15_000)
        fixture.facade.returningOwnerScans.last().emit(BleScanEvent.Failed(BleGattFailure.TRANSIENT_LINK))
        assertEquals(15_000L, scheduler.nextOpen().delayMillis)
        scheduler.advanceBy(15_000)
        val scan = fixture.facade.returningOwnerScans.last()
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        scan.emit(BleScanEvent.Complete)
        fixture.facade.connections.last().emit(BleGattEvent.GattOpened)
        scheduler.advanceBy(INITIAL_GATT_PROFILE_TIMEOUT_MILLIS)
        assertTrue(assertIs<BleRuntimeState.Reconnecting>(fixture.runtime.state).periodic)
        assertEquals(15_000L, scheduler.nextOpen().delayMillis)
        assertTrue(fixture.facade.connections.last().closed)
    }

    @Test
    fun lifecycleStopAfterPeriodicAuthorizationPendingRequiresExplicitRetry() {
        val scheduler = TestRuntimeScheduler()
        val fixture = Fixture(TestBluetoothFacade(returningOwnerScanSupported = true), scheduler)
        fixture.exhaustFastRetries(scheduler)
        scheduler.advanceBy(15_000)
        val scan = fixture.facade.returningOwnerScans.last()
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        scan.emit(BleScanEvent.Complete)
        val gatt = fixture.facade.connections.last()
        gatt.emit(BleGattEvent.ProfileReady)
        gatt.emit(BleGattEvent.ProtectedProtocolInfoRead(authorizationProtocolInfoBytes(83)))
        gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU))
        gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
        gatt.emit(BleGattEvent.StreamIndication(authorizationPendingEnvelope(83)))
        fixture.runtime.onLifecycleStop()
        val scanCount = fixture.facade.returningOwnerScans.size
        val connectionCount = fixture.facade.connections.size
        repeat(2) {
            fixture.runtime.onLifecycleStart()
            assertEquals(BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST,
                assertIs<BleRuntimeState.Failed>(fixture.runtime.state).reason)
            assertEquals(scanCount, fixture.facade.returningOwnerScans.size)
            assertEquals(connectionCount, fixture.facade.connections.size)
            assertFalse(scheduler.hasOpenTimers())
            if (it == 0) fixture.runtime.onLifecycleStop()
        }
        fixture.runtime.requestScan()
        assertIs<BleRuntimeState.Scanning>(fixture.runtime.state)
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
            assertEquals(INITIAL_GATT_PROFILE_TIMEOUT_MILLIS, timeout.delayMillis)
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
    fun duplicateStaleAndLateGattOpenedEventsCannotRenewOrReopenTheTimer() {
        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            val staleGatt = fixture.connectGatt()
            val firstTimer = scheduler.nextOpen()

            staleGatt.emit(BleGattEvent.Disconnected)
            val reconnectTimer = scheduler.nextOpen()
            reconnectTimer.run()
            val currentGatt = fixture.facade.connections.last()
            val currentTimer = scheduler.nextOpen()

            staleGatt.emit(BleGattEvent.GattOpened)
            currentGatt.emit(BleGattEvent.GattOpened)
            assertTrue(currentTimer === scheduler.nextOpen())
            assertEquals(INITIAL_GATT_PROFILE_TIMEOUT_MILLIS, currentTimer.delayMillis)
            assertFalse(currentGatt.closed)
            assertTrue(firstTimer.closed)
        }

        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            val gatt = fixture.connectGatt()
            val timer = scheduler.nextOpen()

            fixture.runtime.onLifecycleStop()
            assertTrue(timer.closed)
            gatt.emit(BleGattEvent.GattOpened)
            assertFalse(scheduler.hasOpenTimers())
            assertIs<BleRuntimeState.Inactive>(fixture.runtime.state)
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
    fun factoryResetHasDedicatedReadyOnlyApiAndExactOta0Payload() {
        val fixture = Fixture()
        val gatt = fixture.readyGatt(sessionNonce = 71)
        val reset = CompanionActionRequest(CompanionActionKind.FACTORY_RESET)

        assertFalse(fixture.runtime.submitAction(reset))
        assertTrue(fixture.runtime.submitFactoryReset())
        assertIs<BleRuntimeState.FactoryResetRequesting>(fixture.runtime.state)

        val fragment = CompanionProtocolCodec.decodeFragment(gatt.commands.last()).value!!
        assertEquals(CompanionFrameKind.ACTION_REQUEST, fragment.kind)
        assertEquals(71, fragment.sessionNonce)
        assertEquals(1, fragment.exchangeId)
        val decoded = CompanionSemanticCodec.decodeActionRequest(fragment.payload).value!!
        assertEquals(CompanionActionKind.FACTORY_RESET, decoded.kind)
        assertEquals(RESET_RECEIPT, decoded.factoryResetReceipt)
        assertEquals(5, fragment.payload[6].toInt())
        assertEquals(0, fragment.payload[7].toInt())
        assertContentEquals(
            byteArrayOf(0x88.toByte(), 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11),
            fragment.payload.sliceArray(8 until 16),
        )
        assertTrue(fragment.payload.sliceArray(16 until fragment.payload.size).all { it.toInt() == 0 })
        assertTrue(fixture.facade.resetVerificationScans.isEmpty())
    }

    @Test
    fun admittedFactoryResetNeedsExactReceiptD1BeforeLocalRecordClears() {
        val facade = TestBluetoothFacade()
        facade.factoryResetCleanupResult = FactoryResetLocalCleanupResult.SYSTEM_BOND_REMAINS
        val fixture = Fixture(facade = facade)
        val gatt = fixture.readyGatt(sessionNonce = 72)

        assertTrue(fixture.runtime.submitFactoryReset())
        val admitted = CompanionActionResult(
            kind = CompanionActionKind.FACTORY_RESET,
            factoryResetReceipt = RESET_RECEIPT,
            disposition = CompanionActionDisposition.ADMITTED,
        )
        gatt.emit(BleGattEvent.StreamIndication(actionResultEnvelope(72, 1, admitted)))
        assertIs<BleRuntimeState.FactoryResetErasing>(fixture.runtime.state)
        assertTrue(facade.factoryResetCleanupReceipts.isEmpty())

        gatt.emit(BleGattEvent.Disconnected)
        val verification = facade.resetVerificationScans.single()
        assertTrue(verification.started)
        assertIs<BleRuntimeState.FactoryResetVerifying>(fixture.runtime.state)

        verification.emit(BleScanEvent.FactoryResetReceiptObserved(RESET_RECEIPT + 1uL))
        val wrong = assertIs<BleRuntimeState.FactoryResetNotVerified>(fixture.runtime.state)
        assertEquals(FactoryResetNotVerifiedReason.WRONG_DEVICE_OBSERVED, wrong.reason)
        assertTrue(wrong.canRetryVerification)
        assertTrue(facade.factoryResetCleanupReceipts.isEmpty())

        assertTrue(fixture.runtime.retryFactoryResetVerification())
        facade.resetVerificationScans.last().emit(BleScanEvent.FactoryResetReceiptObserved(RESET_RECEIPT))
        val complete = assertIs<BleRuntimeState.FactoryResetComplete>(fixture.runtime.state)
        assertTrue(complete.systemBondRemovalRequired)
        assertEquals(listOf(RESET_RECEIPT), facade.factoryResetCleanupReceipts)
    }

    @Test
    fun factoryResetDisconnectTimeoutAndRejectionNeverReportSuccessOrDiscardRecord() {
        run {
            val fixture = Fixture()
            val gatt = fixture.readyGatt(sessionNonce = 72)
            gatt.writeResult = false
            assertFalse(fixture.runtime.submitFactoryReset())

            val uncertain = assertIs<BleRuntimeState.FactoryResetNotVerified>(fixture.runtime.state)
            assertEquals(FactoryResetNotVerifiedReason.REQUEST_WRITE_UNCERTAIN, uncertain.reason)
            assertTrue(uncertain.canRetryVerification)
            assertTrue(fixture.runtime.retryFactoryResetVerification())
            assertTrue(fixture.facade.resetVerificationScans.single().started)
            assertTrue(fixture.facade.factoryResetCleanupReceipts.isEmpty())
        }

        run {
            val fixture = Fixture()
            val gatt = fixture.readyGatt(sessionNonce = 73)
            assertTrue(fixture.runtime.submitFactoryReset())
            gatt.emit(BleGattEvent.Disconnected)

            val failed = assertIs<BleRuntimeState.FactoryResetNotVerified>(fixture.runtime.state)
            assertEquals(FactoryResetNotVerifiedReason.CONNECTION_LOST_BEFORE_ACCEPTANCE, failed.reason)
            assertTrue(failed.canRetryVerification)
            assertTrue(fixture.facade.factoryResetCleanupReceipts.isEmpty())

            assertTrue(fixture.runtime.retryFactoryResetVerification())
            assertTrue(fixture.facade.resetVerificationScans.single().started)
        }

        run {
            val scheduler = TestRuntimeScheduler()
            val fixture = Fixture(scheduler = scheduler)
            fixture.readyGatt(sessionNonce = 74)
            assertTrue(fixture.runtime.submitFactoryReset())
            scheduler.nextOpen().run()
            val timeout = assertIs<BleRuntimeState.FactoryResetNotVerified>(fixture.runtime.state)
            assertEquals(FactoryResetNotVerifiedReason.RESPONSE_TIMEOUT, timeout.reason)
            assertTrue(timeout.canRetryVerification)
            assertTrue(fixture.runtime.retryFactoryResetVerification())
            assertTrue(fixture.facade.resetVerificationScans.single().started)
            assertTrue(fixture.facade.factoryResetCleanupReceipts.isEmpty())
        }

        run {
            val fixture = Fixture()
            val gatt = fixture.readyGatt(sessionNonce = 75)
            assertTrue(fixture.runtime.submitFactoryReset())
            val rejected = CompanionActionResult(
                kind = CompanionActionKind.FACTORY_RESET,
                factoryResetReceipt = RESET_RECEIPT,
                disposition = CompanionActionDisposition.REJECTED,
                rejectReason = io.github.nbjelanovic.otprotocol.CompanionActionRejectReason.POLICY_DENIED,
            )
            gatt.emit(BleGattEvent.StreamIndication(actionResultEnvelope(75, 1, rejected)))
            val state = assertIs<BleRuntimeState.FactoryResetNotVerified>(fixture.runtime.state)
            assertEquals(FactoryResetNotVerifiedReason.REQUEST_REJECTED, state.reason)
            assertFalse(state.canRetryVerification)
            assertFalse(fixture.runtime.retryFactoryResetVerification())
            assertEquals(null, fixture.facade.pendingFactoryResetReceipt)
            assertTrue(fixture.facade.factoryResetCleanupReceipts.isEmpty())
        }
    }

    @Test
    fun admittedFactoryResetVerificationTimeoutRetainsExactTargetAndCanRetry() {
        val facade = TestBluetoothFacade()
        val fixture = Fixture(facade = facade)
        val gatt = fixture.readyGatt(sessionNonce = 76)
        assertTrue(fixture.runtime.submitFactoryReset())
        gatt.emit(
            BleGattEvent.StreamIndication(
                actionResultEnvelope(
                    76,
                    1,
                    CompanionActionResult(
                        kind = CompanionActionKind.FACTORY_RESET,
                        factoryResetReceipt = RESET_RECEIPT,
                        disposition = CompanionActionDisposition.ADMITTED,
                    ),
                ),
            ),
        )
        gatt.emit(BleGattEvent.Disconnected)
        facade.resetVerificationScans.single().emit(BleScanEvent.Complete)

        val notVerified = assertIs<BleRuntimeState.FactoryResetNotVerified>(fixture.runtime.state)
        assertEquals(FactoryResetNotVerifiedReason.VERIFICATION_TIMEOUT, notVerified.reason)
        assertTrue(notVerified.canRetryVerification)
        assertTrue(facade.factoryResetCleanupReceipts.isEmpty())

        fixture.runtime.onLifecycleStop()
        fixture.runtime.onLifecycleStart()
        assertEquals(2, facade.resetVerificationScans.size)
        assertTrue(facade.resetVerificationScans.last().started)
    }

    @Test
    fun pendingReceiptRecoversWithoutPersistedDeviceIdentityAfterRuntimeRestart() {
        val facade = TestBluetoothFacade().apply {
            pendingFactoryResetReceipt = RESET_RECEIPT
        }
        val runtime = BleCompanionRuntime(facade, TestRuntimeScheduler())

        runtime.onLifecycleStart()

        assertIs<BleRuntimeState.FactoryResetVerifying>(runtime.state)
        assertTrue(facade.connections.isEmpty())
        assertTrue(facade.scans.isEmpty())
        val verification = facade.resetVerificationScans.single()
        assertTrue(verification.started)

        verification.emit(BleScanEvent.FactoryResetReceiptObserved(RESET_RECEIPT))
        val complete = assertIs<BleRuntimeState.FactoryResetComplete>(runtime.state)
        assertFalse(complete.systemBondRemovalRequired)
        assertEquals(listOf(RESET_RECEIPT), facade.factoryResetCleanupReceipts)
        assertEquals(null, facade.pendingFactoryResetReceipt)
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

        fun advanceReturningOwnerToInitialSnapshot(gatt: TestGattLease, sessionNonce: Long) {
            gatt.emit(BleGattEvent.ProfileReady)
            gatt.emit(BleGattEvent.ProtectedProtocolInfoRead(authorizationProtocolInfoBytes(sessionNonce)))
            gatt.emit(BleGattEvent.MtuChanged(COMPANION_MINIMUM_ATT_MTU))
            gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
            gatt.emit(BleGattEvent.StreamIndication(authorizationPendingEnvelope(sessionNonce)))
            gatt.emit(BleGattEvent.StreamIndication(authorizationAcceptedEnvelope(sessionNonce)))
        }

        fun readyGatt(sessionNonce: Long, initialEventId: Long = 1): TestGattLease {
            val gatt = connectGatt()
            advanceToInitialSnapshot(gatt)
            gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce, initialEventId)))
            assertIs<BleRuntimeState.Ready>(runtime.state)
            return gatt
        }

        fun exhaustFastRetries(scheduler: TestRuntimeScheduler) {
            readyGatt(71).emit(BleGattEvent.Disconnected)
            for (delay in listOf(1_000L, 2_000L, 4_000L)) {
                assertEquals(delay, scheduler.nextOpen().delayMillis)
                scheduler.advanceBy(delay)
                facade.connections.last().emit(BleGattEvent.Failed(BleGattFailure.TRANSIENT_LINK))
            }
            assertTrue(assertIs<BleRuntimeState.Reconnecting>(runtime.state).periodic)
        }
    }

    private class TestBluetoothFacade(
        private val gattStartEvent: BleGattEvent? = null,
        private val gattStartResult: Boolean = true,
        private val connectionCreationSupported: Boolean = true,
        private val returningOwnerScanSupported: Boolean = false,
    ) : AndroidBluetoothFacade {
        var preflight = BlePreflight()
        val scans = mutableListOf<TestScanLease>()
        val returningOwnerScans = mutableListOf<TestScanLease>()
        val resetVerificationScans = mutableListOf<TestScanLease>()
        val connections = mutableListOf<TestGattLease>()
        val factoryResetCleanupReceipts = mutableListOf<ULong>()
        var factoryResetCleanupResult = FactoryResetLocalCleanupResult.CLEARED
        var pendingFactoryResetReceipt: ULong? = null

        override fun preflight(): BlePreflight = preflight

        override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease =
            TestScanLease(observer).also(scans::add)

        override fun createReturningOwnerScan(observer: (BleScanEvent) -> Unit): BleScanLease? =
            if (returningOwnerScanSupported) TestScanLease(observer).also(returningOwnerScans::add) else null

        override fun stageFactoryResetReceipt(): ULong? {
            if (pendingFactoryResetReceipt != null) return null
            pendingFactoryResetReceipt = RESET_RECEIPT
            return RESET_RECEIPT
        }

        override fun loadPendingFactoryResetReceipt(): ULong? = pendingFactoryResetReceipt

        override fun clearPendingFactoryResetReceipt(receipt: ULong): Boolean {
            if (pendingFactoryResetReceipt != receipt) return false
            pendingFactoryResetReceipt = null
            return true
        }

        override fun createFactoryResetVerificationScan(
            receipt: ULong,
            observer: (BleScanEvent) -> Unit,
        ): BleScanLease? =
            if (receipt == pendingFactoryResetReceipt) {
                TestScanLease(observer).also(resetVerificationScans::add)
            } else {
                null
            }

        override fun completeFactoryResetVerification(receipt: ULong): FactoryResetLocalCleanupResult {
            if (receipt != pendingFactoryResetReceipt) return FactoryResetLocalCleanupResult.FAILED
            factoryResetCleanupReceipts += receipt
            if (factoryResetCleanupResult != FactoryResetLocalCleanupResult.FAILED) {
                pendingFactoryResetReceipt = null
            }
            return factoryResetCleanupResult
        }

        override fun createConnection(
            endpointToken: String,
            purpose: BleConnectionPurpose,
            observer: (BleGattEvent) -> Unit,
        ): BleGattLease? =
            if (connectionCreationSupported) {
                TestGattLease(endpointToken, purpose, observer, gattStartEvent, gattStartResult).also(connections::add)
            } else {
                null
            }
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
        val purpose: BleConnectionPurpose,
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
        var writeResult = true

        override fun start(): Boolean {
            started = true
            if (startResult) observer(BleGattEvent.GattOpened)
            startEvent?.let(observer)
            return startResult
        }
        override fun requestMtu(mtu: Int): Boolean = true.also { requestedMtus += mtu }
        override fun readProtocolInfo(): Boolean = true.also { protocolInfoReads += 1 }
        override fun subscribeStreamIndications(): Boolean = true.also { streamSubscriptions += 1 }
        override fun writeCommandWithResponse(value: ByteArray): Boolean = writeResult.also { commands += value.copyOf() }
        override fun close() { closed = true }
        fun emit(event: BleGattEvent) = observer(event)
    }

    private class TestRuntimeScheduler : BleRuntimeScheduler {
        val pending = mutableListOf<TestReconnectLease>()
        private var nowMillis = 0L
        override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease =
            TestReconnectLease(delayMillis, callback, nowMillis + delayMillis).also(pending::add)

        fun advanceBy(millis: Long) {
            val target = nowMillis + millis
            while (true) {
                val next = pending.filter { !it.closed && !it.ran && it.dueMillis <= target }
                    .minByOrNull { it.dueMillis } ?: break
                nowMillis = next.dueMillis
                next.run()
            }
            nowMillis = target
        }

        fun runNext() {
            val next = nextOpen()
            nowMillis = maxOf(nowMillis, next.dueMillis)
            next.run()
        }

        fun nextOpen(): TestReconnectLease =
            pending.firstOrNull { !it.closed && !it.ran }
                ?: error("Expected a pending runtime timer")

        fun hasOpenTimers(): Boolean = pending.any { !it.closed && !it.ran }
    }

    private class TestReconnectLease(
        val delayMillis: Long,
        private val callback: () -> Unit,
        val dueMillis: Long = delayMillis,
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
        private val RESET_RECEIPT = 0x1122334455667788uL
        private val SECURE_LINK = BleSecurityEvidence(
            encrypted = true,
            authenticatedBond = true,
            applicationAuthorized = true,
        )
        private val AUTHORIZATION_CORRELATION =
            CompanionAuthorizationCorrelation(ByteArray(16) { (0xa0 + it).toByte() })

        private fun authorizationProtocolInfoBytes(sessionNonce: Long): ByteArray =
            CompanionAuthorizationProtocolInfoCodec.encode(
                CompanionAuthorizationProtocolInfo(provisionalSessionNonce = sessionNonce),
            ).value!!

        private fun authorizationPendingEnvelope(sessionNonce: Long): ByteArray =
            CompanionProtocolCodec.encodeFragment(
                CompanionFragment(
                    kind = CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS,
                    sessionNonce = sessionNonce,
                    exchangeId = 1,
                    payload = CompanionAuthorizationWireCodec.encodeClaimStatus(
                        CompanionAuthorizationClaimStatus(
                            CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
                            CompanionAuthorizationClaimState.PENDING,
                            AUTHORIZATION_CORRELATION,
                        ),
                    ).value!!,
                ),
            ).value!!

        private fun authorizationAcceptedEnvelope(sessionNonce: Long): ByteArray =
            CompanionProtocolCodec.encodeFragment(
                CompanionFragment(
                    kind = CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT,
                    sessionNonce = sessionNonce,
                    exchangeId = 1,
                    payload = CompanionAuthorizationWireCodec.encodeClaimResult(
                        CompanionAuthorizationClaimResult(
                            CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
                            CompanionAuthorizationClaimOutcome.ACCEPTED,
                            CompanionAuthorizationDenyReason.NONE,
                            AUTHORIZATION_CORRELATION,
                        ),
                    ).value!!,
                ),
            ).value!!

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
