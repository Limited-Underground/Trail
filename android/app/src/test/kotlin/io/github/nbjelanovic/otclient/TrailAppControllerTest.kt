package io.github.nbjelanovic.otclient

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimState
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationClaimStatus
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationCorrelation
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfoCodec
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationPurpose
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationWireCodec
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionSemanticCodec
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import io.github.nbjelanovic.otprotocol.CompanionFragment
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class TrailAppControllerTest {
    @Test
    fun modeChoiceIsExplicitAndBluetoothFailureNeverFallsBackToLocalFixtures() {
        val harness = harness()
        assertEquals(TrailAppUiState.ChooseMode, harness.controller.state)

        harness.controller.chooseLocalTestMode()
        assertIs<TrailAppUiState.LocalTest>(harness.controller.state)
        harness.controller.chooseLocalDevice()
        harness.controller.connectLocalDevice("fake-a")
        assertIs<CompanionUiState.Connected>(assertIs<TrailAppUiState.LocalTest>(harness.controller.state).companionState)

        harness.facade.preflight = BlePreflight(BleRuntimeBlock.BLUETOOTH_DISABLED)
        harness.controller.chooseBluetoothDeviceMode()
        assertIs<CompanionUiState.Disconnected>(harness.local.state)
        harness.controller.scanBluetoothDevices()
        val blocked = assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state)
        assertEquals(BleRuntimeBlock.BLUETOOTH_DISABLED, assertIs<BleRuntimeState.Blocked>(blocked.runtimeState).reason)
        assertFalse(harness.controller.state is TrailAppUiState.LocalTest)
        harness.controller.close()
    }

    @Test
    fun permissionRequestUsesRecheckedAuthorityAndLateCallbacksCannotStartWork() {
        val harness = harness(permission = NearbyDevicesPermissionState.MISSING)
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        assertTrue(harness.facade.scans.isEmpty())

        assertTrue(harness.controller.beginNearbyDevicesPermissionRequest())
        assertFalse(harness.controller.beginNearbyDevicesPermissionRequest())
        harness.controller.onLifecycleStop()
        harness.permission.currentValue = NearbyDevicesPermissionState.GRANTED
        harness.controller.onNearbyDevicesPermissionResult()
        val stopped = assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state)
        assertEquals(NearbyDevicesPermissionState.GRANTED, stopped.permissionState)
        assertEquals(BleRuntimeState.Inactive, stopped.runtimeState)
        assertTrue(harness.facade.scans.isEmpty())

        harness.controller.onLifecycleStart()
        harness.permission.currentValue = NearbyDevicesPermissionState.MISSING
        harness.controller.refreshPermissionState()
        assertTrue(harness.controller.beginNearbyDevicesPermissionRequest())
        harness.controller.returnToModeChoice()
        harness.permission.currentValue = NearbyDevicesPermissionState.GRANTED
        harness.controller.onNearbyDevicesPermissionResult()
        assertEquals(TrailAppUiState.ChooseMode, harness.controller.state)
        assertTrue(harness.facade.scans.isEmpty())
        harness.controller.close()
    }

    @Test
    fun grantedPermissionEnablesOnlyAnExplicitScanAndLifecycleStopReleasesIt() {
        val harness = harness()
        harness.controller.chooseBluetoothDeviceMode()
        assertTrue(harness.facade.scans.isEmpty())
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.single()
        assertTrue(scan.started)
        assertIs<BleRuntimeState.Scanning>(assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState)

        harness.controller.onLifecycleStop()
        assertTrue(scan.closed)
        assertEquals(
            BleRuntimeState.Inactive,
            assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState,
        )
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertEquals(
            BleRuntimeState.Inactive,
            assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState,
        )
        harness.controller.onLifecycleStart()
        assertEquals(BleRuntimeState.Idle, assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state).runtimeState)
        harness.controller.close()
    }

    @Test
    fun switchingModesClosesScanningConnectingAndReadyLeasesBeforePublishingTheNewMode() {
        val scanning = harness()
        scanning.controller.chooseBluetoothDeviceMode()
        scanning.controller.scanBluetoothDevices()
        val scanLease = scanning.facade.scans.single()
        scanning.controller.chooseLocalTestMode()
        assertTrue(scanLease.closed)
        assertIs<TrailAppUiState.LocalTest>(scanning.controller.state)
        scanning.controller.close()

        val connecting = harness()
        val connectingLease = beginConnection(connecting)
        connecting.controller.returnToModeChoice()
        assertTrue(connectingLease.closed)
        assertEquals(TrailAppUiState.ChooseMode, connecting.controller.state)
        connecting.controller.close()

        val ready = harness()
        val readyLease = beginConnection(ready)
        makeReady(readyLease)
        assertIs<BleRuntimeState.Ready>(assertIs<TrailAppUiState.BluetoothDevice>(ready.controller.state).runtimeState)
        ready.controller.chooseLocalTestMode()
        assertTrue(readyLease.closed)
        assertIs<TrailAppUiState.LocalTest>(ready.controller.state)
        ready.controller.close()
    }

    @Test
    fun permissionRevocationClosesCurrentBluetoothWorkBeforeUiReportsMissing() {
        val harness = harness()
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.single()
        val observed = mutableListOf<TrailAppUiState>()
        harness.controller.observe(observed::add)

        harness.permission.currentValue = NearbyDevicesPermissionState.MISSING
        harness.controller.refreshPermissionState()

        assertTrue(scan.closed)
        val current = assertIs<TrailAppUiState.BluetoothDevice>(harness.controller.state)
        assertEquals(NearbyDevicesPermissionState.MISSING, current.permissionState)
        assertEquals(BleRuntimeState.Idle, current.runtimeState)
        assertTrue(
            observed.filterIsInstance<TrailAppUiState.BluetoothDevice>().none {
                it.runtimeState == BleRuntimeState.Idle && it.permissionState == NearbyDevicesPermissionState.GRANTED
            },
        )
        harness.controller.close()
    }

    @Test
    fun lifecycleStopAndCloseRequestedInsideObserverAreDeferredButNeverLost() {
        val stopped = harness()
        stopped.controller.chooseBluetoothDeviceMode()
        stopped.controller.scanBluetoothDevices()
        val stoppedScan = stopped.facade.scans.single()
        stopped.controller.observe { state ->
            if (state is TrailAppUiState.BluetoothDevice && state.runtimeState is BleRuntimeState.Scanning) {
                stopped.controller.onLifecycleStop()
            }
        }
        assertTrue(stoppedScan.closed)
        assertEquals(BleRuntimeState.Inactive, assertIs<TrailAppUiState.BluetoothDevice>(stopped.controller.state).runtimeState)
        assertEquals(0, stopped.facade.closeCount)
        stopped.controller.close()
        assertEquals(1, stopped.facade.closeCount)

        val destroyed = harness(startLifecycle = false)
        val owner = TestLifecycleOwner()
        val binding = TrailAppLifecycleBinding(owner.lifecycle, destroyed.controller)
        owner.registry.handleLifecycleEvent(Lifecycle.Event.ON_START)
        destroyed.controller.chooseBluetoothDeviceMode()
        destroyed.controller.scanBluetoothDevices()
        val destroyedScan = destroyed.facade.scans.single()
        destroyed.controller.observe { state ->
            if (state is TrailAppUiState.BluetoothDevice && state.runtimeState is BleRuntimeState.Scanning) {
                binding.close()
            }
        }
        assertTrue(destroyedScan.closed)
        assertEquals(BleRuntimeState.Closed, destroyed.runtime.state)
        assertEquals(1, destroyed.facade.closeCount)
        binding.close()
        assertEquals(1, destroyed.facade.closeCount)
        destroyedScan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertEquals(BleRuntimeState.Closed, destroyed.runtime.state)
    }

    @Test
    fun lifecycleStopReleasesConnectingAndReadyGattAndEveryOwnedTimer() {
        val connecting = harness()
        val connectingGatt = beginConnection(connecting)
        assertTrue(connecting.scheduler.leases.any { !it.closed })
        connecting.controller.onLifecycleStop()
        assertTrue(connectingGatt.closed)
        assertTrue(connecting.scheduler.leases.all { it.closed })
        assertEquals(BleRuntimeState.Inactive, connecting.runtime.state)
        connecting.controller.close()

        val ready = harness()
        val readyGatt = beginConnection(ready)
        makeReady(readyGatt)
        assertIs<BleRuntimeState.Ready>(ready.runtime.state)
        ready.controller.onLifecycleStop()
        assertTrue(readyGatt.closed)
        assertTrue(ready.scheduler.leases.all { it.closed })
        assertEquals(BleRuntimeState.Inactive, ready.runtime.state)
        ready.controller.close()
    }

    @Test
    fun deferredCloseWinsOverQueuedLifecycleChangesAndLatePermissionResultIsIgnored() {
        val harness = harness()
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.single()
        harness.controller.observe { state ->
            if (state is TrailAppUiState.BluetoothDevice && state.runtimeState is BleRuntimeState.Scanning) {
                harness.controller.onLifecycleStop()
                harness.controller.onLifecycleStart()
                harness.controller.close()
            }
        }
        assertTrue(scan.closed)
        assertEquals(BleRuntimeState.Closed, harness.runtime.state)
        assertEquals(1, harness.facade.closeCount)

        val permission = harness(permission = NearbyDevicesPermissionState.MISSING)
        permission.controller.chooseBluetoothDeviceMode()
        assertTrue(permission.controller.beginNearbyDevicesPermissionRequest())
        permission.controller.close()
        permission.permission.currentValue = NearbyDevicesPermissionState.GRANTED
        permission.controller.onNearbyDevicesPermissionResult()
        assertTrue(permission.facade.scans.isEmpty())
        assertEquals(BleRuntimeState.Closed, permission.runtime.state)
        assertEquals(1, permission.facade.closeCount)
    }

    @Test
    fun injectedPreexistingLeaseObserverReentryAndOffOwnerCallsFailClosed() {
        val facade = TestFacade()
        val scheduler = TestScheduler()
        val runtime = BleCompanionRuntime(facade, scheduler)
        runtime.onLifecycleStart()
        runtime.requestScan()
        val inheritedScan = facade.scans.single()
        val verifier = MutableThreadVerifier(true)
        val controller = TrailAppController(
            localController = CompanionAppController(FakeCompanionTransport()),
            bluetoothRuntime = runtime,
            permissionReader = MutablePermissionReader(NearbyDevicesPermissionState.GRANTED),
            authorizationClient = TestAuthorizationClient(),
            authorizationScheduler = scheduler,
            threadVerifier = verifier,
            bluetoothFacadeCloseable = facade,
        )
        assertTrue(inheritedScan.closed)
        assertEquals(TrailAppUiState.ChooseMode, controller.state)

        controller.observe { state ->
            if (state is TrailAppUiState.LocalTest) controller.chooseBluetoothDeviceMode()
        }
        controller.chooseLocalTestMode()
        assertIs<TrailAppUiState.LocalTest>(controller.state)

        verifier.allowed = false
        assertFailsWith<IllegalStateException> { controller.returnToModeChoice() }
        assertIs<TrailAppUiState.LocalTest>(controller.state)
        verifier.allowed = true
        controller.close()
        assertEquals(1, facade.closeCount)
    }

    @Test
    fun deviceIssuedClaimMustBePendingThenExactAcceptedBeforeGattStarts() {
        val harness = harness()
        val claim = beginClaim(harness, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        assertTrue(claim.started)
        assertTrue(harness.facade.connections.isEmpty())
        assertIs<DeviceAuthorizationUiState.Starting>(bluetoothState(harness).authorizationState)
        assertTrue(harness.scheduler.leases.isEmpty())

        claim.emit(DeviceAuthorizationClaimEvent.Pending("opaque-claim"))
        assertIs<DeviceAuthorizationUiState.Pending>(bluetoothState(harness).authorizationState)
        assertFalse(bluetoothState(harness).toString().contains("opaque-claim"))
        assertEquals(DEVICE_AUTHORIZATION_CLAIM_TIMEOUT_MILLIS, harness.scheduler.leases.last().delayMillis)

        claim.emit(DeviceAuthorizationClaimEvent.Accepted("opaque-claim"))
        assertTrue(claim.closed)
        assertIs<DeviceAuthorizationUiState.Accepted>(bluetoothState(harness).authorizationState)
        assertIs<BleRuntimeState.Connecting>(bluetoothState(harness).runtimeState)
        assertEquals(1, harness.facade.connections.size)
        harness.controller.close()
    }

    @Test
    fun synchronousStartCallbacksPreserveAuthoritativeTerminalOutcomes() {
        run {
            val harness = harness()
            harness.authorization.eventsOnStart = listOf(
                DeviceAuthorizationClaimEvent.Pending("sync-denied"),
                DeviceAuthorizationClaimEvent.Denied("sync-denied"),
            )
            beginClaim(harness, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
            assertIs<DeviceAuthorizationUiState.Denied>(bluetoothState(harness).authorizationState)
            assertTrue(harness.facade.connections.isEmpty())
            harness.controller.close()
        }
        run {
            val harness = harness()
            harness.authorization.eventsOnStart = listOf(
                DeviceAuthorizationClaimEvent.Pending("sync-accepted"),
                DeviceAuthorizationClaimEvent.Accepted("sync-accepted"),
            )
            beginClaim(harness, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
            assertIs<DeviceAuthorizationUiState.Accepted>(bluetoothState(harness).authorizationState)
            assertEquals(1, harness.facade.connections.size)
            harness.controller.close()
        }
        run {
            val harness = harness()
            harness.authorization.eventsOnStart = listOf(DeviceAuthorizationClaimEvent.Unavailable("sync-local"))
            beginClaim(harness, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
            assertIs<DeviceAuthorizationUiState.Unavailable>(bluetoothState(harness).authorizationState)
            assertTrue(harness.facade.connections.isEmpty())
            harness.controller.close()
        }
    }

    @Test
    fun localTransportOutcomesAreVisibleAndNeverFallBackToFakeMode() {
        val observed = mutableListOf<TrailAppUiState>()
        val unavailable = harness()
        unavailable.controller.observe(observed::add)
        val unavailableClaim = beginClaim(unavailable, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        unavailableClaim.emit(DeviceAuthorizationClaimEvent.Unavailable("local-unavailable"))
        assertIs<DeviceAuthorizationUiState.Unavailable>(bluetoothState(unavailable).authorizationState)
        assertTrue(observed.any {
            (it as? TrailAppUiState.BluetoothDevice)?.authorizationState is DeviceAuthorizationUiState.Unavailable
        })
        assertFalse(unavailable.controller.state is TrailAppUiState.LocalTest)
        unavailable.controller.close()

        val unsupported = harness()
        val unsupportedClaim = beginClaim(unsupported, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        unsupportedClaim.emit(DeviceAuthorizationClaimEvent.Unsupported("local-unsupported"))
        assertIs<DeviceAuthorizationUiState.Unsupported>(bluetoothState(unsupported).authorizationState)
        assertFalse(unsupported.controller.state is TrailAppUiState.LocalTest)
        unsupported.controller.close()

        val unknown = harness()
        val unknownClaim = beginClaim(unknown, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        unknownClaim.emit(DeviceAuthorizationClaimEvent.Pending("local-unknown"))
        unknownClaim.emit(DeviceAuthorizationClaimEvent.AuthorityUnknown("local-unknown"))
        assertIs<DeviceAuthorizationUiState.AuthorityUnknown>(bluetoothState(unknown).authorizationState)
        assertFalse(unknown.controller.state is TrailAppUiState.LocalTest)
        unknown.controller.close()
    }

    @Test
    fun runtimeBackedProtectedFailuresReachExplicitControllerStates() {
        run {
            val harness = runtimeBackedHarness()
            val gatt = beginRuntimeClaim(harness)
            gatt.emit(BleGattEvent.ProfileReady)
            gatt.emit(BleGattEvent.Failed(BleGattFailure.SECURITY_REJECTED))
            assertIs<DeviceAuthorizationUiState.Unavailable>(runtimeBluetoothState(harness).authorizationState)
            assertFalse(harness.controller.state is TrailAppUiState.LocalTest)
            harness.controller.close()
        }
        run {
            val harness = runtimeBackedHarness()
            val gatt = beginRuntimeClaim(harness)
            gatt.emit(BleGattEvent.ProfileReady)
            gatt.emit(BleGattEvent.ProtectedProtocolInfoRead(protocolInfoBytes()))
            assertIs<DeviceAuthorizationUiState.Unsupported>(runtimeBluetoothState(harness).authorizationState)
            assertFalse(harness.controller.state is TrailAppUiState.LocalTest)
            harness.controller.close()
        }
        run {
            val harness = runtimeBackedHarness()
            val observed = mutableListOf<TrailAppUiState>()
            harness.controller.observe(observed::add)
            val gatt = beginRuntimeClaim(harness)
            gatt.emit(BleGattEvent.ProfileReady)
            gatt.emit(BleGattEvent.ProtectedProtocolInfoRead(authorizationProtocolInfoBytes()))
            gatt.emit(BleGattEvent.MtuChanged(151))
            gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
            gatt.emit(BleGattEvent.StreamIndication(authorizationPendingEnvelope()))
            gatt.emit(BleGattEvent.Disconnected)
            assertIs<DeviceAuthorizationUiState.AuthorityUnknown>(runtimeBluetoothState(harness).authorizationState)
            assertTrue(observed.any {
                (it as? TrailAppUiState.BluetoothDevice)?.authorizationState is
                    DeviceAuthorizationUiState.AuthorityUnknown
            })
            assertEquals(1, harness.facade.connections.size)
            assertFalse(harness.controller.state is TrailAppUiState.LocalTest)
            harness.controller.close()
        }
    }

    @Test
    fun permissionAndLifecycleLossMapStartingUnavailableAndPendingUnknownWithLateCallbacksSuppressed() {
        val starting = harness()
        val startingClaim = beginClaim(starting, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        starting.permission.currentValue = NearbyDevicesPermissionState.MISSING
        starting.controller.refreshPermissionState()
        assertIs<DeviceAuthorizationUiState.Unavailable>(bluetoothState(starting).authorizationState)
        startingClaim.emit(DeviceAuthorizationClaimEvent.Pending("late-starting"))
        assertIs<DeviceAuthorizationUiState.Unavailable>(bluetoothState(starting).authorizationState)
        starting.controller.close()

        val pending = harness()
        val pendingClaim = beginClaim(pending, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        pendingClaim.emit(DeviceAuthorizationClaimEvent.Pending("pending-loss"))
        pending.permission.currentValue = NearbyDevicesPermissionState.MISSING
        pending.controller.refreshPermissionState()
        assertIs<DeviceAuthorizationUiState.AuthorityUnknown>(bluetoothState(pending).authorizationState)
        pendingClaim.emit(DeviceAuthorizationClaimEvent.Accepted("pending-loss"))
        assertIs<DeviceAuthorizationUiState.AuthorityUnknown>(bluetoothState(pending).authorizationState)
        pending.controller.close()

        val stopped = harness()
        val stoppedClaim = beginClaim(stopped, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        stopped.controller.onLifecycleStop()
        assertEquals(BleRuntimeState.Inactive, stopped.runtime.state)
        stoppedClaim.emit(DeviceAuthorizationClaimEvent.Unavailable("late-stop"))
        assertEquals(BleRuntimeState.Inactive, stopped.runtime.state)
        stopped.controller.close()
    }

    @Test
    fun deniedMismatchedOversizeAndExpiredClaimsFailClosedWithoutGattOrTokenRetention() {
        val denied = harness()
        val deniedClaim = beginClaim(denied, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        deniedClaim.emit(DeviceAuthorizationClaimEvent.Pending("denied-exact"))
        deniedClaim.emit(DeviceAuthorizationClaimEvent.Denied("denied-exact"))
        assertIs<DeviceAuthorizationUiState.Denied>(bluetoothState(denied).authorizationState)
        assertTrue(denied.facade.connections.isEmpty())
        denied.controller.close()

        val mismatch = harness()
        val mismatchClaim = beginClaim(mismatch, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        mismatchClaim.emit(DeviceAuthorizationClaimEvent.Pending("claim-a"))
        mismatchClaim.emit(DeviceAuthorizationClaimEvent.Denied("claim-b"))
        assertIs<DeviceAuthorizationUiState.InvalidResult>(bluetoothState(mismatch).authorizationState)
        assertTrue(INVALID_AUTHORIZATION_RESULT_PUBLIC_TEXT.contains("authority is unknown"))
        assertTrue(mismatch.facade.connections.isEmpty())
        mismatch.controller.close()

        val oversize = harness()
        oversize.authorization.eventsOnStart = listOf(
            DeviceAuthorizationClaimEvent.Pending("x".repeat(MAX_DEVICE_AUTHORIZATION_TOKEN_CHARS + 1)),
        )
        beginClaim(oversize, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        assertIs<DeviceAuthorizationUiState.InvalidResult>(bluetoothState(oversize).authorizationState)
        assertTrue(oversize.authorization.leases.single().closed)
        assertTrue(oversize.facade.connections.isEmpty())
        oversize.controller.close()

        val asyncOversize = harness()
        val asyncClaim = beginClaim(asyncOversize, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        asyncClaim.emit(DeviceAuthorizationClaimEvent.Pending("bounded-first"))
        asyncClaim.emit(
            DeviceAuthorizationClaimEvent.Accepted("y".repeat(MAX_DEVICE_AUTHORIZATION_TOKEN_CHARS + 1)),
        )
        assertIs<DeviceAuthorizationUiState.InvalidResult>(bluetoothState(asyncOversize).authorizationState)
        assertTrue(asyncOversize.facade.connections.isEmpty())
        asyncOversize.controller.close()

        val expired = harness()
        val expiringClaim = beginClaim(expired, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        expiringClaim.emit(DeviceAuthorizationClaimEvent.Pending("expires"))
        expired.scheduler.leases.last().run()
        assertIs<DeviceAuthorizationUiState.Expired>(bluetoothState(expired).authorizationState)
        assertTrue(EXPIRED_AUTHORIZATION_PUBLIC_TEXT.contains("authority is unknown"))
        assertFalse(EXPIRED_AUTHORIZATION_PUBLIC_TEXT.contains("No phone authority changed"))
        expiringClaim.emit(DeviceAuthorizationClaimEvent.Accepted("expires"))
        assertTrue(expired.facade.connections.isEmpty())
        expired.controller.close()
    }

    @Test
    fun observerStopOrCloseOnStartingCreatesNoClaimLeaseOrTimer() {
        val stopped = harness()
        stopped.controller.observe { state ->
            val authorization = (state as? TrailAppUiState.BluetoothDevice)?.authorizationState
            if (authorization is DeviceAuthorizationUiState.Starting) stopped.controller.onLifecycleStop()
        }
        prepareCandidate(stopped)
        stopped.controller.selectBluetoothDevice(CANDIDATE.endpointToken)
        assertEquals(0, stopped.authorization.createCount)
        assertTrue(stopped.scheduler.leases.isEmpty())
        assertEquals(BleRuntimeState.Inactive, stopped.runtime.state)
        stopped.controller.close()

        val closed = harness()
        closed.controller.observe { state ->
            val authorization = (state as? TrailAppUiState.BluetoothDevice)?.authorizationState
            if (authorization is DeviceAuthorizationUiState.Starting) closed.controller.close()
        }
        prepareCandidate(closed)
        closed.controller.selectBluetoothDevice(CANDIDATE.endpointToken)
        assertEquals(0, closed.authorization.createCount)
        assertTrue(closed.scheduler.leases.isEmpty())
        assertEquals(BleRuntimeState.Closed, closed.runtime.state)
        assertEquals(1, closed.facade.closeCount)
    }

    @Test
    fun claimAndTimerCallbacksDuringObserverDeliveryAreDeferredAndBounded() {
        val accepted = harness()
        val claim = beginClaim(accepted, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        accepted.controller.observe { state ->
            if ((state as? TrailAppUiState.BluetoothDevice)?.authorizationState is DeviceAuthorizationUiState.Pending) {
                claim.emit(DeviceAuthorizationClaimEvent.Accepted("deferred"))
            }
        }
        claim.emit(DeviceAuthorizationClaimEvent.Pending("deferred"))
        assertIs<DeviceAuthorizationUiState.Accepted>(bluetoothState(accepted).authorizationState)
        assertEquals(1, accepted.facade.connections.size)
        accepted.controller.close()

        val timed = harness()
        val timedClaim = beginClaim(timed, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        timed.controller.observe { state ->
            if ((state as? TrailAppUiState.BluetoothDevice)?.authorizationState is DeviceAuthorizationUiState.Pending) {
                timed.scheduler.leases.last().run()
            }
        }
        timedClaim.emit(DeviceAuthorizationClaimEvent.Pending("timed"))
        assertIs<DeviceAuthorizationUiState.Expired>(bluetoothState(timed).authorizationState)
        assertTrue(timed.facade.connections.isEmpty())
        timed.controller.close()

        val oversized = harness()
        val oversizedClaim = beginClaim(oversized, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        oversized.controller.observe { state ->
            if ((state as? TrailAppUiState.BluetoothDevice)?.authorizationState is DeviceAuthorizationUiState.Pending) {
                oversizedClaim.emit(
                    DeviceAuthorizationClaimEvent.Accepted("z".repeat(MAX_DEVICE_AUTHORIZATION_TOKEN_CHARS + 1)),
                )
            }
        }
        oversizedClaim.emit(DeviceAuthorizationClaimEvent.Pending("bounded"))
        assertIs<DeviceAuthorizationUiState.InvalidResult>(bluetoothState(oversized).authorizationState)
        assertTrue(oversized.facade.connections.isEmpty())
        oversized.controller.close()

        val synchronousTimeout = harness()
        synchronousTimeout.scheduler.callbackOnSchedule = true
        val synchronousClaim = beginClaim(synchronousTimeout, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        assertTrue(synchronousTimeout.scheduler.leases.isEmpty())
        synchronousClaim.emit(DeviceAuthorizationClaimEvent.Pending("synchronous"))
        assertIs<DeviceAuthorizationUiState.Expired>(bluetoothState(synchronousTimeout).authorizationState)
        assertTrue(synchronousTimeout.scheduler.leases.last().closed)
        assertTrue(synchronousTimeout.facade.connections.isEmpty())
        synchronousTimeout.controller.close()

        val preStartOverflow = harness()
        preStartOverflow.authorization.eventsOnStart = List(5) {
            DeviceAuthorizationClaimEvent.Pending("prestart-overflow")
        }
        beginClaim(preStartOverflow, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        assertIs<DeviceAuthorizationUiState.InvalidResult>(bluetoothState(preStartOverflow).authorizationState)
        assertTrue(preStartOverflow.facade.connections.isEmpty())
        preStartOverflow.controller.close()

        val observerOverflow = harness()
        val overflowClaim = beginClaim(observerOverflow, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        observerOverflow.controller.observe { state ->
            if ((state as? TrailAppUiState.BluetoothDevice)?.authorizationState is DeviceAuthorizationUiState.Pending) {
                repeat(9) { overflowClaim.emit(DeviceAuthorizationClaimEvent.Accepted("observer-overflow")) }
            }
        }
        overflowClaim.emit(DeviceAuthorizationClaimEvent.Pending("observer-overflow"))
        assertIs<DeviceAuthorizationUiState.InvalidResult>(bluetoothState(observerOverflow).authorizationState)
        assertTrue(observerOverflow.facade.connections.isEmpty())
        observerOverflow.controller.close()
    }

    @Test
    fun modeSwitchPermissionRevocationAndCloseCancelClaimAndSuppressLateResults() {
        val switched = harness()
        val switchedClaim = beginClaim(switched, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        switchedClaim.emit(DeviceAuthorizationClaimEvent.Pending("stale"))
        switched.controller.chooseLocalTestMode()
        assertTrue(switchedClaim.closed)
        switchedClaim.emit(DeviceAuthorizationClaimEvent.Accepted("stale"))
        assertTrue(switched.facade.connections.isEmpty())
        assertIs<TrailAppUiState.LocalTest>(switched.controller.state)
        switched.controller.close()

        val revoked = harness()
        val revokedClaim = beginClaim(revoked, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        revokedClaim.emit(DeviceAuthorizationClaimEvent.Pending("revoked"))
        revoked.permission.currentValue = NearbyDevicesPermissionState.MISSING
        revoked.controller.refreshPermissionState()
        assertTrue(revokedClaim.closed)
        revokedClaim.emit(DeviceAuthorizationClaimEvent.Accepted("revoked"))
        assertTrue(revoked.facade.connections.isEmpty())
        revoked.controller.close()
    }

    @Test
    fun lifecycleStopAndCloseCancelPendingClaimTimerAndSuppressLateAcceptance() {
        val stopped = harness()
        val stoppedClaim = beginClaim(stopped, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        stoppedClaim.emit(DeviceAuthorizationClaimEvent.Pending("stop-pending"))
        val stoppedTimer = stopped.scheduler.leases.last()
        stopped.controller.onLifecycleStop()
        assertTrue(stoppedClaim.closed)
        assertTrue(stoppedTimer.closed)
        assertEquals(BleRuntimeState.Inactive, stopped.runtime.state)
        stoppedClaim.emit(DeviceAuthorizationClaimEvent.Accepted("stop-pending"))
        stoppedTimer.run()
        assertTrue(stopped.facade.connections.isEmpty())
        stopped.controller.close()

        val closed = harness()
        val closedClaim = beginClaim(closed, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        closedClaim.emit(DeviceAuthorizationClaimEvent.Pending("close-pending"))
        val closedTimer = closed.scheduler.leases.last()
        closed.controller.close()
        assertTrue(closedClaim.closed)
        assertTrue(closedTimer.closed)
        assertEquals(BleRuntimeState.Closed, closed.runtime.state)
        assertEquals(1, closed.facade.closeCount)
        closedClaim.emit(DeviceAuthorizationClaimEvent.Accepted("close-pending"))
        closedTimer.run()
        assertTrue(closed.facade.connections.isEmpty())

        val throwing = harness()
        throwing.authorization.throwOnClose = true
        val throwingClaim = beginClaim(throwing, DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        throwingClaim.emit(DeviceAuthorizationClaimEvent.Pending("throw-close"))
        val throwingTimer = throwing.scheduler.leases.last()
        throwing.controller.onLifecycleStop()
        assertTrue(throwingClaim.closeAttempted)
        assertTrue(throwingTimer.closed)
        assertEquals(BleRuntimeState.Inactive, throwing.runtime.state)
        throwing.controller.close()
    }

    @Test
    fun factoryResetConfirmationIsBoundToTheExactReadySessionAndCancelSendsNothing() {
        val harness = harness()
        val gatt = beginConnection(harness)
        makeReady(gatt)

        assertTrue(harness.controller.requestFactoryResetConfirmation())
        val canceledDeadline = harness.scheduler.leases.last()
        assertEquals(FACTORY_RESET_CONFIRMATION_FRESHNESS_MILLIS, canceledDeadline.delayMillis)
        assertTrue(bluetoothState(harness).factoryResetConfirmationVisible)
        harness.controller.cancelFactoryResetConfirmation()
        assertTrue(canceledDeadline.closed)
        assertFalse(bluetoothState(harness).factoryResetConfirmationVisible)
        assertFalse(harness.controller.confirmFactoryReset())
        assertTrue(gatt.writes.isEmpty())

        assertTrue(harness.controller.requestFactoryResetConfirmation())
        val confirmedDeadline = harness.scheduler.leases.last()
        assertTrue(harness.controller.confirmFactoryReset())
        assertTrue(confirmedDeadline.closed)
        assertFalse(bluetoothState(harness).factoryResetConfirmationVisible)
        assertEquals(1, gatt.writes.size)
        assertIs<BleRuntimeState.FactoryResetRequesting>(harness.runtime.state)
        harness.controller.close()
    }

    @Test
    fun factoryResetConfirmationIsInvalidatedWhenTheReadySessionGoesStale() {
        val harness = harness()
        val gatt = beginConnection(harness)
        makeReady(gatt)

        assertTrue(harness.controller.requestFactoryResetConfirmation())
        val staleDeadline = harness.scheduler.leases.last()
        assertTrue(bluetoothState(harness).factoryResetConfirmationVisible)
        gatt.emit(BleGattEvent.Disconnected)

        assertTrue(staleDeadline.closed)
        assertFalse(bluetoothState(harness).factoryResetConfirmationVisible)
        assertFalse(harness.controller.confirmFactoryReset())
        assertTrue(gatt.writes.isEmpty())
        harness.controller.close()
    }

    @Test
    fun factoryResetConfirmationExpiresAndLifecycleStopCannotReplayIt() {
        val expired = harness()
        val expiredGatt = beginConnection(expired)
        makeReady(expiredGatt)

        assertTrue(expired.controller.requestFactoryResetConfirmation())
        val expiry = expired.scheduler.leases.last()
        assertEquals(FACTORY_RESET_CONFIRMATION_FRESHNESS_MILLIS, expiry.delayMillis)
        expiry.run()

        assertFalse(bluetoothState(expired).factoryResetConfirmationVisible)
        assertFalse(expired.controller.confirmFactoryReset())
        assertTrue(expiredGatt.writes.isEmpty())
        expired.controller.close()

        val stopped = harness()
        val stoppedGatt = beginConnection(stopped)
        makeReady(stoppedGatt)
        assertTrue(stopped.controller.requestFactoryResetConfirmation())
        val stoppedDeadline = stopped.scheduler.leases.last()

        stopped.controller.onLifecycleStop()

        assertTrue(stoppedDeadline.closed)
        assertFalse(bluetoothState(stopped).factoryResetConfirmationVisible)
        assertFalse(stopped.controller.confirmFactoryReset())
        assertTrue(stoppedGatt.writes.isEmpty())
        stopped.controller.close()
    }

    @Test
    fun rejectedFactoryResetCanExitWithoutSubmittingAnotherReset() {
        val harness = harness()
        val gatt = beginConnection(harness)
        makeReady(gatt)

        assertTrue(harness.controller.requestFactoryResetConfirmation())
        assertTrue(harness.controller.confirmFactoryReset())
        assertEquals(1, gatt.writes.size)
        gatt.emit(
            BleGattEvent.StreamIndication(
                actionResultEnvelope(
                    sessionNonce = 7,
                    exchangeId = 1,
                    result = CompanionActionResult(
                        kind = CompanionActionKind.FACTORY_RESET,
                        factoryResetReceipt = RESET_RECEIPT,
                        disposition = CompanionActionDisposition.REJECTED,
                        rejectReason = CompanionActionRejectReason.POLICY_DENIED,
                    ),
                ),
            ),
        )
        val rejected = assertIs<BleRuntimeState.FactoryResetNotVerified>(harness.runtime.state)
        assertFalse(rejected.canRetryVerification)

        harness.controller.disconnectBluetoothDevice()

        assertEquals(BleRuntimeState.Idle, harness.runtime.state)
        assertEquals(1, gatt.writes.size)
        assertEquals(null, harness.facade.pendingFactoryResetReceipt)
        harness.controller.close()
    }

    private fun prepareCandidate(harness: Harness) {
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        harness.facade.scans.last().emit(BleScanEvent.Candidate(CANDIDATE))
    }

    private fun beginClaim(
        harness: Harness,
        purpose: DeviceAuthorizationPurpose,
    ): TestAuthorizationLease {
        prepareCandidate(harness)
        when (purpose) {
            DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE ->
                harness.controller.selectBluetoothDevice(CANDIDATE.endpointToken)
        }
        return harness.authorization.leases.last()
    }

    private fun bluetoothState(harness: Harness): TrailAppUiState.BluetoothDevice =
        assertIs(harness.controller.state)

    private fun beginConnection(harness: Harness): TestGattLease {
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        val scan = harness.facade.scans.last()
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        harness.controller.selectBluetoothDevice(CANDIDATE.endpointToken)
        val claim = harness.authorization.leases.last()
        claim.emit(DeviceAuthorizationClaimEvent.Pending("device-claim-1"))
        claim.emit(DeviceAuthorizationClaimEvent.Accepted("device-claim-1"))
        return harness.facade.connections.last().also {
            it.emit(BleGattEvent.GattOpened)
        }
    }

    private fun makeReady(gatt: TestGattLease) {
        gatt.emit(BleGattEvent.SecurityEstablished(SECURE_LINK))
        gatt.emit(BleGattEvent.MtuChanged(151))
        gatt.emit(BleGattEvent.ProtocolInfoRead(protocolInfoBytes()))
        gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
        gatt.emit(BleGattEvent.StreamIndication(snapshotEnvelope(sessionNonce = 7, eventId = 1)))
    }

    private fun harness(
        permission: NearbyDevicesPermissionState = NearbyDevicesPermissionState.GRANTED,
        startLifecycle: Boolean = true,
    ): Harness {
        val facade = TestFacade()
        val scheduler = TestScheduler()
        val runtime = BleCompanionRuntime(facade, scheduler)
        val local = CompanionAppController(FakeCompanionTransport())
        val permissionReader = MutablePermissionReader(permission)
        val authorization = TestAuthorizationClient()
        val controller = TrailAppController(
            localController = local,
            bluetoothRuntime = runtime,
            permissionReader = permissionReader,
            authorizationClient = authorization,
            authorizationScheduler = scheduler,
            bluetoothFacadeCloseable = facade,
        )
        if (startLifecycle) controller.onLifecycleStart()
        return Harness(controller, local, runtime, facade, permissionReader, scheduler, authorization)
    }

    private fun runtimeBackedHarness(): RuntimeHarness {
        val facade = TestFacade()
        val scheduler = TestScheduler()
        val runtime = BleCompanionRuntime(facade, scheduler)
        val permission = MutablePermissionReader(NearbyDevicesPermissionState.GRANTED)
        val controller = TrailAppController(
            localController = CompanionAppController(FakeCompanionTransport()),
            bluetoothRuntime = runtime,
            permissionReader = permission,
            authorizationClient = RuntimeDeviceAuthorizationClaimClient(runtime),
            authorizationScheduler = scheduler,
            bluetoothFacadeCloseable = facade,
        )
        controller.onLifecycleStart()
        return RuntimeHarness(controller, runtime, facade)
    }

    private fun beginRuntimeClaim(harness: RuntimeHarness): TestGattLease {
        harness.controller.chooseBluetoothDeviceMode()
        harness.controller.scanBluetoothDevices()
        harness.facade.scans.last().emit(BleScanEvent.Candidate(CANDIDATE))
        harness.controller.selectBluetoothDevice(CANDIDATE.endpointToken)
        return harness.facade.connections.single()
    }

    private fun runtimeBluetoothState(harness: RuntimeHarness): TrailAppUiState.BluetoothDevice =
        assertIs(harness.controller.state)

    private data class Harness(
        val controller: TrailAppController,
        val local: CompanionAppController,
        val runtime: BleCompanionRuntime,
        val facade: TestFacade,
        val permission: MutablePermissionReader,
        val scheduler: TestScheduler,
        val authorization: TestAuthorizationClient,
    )

    private data class RuntimeHarness(
        val controller: TrailAppController,
        val runtime: BleCompanionRuntime,
        val facade: TestFacade,
    )

    private class MutablePermissionReader(var currentValue: NearbyDevicesPermissionState) :
        NearbyDevicesPermissionReader {
        override fun current(): NearbyDevicesPermissionState = currentValue
    }

    private class MutableThreadVerifier(var allowed: Boolean) : BleRuntimeThreadVerifier {
        override fun isOwnerThread(): Boolean = allowed
    }

    private class TestFacade : AndroidBluetoothFacade, AutoCloseable {
        var preflight = BlePreflight()
        val scans = mutableListOf<TestScanLease>()
        val connections = mutableListOf<TestGattLease>()
        var pendingFactoryResetReceipt: ULong? = null
        var closeCount = 0

        override fun preflight(): BlePreflight = preflight
        override fun stageFactoryResetReceipt(): ULong? {
            if (pendingFactoryResetReceipt != null) return null
            return 0x1122334455667788uL.also { pendingFactoryResetReceipt = it }
        }
        override fun clearPendingFactoryResetReceipt(receipt: ULong): Boolean {
            if (pendingFactoryResetReceipt != receipt) return false
            pendingFactoryResetReceipt = null
            return true
        }
        override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease =
            TestScanLease(observer).also(scans::add)
        override fun createConnection(
            endpointToken: String,
            purpose: BleConnectionPurpose,
            observer: (BleGattEvent) -> Unit,
        ): BleGattLease =
            TestGattLease(endpointToken, observer).also(connections::add)
        override fun close() {
            closeCount += 1
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
        private val observer: (BleGattEvent) -> Unit,
    ) : BleGattLease {
        var closed = false
        val writes = mutableListOf<ByteArray>()
        override fun start(): Boolean = true
        override fun requestMtu(mtu: Int): Boolean = true
        override fun readProtocolInfo(): Boolean = true
        override fun subscribeStreamIndications(): Boolean = true
        override fun writeCommandWithResponse(value: ByteArray): Boolean = true.also { writes += value.copyOf() }
        override fun close() { closed = true }
        fun emit(event: BleGattEvent) = observer(event)
    }

    private class TestScheduler : BleRuntimeScheduler {
        val leases = mutableListOf<TestTimerLease>()
        var callbackOnSchedule = false
        override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease =
            TestTimerLease(delayMillis, callback).also {
                leases.add(it)
                if (callbackOnSchedule) callback()
            }
    }

    private class TestTimerLease(val delayMillis: Long, private val callback: () -> Unit) : BleReconnectLease {
        var closed = false
        override fun close() { closed = true }
        fun run() { if (!closed) callback() }
    }

    private class TestAuthorizationClient : DeviceAuthorizationClaimClient {
        val leases = mutableListOf<TestAuthorizationLease>()
        var createCount = 0
        var eventsOnStart: List<DeviceAuthorizationClaimEvent> = emptyList()
        var throwOnClose = false
        override fun createClaim(
            endpointToken: String,
            purpose: DeviceAuthorizationPurpose,
            observer: (DeviceAuthorizationClaimEvent) -> Unit,
        ): DeviceAuthorizationClaimLease {
            createCount += 1
            return TestAuthorizationLease(endpointToken, purpose, observer, eventsOnStart, throwOnClose).also(leases::add)
        }
    }

    private class TestAuthorizationLease(
        val endpointToken: String,
        val purpose: DeviceAuthorizationPurpose,
        private val observer: (DeviceAuthorizationClaimEvent) -> Unit,
        private val eventsOnStart: List<DeviceAuthorizationClaimEvent>,
        private val throwOnClose: Boolean,
    ) : DeviceAuthorizationClaimLease {
        var started = false
        var closed = false
        var closeAttempted = false
        override fun start(): Boolean {
            started = true
            eventsOnStart.forEach(observer)
            return true
        }
        override fun close() {
            closeAttempted = true
            if (throwOnClose) throw IllegalStateException("test close failure")
            closed = true
        }
        fun emit(event: DeviceAuthorizationClaimEvent) = observer(event)
    }

    private class TestLifecycleOwner : LifecycleOwner {
        val registry = LifecycleRegistry.createUnsafe(this)
        override val lifecycle: Lifecycle get() = registry
    }

    companion object {
        private val CANDIDATE = BleDiscoveredCompanion("opaque-device", "Nearby compatible device 1")
        private val SECURE_LINK = BleSecurityEvidence(true, true, true)
        private val RESET_RECEIPT = 0x1122334455667788uL

        private fun protocolInfoBytes(): ByteArray = CompanionProtocolCodec.encodeProtocolInfo(
            CompanionProtocolInfo(capabilities = REQUIRED_ACTION_CAPABILITIES),
        ).value!!

        private fun authorizationProtocolInfoBytes(): ByteArray = CompanionAuthorizationProtocolInfoCodec.encode(
            CompanionAuthorizationProtocolInfo(provisionalSessionNonce = 0x11223344),
        ).value!!

        private fun authorizationPendingEnvelope(): ByteArray = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS,
                sessionNonce = 0x11223344,
                exchangeId = 1,
                payload = CompanionAuthorizationWireCodec.encodeClaimStatus(
                    CompanionAuthorizationClaimStatus(
                        purpose = CompanionAuthorizationPurpose.AUTHORIZE_CONTROLLER,
                        state = CompanionAuthorizationClaimState.PENDING,
                        correlation = CompanionAuthorizationCorrelation(ByteArray(16) { (it + 1).toByte() }),
                    ),
                ).value!!,
            ),
        ).value!!

        private fun snapshotEnvelope(sessionNonce: Long, eventId: Long): ByteArray = CompanionProtocolCodec.encodeFragment(
            CompanionFragment(
                kind = CompanionFrameKind.SNAPSHOT,
                sessionNonce = sessionNonce,
                exchangeId = eventId,
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
    }
}
