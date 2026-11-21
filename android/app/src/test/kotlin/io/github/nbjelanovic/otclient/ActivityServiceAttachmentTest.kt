package io.github.nbjelanovic.otclient

import java.io.File
import io.github.nbjelanovic.otprotocol.*
import kotlin.test.*

/** Real controller/runtime/owner composition; Android binding and GATT I/O are controlled host seams. */
class ActivityServiceAttachmentTest {
    @Test
    fun missingLocalControllerCannotInterruptTheBluetoothSession() {
        val service = ServiceGraph()
        val activity = service.activity(localController = null)
        service.establishReady(activity)
        val before = activity.state
        val operations = service.operationCounts()

        activity.chooseLocalTestMode()
        activity.chooseLocalDevice()

        assertEquals(before, activity.state)
        assertEquals(operations, service.operationCounts())
        assertTrue(service.running)
        activity.close()
        service.owner.close()
    }

    @Test
    fun activityReopenRecoversAnExistingOwnerStartedWithBluetoothOff() {
        for (hasSavedBoard in listOf(false, true)) {
            val facade = Facade().apply {
                blocker = BleRuntimeBlock.BLUETOOTH_DISABLED
                allowReturningOwnerScan = hasSavedBoard
            }
            val service = ServiceGraph(facade = facade)
            val first = service.activity()
            first.onLifecycleStart()
            first.chooseBluetoothDeviceMode()
            first.startBluetoothService()
            assertEquals(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_DISABLED), bluetooth(first).runtimeState)
            first.onLifecycleStop()
            first.close()
            assertTrue(service.running)
            facade.blocker = null

            // onResume may finish before Android delivers the existing binding callback.
            val connector = Connector(service).apply { delayed = true }
            val reopened = service.activity(connector)
            reopened.onLifecycleStart()
            reopened.refreshPermissionState()
            assertEquals(TrailAppUiState.ChooseMode, reopened.state)
            connector.callbacks.single()(ConnectedDeviceServiceConnection.Connected(service.port))

            val expected = if (hasSavedBoard) BleRuntimeState.FindingReturningOwner else BleRuntimeState.Idle
            assertEquals(expected, bluetooth(reopened).runtimeState)
            reopened.refreshPermissionState()
            assertEquals(expected, service.runtime.state)
            assertEquals(if (hasSavedBoard) 1 else 0, facade.returningOwnerScans.size)
            assertTrue(facade.scans.isEmpty())
            assertTrue(facade.connections.isEmpty())
            assertEquals(0, service.authorization.claims)
            assertEquals(1, service.starts)
            reopened.close()
            service.owner.close()
        }
    }

    @Test
    fun retryDoesNotUseServicePermissionSampleTakenBeforeBluetoothPreflight() {
        val permissions = Permissions(NearbyDevicesPermissionState.MISSING)
        val facade = Facade().apply {
            blocker = BleRuntimeBlock.BLUETOOTH_DISABLED
            allowReturningOwnerScan = true
            atNextPreflight = { permissions.value = NearbyDevicesPermissionState.GRANTED }
        }
        // Model permission changing between the service controller's read and platform preflight.
        val service = ServiceGraph(permissions = permissions, facade = facade)
        val activity = service.activity()
        activity.onLifecycleStart()
        activity.chooseBluetoothDeviceMode()
        activity.startBluetoothService()
        assertEquals(BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_DISABLED), bluetooth(activity).runtimeState)
        assertEquals(NearbyDevicesPermissionState.GRANTED, bluetooth(activity).permissionState)
        facade.blocker = null

        activity.retryBluetoothDiscovery()

        assertEquals(BleRuntimeState.FindingReturningOwner, bluetooth(activity).runtimeState)
        assertEquals(1, facade.returningOwnerScans.size)
        assertTrue(facade.scans.isEmpty())
        assertTrue(facade.connections.isEmpty())
        assertEquals(0, service.authorization.claims)
        activity.close()
        service.owner.close()
    }

    @Test
    fun explicitBluetoothRetryReachesTheOwnerWithoutChangingDiscoveryRoutes() {
        for (returningOwner in listOf(false, true)) {
            val service = ServiceGraph()
            service.facade.allowReturningOwnerScan = returningOwner
            service.runtime.onLifecycleStop()
            service.facade.blocker = BleRuntimeBlock.BLUETOOTH_DISABLED
            service.runtime.onLifecycleStart()
            if (!returningOwner) service.controller.scanBluetoothDevices()
            val activity = service.activity()
            activity.onLifecycleStart()
            activity.chooseBluetoothDeviceMode()
            activity.startBluetoothService()
            assertIs<BleRuntimeState.Blocked>(bluetooth(activity).runtimeState)

            activity.retryBluetoothDiscovery()
            assertTrue(service.facade.scans.isEmpty())
            assertTrue(service.facade.returningOwnerScans.isEmpty())
            service.facade.blocker = null
            activity.retryBluetoothDiscovery()

            assertEquals(if (returningOwner) 1 else 0, service.facade.returningOwnerScans.size)
            assertEquals(if (returningOwner) 0 else 1, service.facade.scans.size)
            if (returningOwner) assertIs<BleRuntimeState.FindingReturningOwner>(service.runtime.state)
            else assertIs<BleRuntimeState.Scanning>(service.runtime.state)
            assertEquals(0, service.counted.scans)
            assertEquals(0, service.authorization.claims)
            assertTrue(service.facade.connections.isEmpty())
            activity.retryBluetoothDiscovery()
            assertEquals(1, service.facade.scans.size + service.facade.returningOwnerScans.size)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun explicitBluetoothRetryIsRefusedAfterPermissionLossOrOwnerClosure() {
        val service = ServiceGraph()
        service.runtime.onLifecycleStop()
        service.facade.allowReturningOwnerScan = true
        service.facade.blocker = BleRuntimeBlock.BLUETOOTH_DISABLED
        service.runtime.onLifecycleStart()
        val activity = service.activity()
        activity.onLifecycleStart()
        activity.chooseBluetoothDeviceMode()
        activity.startBluetoothService()
        service.permissions.value = NearbyDevicesPermissionState.MISSING
        activity.refreshPermissionState()
        service.facade.blocker = null

        activity.retryBluetoothDiscovery()
        assertTrue(service.facade.returningOwnerScans.isEmpty())
        assertTrue(service.facade.scans.isEmpty())
        service.owner.close()
        service.permissions.value = NearbyDevicesPermissionState.GRANTED
        service.owner.retryDiscovery()
        assertTrue(service.facade.returningOwnerScans.isEmpty())
        assertTrue(service.facade.scans.isEmpty())
        activity.close()
    }

    @Test
    fun pendingExistingLeaseCannotSuppressOneExplicitStartOrAcceptRetiredCallbacks() {
        val service = returningService()
        val ready = assertIs<BleRuntimeState.Ready>(service.owner.state.runtimeState)
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        assertEquals(1, connector.existingRequests)
        assertEquals(0, connector.closedBindings)
        assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, bluetooth(activity).serviceState)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        val retired = connector.callbacks.single()

        activity.startBluetoothService()

        assertEquals(before.first() + 1, service.starts)
        assertEquals(1, connector.closedBindings)
        assertEquals(1, connector.startupBindings)
        assertEquals(ConnectedDeviceServiceUiState.STARTING, bluetooth(activity).serviceState)
        val pendingExplicit = activity.state
        retired(ConnectedDeviceServiceConnection.Connected(service.port))
        retired(ConnectedDeviceServiceConnection.Disconnected)
        assertEquals(pendingExplicit, activity.state)
        activity.startBluetoothService()
        assertEquals(before.first() + 1, service.starts)
        connector.callbacks.last()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertSame(ready.session, assertIs<BleRuntimeState.Ready>(bluetooth(activity).runtimeState).session)
        assertEquals(ConnectedDeviceServiceUiState.RUNNING, bluetooth(activity).serviceState)
        assertEquals(before.drop(1), service.operationCounts().drop(1))
        activity.startBluetoothService()
        assertEquals(before.first() + 1, service.starts)
        activity.close()
        service.owner.close()
    }

    @Test
    fun acceptedPendingLeaseWithoutCurrentOwnerCanStartOnceWithoutScanOrClaim() {
        val service = ServiceGraph()
        val before = service.operationCounts()
        // Model a platform-accepted lease with no owner callback, not observed service authority.
        val connector = Connector(service).apply { delayed = true; acceptPendingWithoutOwner = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        val retired = connector.callbacks.single()
        assertEquals(0, service.starts)
        assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, bluetooth(activity).serviceState)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        activity.startBluetoothService()
        activity.startBluetoothService()
        assertEquals(1, service.starts)
        assertEquals(1, connector.startupBindings)
        assertEquals(1, connector.closedBindings)
        retired(ConnectedDeviceServiceConnection.Connected(service.port))
        assertEquals(ConnectedDeviceServiceUiState.STARTING, bluetooth(activity).serviceState)
        connector.callbacks.last()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.RUNNING, bluetooth(activity).serviceState)
        assertEquals(before.drop(1), service.operationCounts().drop(1))
        activity.close()
        service.owner.close()
    }

    @Test
    fun pendingLeaseCleanupCannotReenterStartOrLifecycleAttachmentOrAcceptStaleCallbacks() {
        val service = returningService()
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        val retired = connector.callbacks.single()
        connector.duringClose = {
            repeat(3) { activity.startBluetoothService() }
            activity.onLifecycleStart()
            retired(ConnectedDeviceServiceConnection.Connected(service.port))
            retired(ConnectedDeviceServiceConnection.Disconnected)
        }
        activity.startBluetoothService()
        assertEquals(before.first() + 1, service.starts)
        assertEquals(1, connector.existingRequests, "cleanup must not acquire a new passive lease")
        assertEquals(1, connector.startupBindings)
        assertEquals(1, connector.closedBindings)
        assertEquals(ConnectedDeviceServiceUiState.STARTING, bluetooth(activity).serviceState)
        assertEquals(before.drop(1), service.operationCounts().drop(1))
        connector.duringClose = null
        connector.callbacks.last()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertIs<BleRuntimeState.Ready>(bluetooth(activity).runtimeState)
        activity.close()
        service.owner.close()
    }

    @Test
    fun pendingLeaseCleanupStopCloseOrModeExitCancelsExplicitStartup() {
        listOf("stop", "close", "mode", "local").forEach { action ->
            val service = returningService()
            val before = service.operationCounts()
            val connector = Connector(service).apply { delayed = true }
            val activity = service.activity(connector)
            activity.chooseBluetoothDeviceMode()
            activity.onLifecycleStart()
            val retired = connector.callbacks.single()
            connector.duringClose = {
                when (action) {
                    "stop" -> activity.onLifecycleStop()
                    "close" -> activity.close()
                    "mode" -> activity.returnToModeChoice()
                    else -> activity.chooseLocalTestMode()
                }
            }
            activity.startBluetoothService()
            val cancelled = activity.state
            retired(ConnectedDeviceServiceConnection.Connected(service.port))
            retired(ConnectedDeviceServiceConnection.Disconnected)
            activity.startBluetoothService()
            assertEquals(cancelled, activity.state, action)
            assertEquals(before.first(), service.starts, action)
            assertEquals(0, connector.startupBindings, action)
            assertEquals(1, connector.closedBindings, action)
            assertEquals(before[1] + if (action == "mode" || action == "local") 1 else 0, service.stops, action)
            assertEquals(before.drop(2), service.operationCounts().drop(2), action)
            connector.duringClose = null
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun pendingLeaseCleanupUsesFreshPermissionAndNotificationStateWithoutStoppingOwner() {
        listOf("lost", "refresh-lost", "regained", "notification-denied").forEach { change ->
            val service = returningService()
            val before = service.operationCounts()
            val connector = Connector(service).apply { delayed = true }
            val activity = service.activity(connector)
            activity.chooseBluetoothDeviceMode()
            activity.onLifecycleStart()
            connector.duringClose = {
                if (change == "notification-denied") service.notificationState = NotificationPermissionState.DENIED
                else {
                    service.permissions.value = NearbyDevicesPermissionState.MISSING
                    if (change != "lost") activity.refreshPermissionState()
                    if (change == "regained") {
                        service.permissions.value = NearbyDevicesPermissionState.GRANTED
                        activity.refreshPermissionState()
                    }
                }
            }
            activity.startBluetoothService()
            val allowed = change == "regained" || change == "notification-denied"
            assertEquals(before.first() + if (allowed) 1 else 0, service.starts, change)
            assertEquals(if (allowed) 1 else 0, connector.startupBindings, change)
            assertEquals(before.drop(1), service.operationCounts().drop(1), change)
            if (!allowed) {
                assertEquals(NearbyDevicesPermissionState.MISSING, bluetooth(activity).permissionState)
                assertEquals(ConnectedDeviceServiceUiState.START_FAILED, bluetooth(activity).serviceState)
                assertEquals(ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING, bluetooth(activity).serviceFailure)
            } else if (change == "notification-denied") {
                assertEquals(NotificationPermissionState.DENIED, bluetooth(activity).notificationPermissionState)
            }
            connector.duringClose = null
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun missingPermissionRetiresPendingLeaseAndRetryRequiresFreshGrant() {
        val service = returningService()
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        val retired = connector.callbacks.single()
        service.permissions.value = NearbyDevicesPermissionState.MISSING
        activity.startBluetoothService()
        assertEquals(before, service.operationCounts())
        assertEquals(1, connector.closedBindings)
        assertEquals(ConnectedDeviceServiceUiState.START_FAILED, bluetooth(activity).serviceState)
        retired(ConnectedDeviceServiceConnection.Connected(service.port))
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        service.permissions.value = NearbyDevicesPermissionState.GRANTED
        activity.startBluetoothService()
        activity.startBluetoothService()
        assertEquals(before.first() + 1, service.starts)
        assertEquals(1, connector.startupBindings)
        assertEquals(before.drop(1), service.operationCounts().drop(1))
        activity.close()
        service.owner.close()
    }

    @Test
    fun permissionLostDuringStartingPresentationDoesNotSubmitStart() {
        val service = returningService()
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        var revoked = false
        activity.observe { state ->
            if (!revoked && state is TrailAppUiState.BluetoothDevice &&
                state.serviceState == ConnectedDeviceServiceUiState.STARTING) {
                revoked = true
                service.permissions.value = NearbyDevicesPermissionState.MISSING
            }
        }
        activity.startBluetoothService()
        assertTrue(revoked)
        assertEquals(before, service.operationCounts())
        assertEquals(0, connector.startupBindings)
        assertEquals(1, connector.closedBindings)
        assertEquals(ConnectedDeviceServiceUiState.START_FAILED, bluetooth(activity).serviceState)
        assertEquals(NearbyDevicesPermissionState.MISSING, bluetooth(activity).permissionState)
        assertIs<BleRuntimeState.Ready>(service.owner.state.runtimeState)
        service.permissions.value = NearbyDevicesPermissionState.GRANTED
        activity.startBluetoothService()
        assertEquals(before.first() + 1, service.starts)
        activity.close()
        service.owner.close()
    }

    @Test
    fun observedPassiveOwnerIsNeverPreemptedByRepeatedStart() {
        val service = returningService()
        val before = service.operationCounts()
        val activity = service.activity()
        activity.onLifecycleStart()
        val ready = activity.state
        repeat(3) { activity.startBluetoothService() }
        assertEquals(ready, activity.state)
        assertEquals(before, service.operationCounts())
        assertEquals(0, service.connectors.last().startupBindings)
        assertEquals(0, service.connectors.last().closedBindings)
        activity.close()
        service.owner.close()
    }

    @Test
    fun pendingRetirementIgnoresThrowingLeaseCleanupAndStillStartsExactlyOnce() {
        val service = returningService()
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true; duringClose = { error("host cleanup failure") } }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        activity.startBluetoothService()
        activity.startBluetoothService()
        assertEquals(before.first() + 1, service.starts)
        assertEquals(1, connector.startupBindings)
        assertEquals(1, connector.closedBindings)
        assertEquals(before.drop(1), service.operationCounts().drop(1))
        connector.duringClose = null
        activity.close()
        service.owner.close()
    }

    @Test
    fun pendingExplicitStartObservesReplacedCurrentOwnerAndCurrentDisconnect() {
        val service = returningService()
        val before = service.operationCounts()
        val next = ServiceGraph(42L)
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        val retired = connector.callbacks.single()
        service.port.owner = next.owner
        activity.startBluetoothService()
        retired(ConnectedDeviceServiceConnection.Connected(service.port))
        assertEquals(ConnectedDeviceServiceUiState.STARTING, bluetooth(activity).serviceState)
        connector.callbacks.last()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(42L, service.port.generation)
        assertEquals(ConnectedDeviceServiceUiState.RUNNING, bluetooth(activity).serviceState)
        connector.callbacks.last()(ConnectedDeviceServiceConnection.Disconnected)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.START_FAILED, bluetooth(activity).serviceState)
        assertEquals(before.first() + 1, service.starts)
        assertEquals(before[1] + 1, service.stops, "existing explicit-start failure policy")
        assertEquals(before.drop(2), service.operationCounts().drop(2))
        activity.close()
        service.owner.close()
        next.owner.close()
    }

    @Test
    fun pendingExplicitStartCannotPresentClosedOwnerAsReady() {
        val service = returningService()
        service.owner.close()
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.chooseBluetoothDeviceMode()
        activity.onLifecycleStart()
        activity.startBluetoothService()
        connector.callbacks.last()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.START_FAILED, bluetooth(activity).serviceState)
        assertEquals(before.first() + 1, service.starts)
        assertEquals(before[1] + 1, service.stops, "existing explicit-start failure policy")
        assertEquals(before.drop(2), service.operationCounts().drop(2))
        activity.close()
    }

    @Test
    fun productionExistingServiceAttachmentUsesNonCreatingAndroidBinding() {
        val path = "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidConnectedDeviceServiceConnector.kt"
        val sourceFile = File(path).takeIf { it.isFile } ?: File("app", path)
        require(sourceFile.isFile) { "Required production adapter source is missing" }
        val source = sourceFile.readText()
        val existing = source.substringAfter("override fun bindExisting(")
            .substringBefore("private fun bindWithFlags(")
        assertTrue(existing.contains("bindWithFlags(0, observer)"))
        listOf("BIND_AUTO_CREATE", "startForegroundService", "startService", "stopService").forEach {
            assertFalse(existing.contains(it), "passive attachment: $it")
        }
        val binding = source.substringAfter("private fun bindWithFlags(")
            .substringBefore("override fun stopService()")
        assertTrue(binding.contains("appContext.bindService("))
        assertTrue(binding.contains("binding.connection,"))
        assertTrue(binding.contains("flags,"))
        listOf("BIND_AUTO_CREATE", "startForegroundService", "startService", "stopService").forEach {
            assertFalse(binding.contains(it), "binding adapter: $it")
        }
        assertTrue(source.contains("binder as? ConnectedDeviceSessionPort"))
        val explicit = source.substringAfter("override fun bind(").substringBefore("override fun bindExisting(")
        assertTrue(explicit.contains("bindWithFlags(Context.BIND_AUTO_CREATE, observer)"))
    }

    @Test
    fun silentAcceptedExistingBindFallsBackToOwnedDiscoveryAtDeadline() {
        val service = ServiceGraph()
        val connector = Connector(service).apply {
            allowReturningOwner = true
            acceptSilentExistingWithoutOwner = true
        }
        val activity = service.activity(connector)
        service.enableReturningOwnerBond()

        activity.onLifecycleStart()
        assertEquals(1, connector.existingRequests)
        assertEquals(0, connector.returningOwnerBindings)
        val oldCallback = connector.callbacks.single()
        assertEquals(TrailAppUiState.ChooseMode, activity.state)

        service.scheduler.callbacks.toList().forEach { it() }

        assertEquals(1, connector.closedBindings)
        assertEquals(1, connector.returningOwnerBindings)
        assertEquals(1, service.facade.returningOwnerScans.size)
        assertIs<BleRuntimeState.FindingReturningOwner>(bluetooth(activity).runtimeState)
        assertEquals(0, service.starts)
        assertEquals(0, service.stops)
        oldCallback(ConnectedDeviceServiceConnection.Disconnected)
        assertIs<BleRuntimeState.FindingReturningOwner>(bluetooth(activity).runtimeState)
        activity.close()
        service.owner.close()
    }

    @Test
    fun existingAttachmentDeadlineCannotReplaceAConnectedOwner() {
        val service = returningService()
        val ready = assertIs<BleRuntimeState.Ready>(service.owner.state.runtimeState)
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true; allowReturningOwner = true }
        val activity = service.activity(connector)
        activity.onLifecycleStart()
        val deadline = service.scheduler.callbacks.last()
        connector.callbacks.single()(ConnectedDeviceServiceConnection.Connected(service.port))

        deadline()

        assertSame(ready.session, assertIs<BleRuntimeState.Ready>(bluetooth(activity).runtimeState).session)
        assertEquals(before, service.operationCounts())
        assertEquals(0, connector.returningOwnerBindings)
        activity.close()
        service.owner.close()
    }

    @Test
    fun silentExistingDeadlineCannotStartAfterBackgroundOrPermissionLoss() {
        for (background in listOf(false, true)) {
            val service = ServiceGraph()
            val connector = Connector(service).apply {
                allowReturningOwner = true
                acceptSilentExistingWithoutOwner = true
            }
            val activity = service.activity(connector)
            service.enableReturningOwnerBond()
            activity.onLifecycleStart()
            val deadline = service.scheduler.callbacks.single()
            if (background) activity.onLifecycleStop()
            else service.permissions.value = NearbyDevicesPermissionState.MISSING

            deadline()

            assertEquals(TrailAppUiState.ChooseMode, activity.state)
            assertEquals(1, connector.closedBindings)
            assertEquals(0, connector.returningOwnerBindings)
            assertTrue(service.facade.returningOwnerScans.isEmpty())
            assertEquals(0, service.starts)
            assertEquals(0, service.stops)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun coldForegroundBindingUsesOwnedDiscoveryAndProtectedClaimWithoutStartingService() {
        val service = ServiceGraph()
        val connector = Connector(service).apply { allowReturningOwner = true }
        val activity = service.activity(connector)
        service.enableReturningOwnerBond()

        activity.onLifecycleStart()
        assertEquals(1, connector.existingRequests)
        assertEquals(1, connector.returningOwnerBindings)
        assertEquals(0, service.starts)
        assertIs<BleRuntimeState.FindingReturningOwner>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.BOUND_ONLY, bluetooth(activity).serviceState)
        assertTrue(service.facade.scans.isEmpty())
        val scan = service.facade.returningOwnerScans.single()
        scan.emit(BleScanEvent.Candidate(CANDIDATE))
        assertTrue(service.facade.connections.isEmpty())
        scan.emit(BleScanEvent.Complete)
        val gatt = service.facade.connections.single()
        assertEquals(BleConnectionPurpose.EXISTING_OWNER, gatt.purpose)
        gatt.emit(BleGattEvent.ProfileReady)
        gatt.emit(BleGattEvent.Failed(BleGattFailure.AUTHORIZATION_REJECTED))
        assertEquals(
            BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE,
            assertIs<BleRuntimeState.Failed>(bluetooth(activity).runtimeState).reason,
        )
        assertEquals(1, gatt.closes)
        assertEquals(0, service.authorization.claims)
        activity.onLifecycleStop()
        assertEquals(1, connector.closedBindings)
        assertEquals(0, service.stops)
        activity.close()
        service.owner.close()
    }

    @Test
    fun absentBondOrCompletedScanWithoutOwnedBoardLeavesFirstUseAvailable() {
        for (bond in listOf(false, true)) {
            val service = ServiceGraph()
            val connector = Connector(service).apply { allowReturningOwner = true }
            val activity = service.activity(connector)
            service.port.generationOverride = 0L
            if (bond) service.enableReturningOwnerBond()

            activity.onLifecycleStart()
            if (bond) service.facade.returningOwnerScans.single().emit(BleScanEvent.Complete)
            assertEquals(TrailAppUiState.ChooseMode, activity.state)
            assertEquals(1, connector.returningOwnerBindings)
            assertEquals(1, connector.closedBindings)
            assertEquals(0, service.starts)
            assertEquals(0, service.stops)
            assertTrue(service.facade.scans.isEmpty())
            assertTrue(service.facade.connections.isEmpty())

            service.port.generationOverride = null
            activity.chooseBluetoothDeviceMode()
            activity.startBluetoothService()
            assertEquals(1, service.starts)
            assertEquals(1, connector.startupBindings)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun delayedReturningOwnerBindingCannotActivateAfterBackgroundOrPermissionRevocation() {
        for (background in listOf(false, true)) {
            val service = ServiceGraph()
            val connector = Connector(service).apply { allowReturningOwner = true; delayed = true }
            val activity = service.activity(connector)
            service.enableReturningOwnerBond()

            activity.onLifecycleStart()
            val pending = connector.callbacks.single()
            if (background) activity.onLifecycleStop()
            else service.permissions.value = NearbyDevicesPermissionState.MISSING
            pending(ConnectedDeviceServiceConnection.Connected(service.port))

            assertEquals(TrailAppUiState.ChooseMode, activity.state)
            assertEquals(1, connector.closedBindings)
            assertTrue(service.facade.returningOwnerScans.isEmpty())
            assertEquals(0, service.starts)
            assertEquals(0, service.stops)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun explicitDisconnectDoesNotRestartReturningOwnerOnSameActivityForeground() {
        val service = ServiceGraph()
        val connector = Connector(service).apply { allowReturningOwner = true }
        val activity = service.activity(connector)
        service.enableReturningOwnerBond()

        activity.onLifecycleStart()
        assertIs<BleRuntimeState.FindingReturningOwner>(bluetooth(activity).runtimeState)
        activity.disconnectBluetoothDevice()
        assertEquals(TrailAppUiState.ChooseMode, activity.state)
        activity.onLifecycleStop()
        activity.onLifecycleStart()

        assertEquals(TrailAppUiState.ChooseMode, activity.state)
        assertEquals(1, connector.returningOwnerBindings)
        assertEquals(1, service.facade.returningOwnerScans.size)
        assertEquals(0, service.starts)
        activity.close()
        service.owner.close()
    }

    @Test
    fun replacementActivityObservesTheSameLiveReadyOwnerWithoutStartingOrClaimingAgain() {
        val service = ServiceGraph()
        val first = service.activity()
        service.establishReady(first)
        val ready = assertIs<BleRuntimeState.Ready>(service.owner.state.runtimeState)
        val before = service.operationCounts()
        first.onLifecycleStop()
        first.close()

        repeat(3) {
            val replacement = service.activity()
            replacement.onLifecycleStart()
            val state = assertIs<TrailAppUiState.BluetoothDevice>(replacement.state)
            assertEquals(ConnectedDeviceServiceUiState.RUNNING, state.serviceState)
            assertSame(ready.session, assertIs<BleRuntimeState.Ready>(state.runtimeState).session)
            assertEquals(before, service.operationCounts())
            assertEquals(41L, service.port.generation)
            replacement.onLifecycleStop()
            replacement.close()
        }
        assertEquals(before, service.operationCounts())
        service.owner.close()
    }

    @Test
    fun sameActivityForegroundReopenUsesExistingBindingAndNeverCreatesAnAbsentOwner() {
        val service = ServiceGraph()
        val activity = service.activity()
        service.establishReady(activity)
        val ready = assertIs<BleRuntimeState.Ready>(service.owner.state.runtimeState)
        val before = service.operationCounts()
        activity.onLifecycleStop()
        activity.onLifecycleStart()
        assertSame(ready.session, assertIs<BleRuntimeState.Ready>(bluetooth(activity).runtimeState).session)
        assertEquals(before, service.operationCounts())
        assertEquals(1, service.connectors.single().startupBindings)
        activity.onLifecycleStop()
        service.running = false
        activity.onLifecycleStart()
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, bluetooth(activity).serviceState)
        assertEquals(before, service.operationCounts())
        assertEquals(1, service.connectors.single().startupBindings)
        activity.close()
        service.owner.close()
    }

    @Test
    fun delayedOrAbsentReopenNeverReportsRunningBeforeCurrentObservation() {
        listOf(true, false).forEach { present ->
            val service = ServiceGraph()
            val activity = service.activity()
            service.establishReady(activity)
            val before = service.operationCounts()
            activity.onLifecycleStop()
            val observed = mutableListOf<TrailAppUiState>()
            activity.observe(observed::add)
            val connector = service.connectors.single()
            connector.delayed = true
            service.running = present
            activity.onLifecycleStart()
            assertFalse(observed.any { it is TrailAppUiState.BluetoothDevice &&
                (it.serviceState == ConnectedDeviceServiceUiState.RUNNING || it.runtimeState is BleRuntimeState.Ready) },
                "present=$present before current binder observation")
            if (present) {
                connector.callbacks.last()(ConnectedDeviceServiceConnection.Connected(service.port))
                assertIs<BleRuntimeState.Ready>(bluetooth(activity).runtimeState)
                assertEquals(ConnectedDeviceServiceUiState.RUNNING, bluetooth(activity).serviceState)
            } else {
                assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
                assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, bluetooth(activity).serviceState)
            }
            assertEquals(before, service.operationCounts())
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun absentOwnerLeavesOrdinaryExplicitStartupAvailable() {
        val service = ServiceGraph()
        val activity = service.activity()
        activity.onLifecycleStart()
        assertEquals(TrailAppUiState.ChooseMode, activity.state)
        assertEquals(0, service.starts)
        assertEquals(0, service.connectors.single().startupBindings)
        activity.chooseBluetoothDeviceMode()
        assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, bluetooth(activity).serviceState)
        activity.startBluetoothService()
        assertEquals(1, service.starts)
        assertEquals(1, service.connectors.single().startupBindings)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        activity.close()
        service.owner.close()
    }

    @Test
    fun missingPermissionAtEntryDoesNotEvenAttemptAttachment() {
        val service = returningService()
        val before = service.operationCounts()
        service.permissions.value = NearbyDevicesPermissionState.MISSING
        val activity = service.activity()
        activity.onLifecycleStart()
        assertEquals(TrailAppUiState.ChooseMode, activity.state)
        assertEquals(0, service.connectors.last().existingRequests)
        assertEquals(before, service.operationCounts())
        activity.close()
        service.owner.close()
    }

    @Test
    fun revokedPermissionBeforeDelayedBinderCallbackCannotPublishOrStopOwner() {
        val service = returningService()
        val before = service.operationCounts()
        val connector = Connector(service).apply { delayed = true }
        val activity = service.activity(connector)
        activity.onLifecycleStart()
        service.permissions.value = NearbyDevicesPermissionState.MISSING
        connector.callbacks.single()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertEquals(TrailAppUiState.ChooseMode, activity.state)
        assertEquals(before, service.operationCounts())
        assertEquals(1, connector.closedBindings)
        activity.close()
        service.owner.close()
    }

    @Test
    fun rejectedOrThrowingBindingAndSynchronousCallbackThenFailureNeverPublishReady() {
        listOf("null", "throw", "callback-then-null", "callback-then-throw", "disconnected").forEach { failure ->
            val service = returningService()
            val before = service.operationCounts()
            val connector = Connector(service).apply { bindMode = failure }
            val activity = service.activity(connector)
            val observed = mutableListOf<TrailAppUiState>()
            activity.observe(observed::add)
            activity.onLifecycleStart()
            assertEquals(TrailAppUiState.ChooseMode, activity.state, failure)
            assertFalse(observed.any { it is TrailAppUiState.BluetoothDevice && it.runtimeState is BleRuntimeState.Ready }, failure)
            assertEquals(before, service.operationCounts(), failure)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun nullDeadOrUnleasedObservationNeverPublishesReadyAndLeavesOwnerAlive() {
        listOf("null", "throw", "callback-then-null", "callback-then-throw").forEach { failure ->
            val service = returningService()
            val before = service.operationCounts()
            service.port.observeMode = failure
            val activity = service.activity()
            val observed = mutableListOf<TrailAppUiState>()
            activity.observe(observed::add)
            activity.onLifecycleStart()
            assertEquals(TrailAppUiState.ChooseMode, activity.state, failure)
            assertFalse(observed.any { it is TrailAppUiState.BluetoothDevice && it.runtimeState is BleRuntimeState.Ready }, failure)
            assertEquals(before, service.operationCounts(), failure)
            assertIs<BleRuntimeState.Ready>(service.owner.state.runtimeState)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun closedOwnerWithPositiveGenerationIsNotRunning() {
        val service = returningService()
        service.owner.close()
        val before = service.operationCounts()
        assertEquals(41L, service.port.generation)
        val activity = service.activity()
        activity.onLifecycleStart()
        assertEquals(TrailAppUiState.ChooseMode, activity.state)
        assertEquals(before, service.operationCounts())
        activity.close()
    }

    @Test
    fun zeroNegativeOrDeadGenerationCannotAttachToCachedReady() {
        listOf(0L, -1L, null).forEach { generation ->
            val service = returningService()
            val before = service.operationCounts()
            service.port.generationOverride = generation
            service.port.deadGeneration = generation == null
            val activity = service.activity()
            activity.onLifecycleStart()
            assertEquals(TrailAppUiState.ChooseMode, activity.state)
            assertEquals(before, service.operationCounts())
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun permissionOrGenerationChangedDuringObservationAdmissionRejectsItsSnapshot() {
        listOf("permission", "generation").forEach { change ->
            val service = returningService()
            val before = service.operationCounts()
            service.port.afterObserve = {
                if (change == "permission") service.permissions.value = NearbyDevicesPermissionState.MISSING
                else service.port.generationOverride = 42L
            }
            val activity = service.activity()
            val observed = mutableListOf<TrailAppUiState>()
            activity.observe(observed::add)
            activity.onLifecycleStart()
            assertEquals(TrailAppUiState.ChooseMode, activity.state, change)
            assertFalse(observed.any { it is TrailAppUiState.BluetoothDevice && it.runtimeState is BleRuntimeState.Ready }, change)
            assertEquals(before, service.operationCounts(), change)
            activity.close()
            service.owner.close()
        }
    }

    @Test
    fun actualCurrentRuntimeDisconnectIsObservedWithoutRestartOrClaim() {
        val service = returningService()
        val before = service.operationCounts()
        val activity = service.activity()
        activity.onLifecycleStart()
        service.controller.disconnectBluetoothDevice()
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.RUNNING, bluetooth(activity).serviceState)
        assertEquals(before.take(9), service.operationCounts().take(9))
        assertEquals(before.last() + 1, service.operationCounts().last())
        activity.close()
        service.owner.close()
    }

    @Test
    fun deadCurrentBindingDetachesOnlyAndObsoleteCallbacksCannotReviveIt() {
        val service = returningService()
        val before = service.operationCounts()
        val activity = service.activity()
        activity.onLifecycleStart()
        val connector = service.connectors.last()
        val retired = service.port.callbacks.last()
        connector.callbacks.single()(ConnectedDeviceServiceConnection.Disconnected)
        val detached = activity.state
        retired(service.owner.state)
        connector.callbacks.single()(ConnectedDeviceServiceConnection.Connected(service.port))
        assertEquals(detached, activity.state)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.START_REQUIRED, bluetooth(activity).serviceState)
        assertEquals(before, service.operationCounts())
        activity.close()
        service.owner.close()
    }

    @Test
    fun obsoleteSameGenerationCallbacksAndCallbacksDuringCleanupCannotContaminateReattachment() {
        val service = returningService()
        val before = service.operationCounts()
        val activity = service.activity()
        activity.onLifecycleStart()
        val connector = service.connectors.last()
        val retired = service.port.callbacks.last()
        service.port.duringClose = { retired(service.owner.state.copy(runtimeState = BleRuntimeState.Idle)) }
        connector.duringClose = { connector.callbacks.first()(ConnectedDeviceServiceConnection.Disconnected) }
        activity.onLifecycleStop()
        activity.onLifecycleStart()
        val ready = activity.state
        retired(service.owner.state.copy(runtimeState = BleRuntimeState.Scanning(emptyList())))
        connector.callbacks.first()(ConnectedDeviceServiceConnection.Disconnected)
        assertEquals(ready, activity.state)
        assertIs<BleRuntimeState.Ready>(bluetooth(activity).runtimeState)
        assertEquals(before, service.operationCounts())
        activity.close()
        service.owner.close()
    }

    @Test
    fun duplicateConnectedPortRetiresOldObservationEvenWithIdenticalOwnerGeneration() {
        val service = returningService()
        val activity = service.activity()
        activity.onLifecycleStart()
        val retired = service.port.callbacks.last()
        service.connectors.last().callbacks.single()(ConnectedDeviceServiceConnection.Connected(service.port))
        val current = activity.state
        retired(service.owner.state.copy(runtimeState = BleRuntimeState.Scanning(emptyList())))
        assertEquals(current, activity.state)
        activity.close()
        service.owner.close()
    }

    @Test
    fun sameBinderOwnerReplacementReobservesActualNewOwnerAndRejectsRetiredReady() {
        val service = returningService()
        val before = service.operationCounts()
        val next = ServiceGraph(42L)
        val activity = service.activity()
        activity.onLifecycleStart()
        val retired = service.port.callbacks.last()
        val oldReady = service.owner.state
        service.port.owner = next.owner
        retired(oldReady)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(ConnectedDeviceServiceUiState.RUNNING, bluetooth(activity).serviceState)
        val current = activity.state
        retired(oldReady)
        assertEquals(current, activity.state)
        assertEquals(before, service.operationCounts())
        activity.close()
        service.owner.close()
        next.owner.close()
    }

    @Test
    fun currentPermissionRevocationDetachesAndLateCallbacksCannotRestoreAuthority() {
        val service = returningService()
        val before = service.operationCounts()
        val activity = service.activity()
        activity.onLifecycleStart()
        val callback = service.port.callbacks.last()
        service.permissions.value = NearbyDevicesPermissionState.MISSING
        callback(service.owner.state)
        assertIs<BleRuntimeState.Idle>(bluetooth(activity).runtimeState)
        assertEquals(NearbyDevicesPermissionState.MISSING, bluetooth(activity).permissionState)
        val detached = activity.state
        callback(service.owner.state)
        assertEquals(detached, activity.state)
        assertEquals(before, service.operationCounts())
        activity.close()
        service.owner.close()
    }

    @Test
    fun synchronousPublicationObserverStopOrCloseReleasesOnlyObservation() {
        listOf("stop", "close").forEach { action ->
            val service = returningService()
            val before = service.operationCounts()
            val activity = service.activity()
            activity.observe { state ->
                if (state is TrailAppUiState.BluetoothDevice && state.serviceState == ConnectedDeviceServiceUiState.RUNNING) {
                    if (action == "stop") activity.onLifecycleStop() else activity.close()
                }
            }
            activity.onLifecycleStart()
            assertEquals(1, service.connectors.last().closedBindings, action)
            assertEquals(before, service.operationCounts(), action)
            activity.close()
            service.owner.close()
        }
    }

    private fun bluetooth(activity: TrailActivityController) = assertIs<TrailAppUiState.BluetoothDevice>(activity.state)

    private fun returningService(): ServiceGraph = ServiceGraph().also { service ->
        val first = service.activity()
        service.establishReady(first)
        first.onLifecycleStop()
        first.close()
    }

    private class Permissions(var value: NearbyDevicesPermissionState = NearbyDevicesPermissionState.GRANTED) :
        NearbyDevicesPermissionReader {
        override fun current() = value
    }

    private class ServiceGraph(
        generation: Long = 41L,
        val permissions: Permissions = Permissions(),
        val facade: Facade = Facade(),
    ) {
        val scheduler = Scheduler()
        val runtime = BleCompanionRuntime(facade, scheduler)
        val authorization = Authorization()
        val controller = TrailAppController(
            localController = CompanionAppController(FakeCompanionTransport()),
            bluetoothRuntime = runtime,
            permissionReader = permissions,
            authorizationClient = authorization,
            authorizationScheduler = scheduler,
            bluetoothFacadeCloseable = facade,
        )
        val counted = CountedController(controller)
        val owner = ConnectedDeviceSessionOwner(generation, counted)
        val port = Port(owner)
        var running = false
        var starts = 0
        var stops = 0
        var notificationState = NotificationPermissionState.GRANTED
        val connectors = mutableListOf<Connector>()

        fun activity(
            connector: Connector = Connector(this),
            localController: CompanionAppController? = CompanionAppController(FakeCompanionTransport()),
        ): TrailActivityController {
            connectors += connector
            return TrailActivityController(
                localController, permissions,
                NotificationPermissionReader { notificationState }, connector, scheduler,
            )
        }

        fun establishReady(activity: TrailActivityController) {
            activity.onLifecycleStart()
            activity.chooseBluetoothDeviceMode()
            activity.startBluetoothService()
            activity.scanBluetoothDevices()
            facade.scans.single().emit(BleScanEvent.Candidate(CANDIDATE))
            activity.selectBluetoothDevice(CANDIDATE.endpointToken)
            authorization.observer!!(DeviceAuthorizationClaimEvent.Pending("host-claim"))
            authorization.observer!!(DeviceAuthorizationClaimEvent.Accepted("host-claim"))
            val gatt = facade.connections.single()
            gatt.emit(BleGattEvent.GattOpened)
            gatt.emit(BleGattEvent.SecurityEstablished(BleSecurityEvidence(true, true, true)))
            gatt.emit(BleGattEvent.MtuChanged(151))
            gatt.emit(BleGattEvent.ProtocolInfoRead(CompanionProtocolCodec.encodeProtocolInfo(
                CompanionProtocolInfo(capabilities = REQUIRED_ACTION_CAPABILITIES),
            ).value!!))
            gatt.emit(BleGattEvent.StreamIndicationsSubscribed)
            gatt.emit(BleGattEvent.StreamIndication(CompanionProtocolCodec.encodeFragment(
                CompanionFragment(CompanionFrameKind.SNAPSHOT, 7, 1,
                    payload = CompanionSemanticCodec.encodeStatusSnapshot(CompanionStatusSnapshot(
                        1, CompanionRadioState.READY, CompanionGnssState.SEARCHING,
                        CompanionPowerState.NORMAL, CompanionPositionSharingState.STOPPED, 0,
                    )).value!!),
            ).value!!))
            assertIs<BleRuntimeState.Ready>(runtime.state)
            assertIs<BleRuntimeState.Ready>(assertIs<TrailAppUiState.BluetoothDevice>(activity.state).runtimeState)
        }

        fun enableReturningOwnerBond() {
            port.generationOverride = 0L
            port.returningOwnerActivation = {
                runtime.onLifecycleStop()
                facade.allowReturningOwnerScan = true
                port.boundOnly = true
                runtime.onLifecycleStart()
                port.generationOverride = null
                true
            }
        }

        fun operationCounts() = listOf(starts, stops, counted.scans, counted.claims,
            counted.disconnects, counted.closes, facade.scans.size, facade.connections.size,
            authorization.claims, facade.connections.sumOf { it.closes })
    }

    private class CountedController(private val actual: TrailServiceController) : TrailServiceController by actual {
        var scans = 0
        var claims = 0
        var disconnects = 0
        var closes = 0
        override fun scanBluetoothDevices() { scans++; actual.scanBluetoothDevices() }
        override fun selectBluetoothDevice(endpointToken: String) { claims++; actual.selectBluetoothDevice(endpointToken) }
        override fun disconnectBluetoothDevice() { disconnects++; actual.disconnectBluetoothDevice() }
        override fun close() { closes++; actual.close() }
    }

    private class Port(var owner: ConnectedDeviceSessionOwner) : ConnectedDeviceSessionPort by owner {
        val callbacks = mutableListOf<(TrailAppUiState.BluetoothDevice) -> Unit>()
        var observeMode = "normal"
        var generationOverride: Long? = null
        var duringClose: (() -> Unit)? = null
        var afterObserve: (() -> Unit)? = null
        var returningOwnerActivation: (() -> Boolean)? = null
        var deadGeneration = false
        override fun activateReturningOwner(): Boolean = returningOwnerActivation?.invoke() == true
        override val generation get() = if (deadGeneration) error("host dead binder") else generationOverride ?: owner.generation
        var boundOnly = false
        override val state get() = if (boundOnly) owner.state.copy(
            serviceState = ConnectedDeviceServiceUiState.BOUND_ONLY,
        ) else owner.state
        override fun observe(observer: (TrailAppUiState.BluetoothDevice) -> Unit): ConnectedDeviceStateObservation? {
            callbacks += observer
            if (observeMode == "null") return null
            if (observeMode == "throw") error("host observation failure")
            val lease = owner.observe(observer) ?: return null
            afterObserve?.invoke()
            if (observeMode == "callback-then-null") { lease.close(); return null }
            if (observeMode == "callback-then-throw") { lease.close(); error("host observation failure") }
            return ConnectedDeviceStateObservation { duringClose?.invoke(); lease.close() }
        }
    }

    private class Connector(private val service: ServiceGraph) : ConnectedDeviceServiceConnector {
        val callbacks = mutableListOf<(ConnectedDeviceServiceConnection) -> Unit>()
        var delayed = false
        var reject = false
        var throwBind = false
        var duringClose: (() -> Unit)? = null
        var existingRequests = 0
        var startupBindings = 0
        var closedBindings = 0
        var bindMode = "normal"
        var acceptPendingWithoutOwner = false
        var acceptSilentExistingWithoutOwner = false
        var allowReturningOwner = false
        var returningOwnerBindings = 0
        override fun bindForReturningOwner(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? {
            if (!allowReturningOwner) return null
            returningOwnerBindings++
            return lease(observer)
        }
        override fun startFromVisibleUserAction(): ConnectedDeviceServiceStartFailure? {
            service.starts++; service.running = true; return null
        }
        override fun bind(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding {
            startupBindings++
            return lease(observer)
        }
        override fun bindExisting(observer: (ConnectedDeviceServiceConnection) -> Unit): ConnectedDeviceServiceBinding? {
            existingRequests++
            if (throwBind || bindMode == "throw") error("host binding failure")
            if (reject || bindMode == "null") return null
            if (!service.running && acceptSilentExistingWithoutOwner) return lease(observer, waitForCallback = true)
            if (!service.running && !acceptPendingWithoutOwner) return null
            if (bindMode == "callback-then-null" || bindMode == "callback-then-throw") {
                callbacks += observer
                observer(ConnectedDeviceServiceConnection.Connected(service.port))
                if (bindMode == "callback-then-throw") error("host binding failure")
                return null
            }
            return lease(observer)
        }
        private fun lease(
            observer: (ConnectedDeviceServiceConnection) -> Unit,
            waitForCallback: Boolean = delayed,
        ): ConnectedDeviceServiceBinding {
            callbacks += observer
            if (!waitForCallback) observer(if (bindMode == "disconnected") ConnectedDeviceServiceConnection.Disconnected
                else ConnectedDeviceServiceConnection.Connected(service.port))
            return object : ConnectedDeviceServiceBinding {
                var closed = false
                override fun close() { if (!closed) { closed = true; closedBindings++; duringClose?.invoke() } }
            }
        }
        override fun stopService() { service.stops++; service.running = false }
        override fun close() = Unit
    }

    private class Scheduler : BleRuntimeScheduler {
        val callbacks = mutableListOf<() -> Unit>()
        override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease {
            callbacks += callback
            return object : BleReconnectLease { override fun close() = Unit }
        }
    }

    private class Authorization : DeviceAuthorizationClaimClient {
        var claims = 0
        var observer: ((DeviceAuthorizationClaimEvent) -> Unit)? = null
        override fun createClaim(endpointToken: String, purpose: DeviceAuthorizationPurpose,
            observer: (DeviceAuthorizationClaimEvent) -> Unit): DeviceAuthorizationClaimLease {
            claims++; this.observer = observer
            return object : DeviceAuthorizationClaimLease {
                override fun start() = true
                override fun close() = Unit
            }
        }
    }

    private class Facade : AndroidBluetoothFacade, AutoCloseable {
        val scans = mutableListOf<Scan>()
        val returningOwnerScans = mutableListOf<Scan>()
        val connections = mutableListOf<Gatt>()
        var allowReturningOwnerScan = false
        var blocker: BleRuntimeBlock? = null
        var atNextPreflight: (() -> Unit)? = null
        override fun preflight(): BlePreflight {
            val change = atNextPreflight
            atNextPreflight = null
            change?.invoke()
            return BlePreflight(blocker)
        }
        override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease = Scan(observer).also(scans::add)
        override fun createReturningOwnerScan(observer: (BleScanEvent) -> Unit): BleScanLease? =
            if (allowReturningOwnerScan) Scan(observer).also(returningOwnerScans::add) else null
        override fun createConnection(endpointToken: String, purpose: BleConnectionPurpose,
            observer: (BleGattEvent) -> Unit): BleGattLease = Gatt(purpose, observer).also(connections::add)
        override fun close() = Unit
    }

    private class Scan(private val observer: (BleScanEvent) -> Unit) : BleScanLease {
        override fun start() = true
        override fun close() = Unit
        fun emit(event: BleScanEvent) = observer(event)
    }

    private class Gatt(val purpose: BleConnectionPurpose, private val observer: (BleGattEvent) -> Unit) : BleGattLease {
        var closes = 0
        override fun start() = true
        override fun requestMtu(mtu: Int) = true
        override fun readProtocolInfo() = true
        override fun subscribeStreamIndications() = true
        override fun writeCommandWithResponse(value: ByteArray) = true
        override fun close() { closes++ }
        fun emit(event: BleGattEvent) = observer(event)
    }

    companion object {
        private val CANDIDATE = BleDiscoveredCompanion("host-endpoint", "Host fixture")
    }
}
