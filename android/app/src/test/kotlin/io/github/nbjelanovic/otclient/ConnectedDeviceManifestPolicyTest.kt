package io.github.nbjelanovic.otclient

import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ConnectedDeviceManifestPolicyTest {
    @Test
    fun manifestDeclaresExactConnectedDeviceForegroundSurface() {
        val manifest = projectFile("src/main/AndroidManifest.xml").readText()
        val permissions = Regex("<uses-permission[^>]+android:name=\"([^\"]+)\"")
            .findAll(manifest)
            .map { it.groupValues[1] }
            .toSet()
        assertEquals(
            setOf(
                "android.permission.BLUETOOTH_SCAN",
                "android.permission.BLUETOOTH_CONNECT",
                "android.permission.FOREGROUND_SERVICE",
                "android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE",
                "android.permission.POST_NOTIFICATIONS",
            ),
            permissions,
        )
        assertTrue(manifest.contains("android:name=\".TrailConnectedDeviceService\""))
        assertTrue(manifest.contains("android:exported=\"false\""))
        assertTrue(manifest.contains("android:foregroundServiceType=\"connectedDevice\""))
        assertFalse(manifest.contains("<receiver"))
        listOf("LOCATION", "INTERNET", "STORAGE", "MANAGE_EXTERNAL_STORAGE").forEach {
            assertFalse(manifest.contains(it))
        }
    }

    @Test
    fun serviceSourceIsNonStickyExplicitAndContainsNoAutoStartPath() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/TrailConnectedDeviceService.kt",
        ).readText()
        assertTrue(source.contains("intent?.action != ACTION_START_FROM_VISIBLE_USER"))
        assertTrue(source.contains("START_NOT_STICKY"))
        assertFalse(source.contains("START_STICKY"))
        assertFalse(source.contains("BOOT_COMPLETED"))
        assertFalse(source.contains("startScan()"))
        assertFalse(source.contains("FakeCompanionTransport"))
        assertTrue(source.contains("FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE"))
        val permissionGate = source.indexOf("if (!nearbyPermissionsGranted())")
        val promotionGate = source.indexOf("if (!promoteToForeground())")
        val ownerCreation = source.indexOf("createOwner(generation)")
        assertTrue(permissionGate >= 0 && promotionGate > permissionGate && ownerCreation > promotionGate)
        assertTrue(source.contains("catch (_: SecurityException)"))
        assertTrue(source.contains("stopSelfResult(startId)"))
        assertTrue(source.contains("PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE"))
        assertTrue(source.contains("CONNECTED_DEVICE_NOTIFICATION_TITLE"))
        assertTrue(source.contains("CONNECTED_DEVICE_NOTIFICATION_TEXT"))
    }

    @Test
    fun activityKeepsScreenOnOnlyThroughItsVisibleWindow() {
        val activitySource = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt",
        ).readText()
        val keepScreenOn =
            "window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)"
        val superOnCreate = activitySource.indexOf("super.onCreate(savedInstanceState)")
        val keepScreenOnIndex = activitySource.indexOf(keepScreenOn)
        val controllerCreation = activitySource.indexOf("appController =")
        assertTrue(superOnCreate >= 0)
        assertTrue(keepScreenOnIndex > superOnCreate)
        assertTrue(controllerCreation > keepScreenOnIndex)
        assertEquals(1, Regex(Regex.escape(keepScreenOn)).findAll(activitySource).count())
        listOf(
            "PowerManager",
            "WakeLock",
            "WAKE_LOCK",
            "setTurnScreenOn",
            "FLAG_TURN_SCREEN_ON",
            "setShowWhenLocked",
            "FLAG_SHOW_WHEN_LOCKED",
            "screenBrightness",
        ).forEach {
            assertFalse(activitySource.contains(it))
        }

        val serviceSource = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/TrailConnectedDeviceService.kt",
        ).readText()
        assertFalse(serviceSource.contains("FLAG_KEEP_SCREEN_ON"))
    }

    @Test
    fun activityRendersTheExactTrailArtworkWithoutAStartupDelay() {
        val activitySource = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt",
        ).readText()
        val artwork = projectFile(
            "src/main/res/drawable-nodpi/limited_underground_trail.png",
        )

        assertEquals(2_559_044L, artwork.length())
        val artworkSha256 = MessageDigest.getInstance("SHA-256")
            .digest(artwork.readBytes())
            .joinToString("") { byte -> "%02X".format(byte) }
        assertEquals(
            "A3024504BA261ADDAFD2A85F49F6BCE630D1E9AB994EEA348D5842A6D2AB7422",
            artworkSha256,
        )
        assertTrue(activitySource.contains("R.drawable.limited_underground_trail"))
        assertTrue(activitySource.contains("contentDescription = null"))
        assertTrue(activitySource.contains("contentScale = ContentScale.Fit"))
        assertTrue(activitySource.contains("Text(\"Limited Underground\""))
        assertTrue(activitySource.contains("Text(\"Trail\""))
        listOf("Thread.sleep", "delay(", "postDelayed", "SplashScreen").forEach {
            assertFalse(activitySource.contains(it))
        }
    }

    @Test
    fun protectedBluetoothBondBroadcastUsesExportedReceiverAndExactCallbackFilters() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val registration = source.substring(
            source.indexOf("private fun registerBondReceiver()"),
            source.indexOf("private fun handleBondBroadcast"),
        )
        assertTrue(registration.contains("IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)"))
        assertTrue(registration.contains("Context.RECEIVER_EXPORTED"))
        assertFalse(registration.contains("Context.RECEIVER_NOT_EXPORTED"))

        val callback = source.substring(
            source.indexOf("private fun handleBondBroadcast"),
            source.indexOf("private fun handleBondAction"),
        )
        assertTrue(callback.contains("intent?.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED"))
        assertTrue(callback.contains("callbackDevice != device"))
        assertTrue(callback.contains("handleObservedBondState(state, fromPoll = false)"))

        val timeout = source.substring(
            source.indexOf("private val bondTimeout"),
            source.indexOf("private var bondPollActive"),
        )
        assertTrue(timeout.contains("!bondAttemptGate.allows(systemBondGeneration)"))

        val poll = source.substring(
            source.indexOf("private val bondStatePoll"),
            source.indexOf("private val bondReceiver"),
        )
        assertTrue(source.contains("ANDROID_SYSTEM_BOND_POLL_INTERVAL_MILLIS = 100L"))
        assertTrue(poll.contains("activeGatt !== this@PlatformGattLease"))
        assertTrue(poll.contains("!bondPollActive"))
        assertTrue(poll.contains("!bondReceiverRegistered"))
        assertTrue(poll.contains("!bondAttemptGate.allows(systemBondGeneration)"))
        assertTrue(poll.contains("currentSystemBondState()"))
        assertTrue(poll.contains("handleObservedBondState(state, fromPoll = true)"))
        assertTrue(poll.contains("postDelayed(this, ANDROID_SYSTEM_BOND_POLL_INTERVAL_MILLIS)"))
        assertTrue(source.contains("mainHandler.post(bondStatePoll)"))
        assertTrue(source.contains("mainHandler.removeCallbacks(bondStatePoll)"))
        assertTrue(source.contains("bondAttemptGate.end()"))
        assertTrue(source.contains("!bondAttemptGate.allows(systemBondGeneration)"))
        assertTrue(source.contains("systemBond.onBondState(endpointToken, systemBondGeneration, state)"))
        val start = source.substring(
            source.indexOf("override fun start(): Boolean"),
            source.indexOf("private fun requestSystemBond"),
        )
        assertFalse(start.contains("bondPrerequisite.latchFreshAttemptProof()"))

        val bondAction = source.substring(
            source.indexOf("private fun handleBondAction"),
            source.indexOf("private fun cleanupBondAttempt"),
        )
        assertTrue(bondAction.contains("bondPrerequisite.latchFreshAttemptProof()"))
        assertTrue(source.contains("bondPrerequisite.isSatisfied(currentSystemBondState())"))

        val close = source.substring(
            source.indexOf("override fun close()", source.indexOf("private inner class PlatformGattLease")),
            source.indexOf("private fun handleConnectionState"),
        )
        assertTrue(close.contains("bondPrerequisite.clear()"))

        val openGatt = source.substring(
            source.indexOf("private fun openGatt()"),
            source.indexOf("private fun registerBondReceiver"),
        )
        assertTrue(openGatt.indexOf("gatt = opened") < openGatt.indexOf("observer(BleGattEvent.GattOpened)"))
    }

    @Test
    fun addDeviceAndReturningOwnerScansAreDisjointWhileGattDiscoveryStaysBoundToD0() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val scan = source.substring(
            source.indexOf("private inner class PlatformScanLease"),
            source.indexOf("private inner class PlatformGattLease"),
        )
        assertFalse(source.contains("import android.bluetooth.le.ScanFilter"))
        assertFalse(scan.contains("ScanFilter.Builder"))
        assertTrue(scan.contains("scanner.startScan(null, settings, callback)"))
        assertTrue(scan.contains("AndroidPairableAdvertisementPolicy.accepts"))
        assertTrue(scan.contains("AndroidReturningOwnerAdvertisementPolicy.accepts"))
        assertTrue(scan.contains("AndroidReturningOwnerBondAdmissionPolicy.accepts"))
        assertTrue(scan.contains("currentBondState(result.device)"))
        assertFalse(scan.contains("result.device in bondedDevicesAtStart"))
        assertFalse(scan.contains("result.device in currentBonds"))
        assertTrue(scan.contains("if (purpose == ScanPurpose.RETURNING_OWNER)"))
        assertTrue(scan.contains("observer(BleScanEvent.Failed(BleGattFailure.PLATFORM_FAILURE))"))

        val discovery = source.substring(
            source.indexOf("private fun handleServicesDiscovered"),
            source.indexOf("private fun handleProtocolInfoRead"),
        )
        assertTrue(discovery.contains("callbackGatt.getService(gattServiceUuid)"))
        assertFalse(discovery.contains("getService(pairableAdvertisingUuid)"))

        assertTrue(source.contains("val entry = candidates.entries.singleOrNull"))
        assertTrue(source.contains("entry.value.allowSystemBondCreation"))
        assertTrue(
            source.contains(
                "purpose == BleConnectionPurpose.INITIAL_AUTHORIZATION && entry.value.allowSystemBondCreation",
            ),
        )
        val gattStart = source.substring(
            source.indexOf("override fun start()", source.indexOf("private inner class PlatformGattLease")),
            source.indexOf("private fun requestSystemBond"),
        )
        assertTrue(gattStart.contains("if (!allowSystemBondCreation && !alreadyBonded)"))
        assertTrue(gattStart.contains("fail(BleGattFailure.BOND_REQUIRED)"))
        assertEquals(1, Regex("""device\.createBond\(\)""").findAll(source).count())
        val requestSystemBond = source.substring(
            source.indexOf("private fun requestSystemBond"),
            source.indexOf("private fun rejectBondStart"),
        )
        assertTrue(requestSystemBond.contains("device.createBond()"))
        assertFalse(source.contains("device.address"))
    }

    @Test
    fun freshAuthorizationBondsAndDiscoversOnOneExactGattLease() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val gattLeaseStart = source.indexOf("private inner class PlatformGattLease")
        val gattLease = source.substring(gattLeaseStart, source.indexOf("private data class DiscoveredGattProfile"))
        assertEquals(1, Regex("""device\.connectGatt\(""").findAll(gattLease).count())
        assertFalse(gattLease.contains("removeBond("))

        val start = source.substring(
            source.indexOf("override fun start()", gattLeaseStart),
            source.indexOf("private fun requestSystemBond", gattLeaseStart),
        )
        val freshBondBranch = start.substring(
            start.indexOf("AndroidSystemBondAction.RequestSystemBond"),
            start.indexOf("AndroidSystemBondAction.ProceedToGatt"),
        )
        assertTrue(freshBondBranch.contains("bondRequiredAfterConnect = true"))
        assertTrue(freshBondBranch.contains("openGatt()"))
        assertFalse(freshBondBranch.contains("requestSystemBond()"))

        val connection = source.substring(
            source.indexOf("private fun handleConnectionState", gattLeaseStart),
            source.indexOf("private fun discoverServicesOnOwnedGatt", gattLeaseStart),
        )
        assertTrue(connection.contains("newState == BluetoothProfile.STATE_CONNECTED"))
        assertTrue(connection.contains("if (bondRequiredAfterConnect)"))
        assertTrue(connection.contains("requestSystemBond()"))
        assertFalse(connection.contains("discoverServices()"))

        val requestBond = source.substring(
            source.indexOf("private fun requestSystemBond", gattLeaseStart),
            source.indexOf("private fun rejectBondStart", gattLeaseStart),
        )
        assertEquals(1, Regex("""device\.createBond\(\)""").findAll(requestBond).count())

        val bondAction = source.substring(
            source.indexOf("private fun handleBondAction", gattLeaseStart),
            source.indexOf("private fun cleanupBondAttempt", gattLeaseStart),
        )
        val bondedBranch = bondAction.substring(
            bondAction.indexOf("AndroidSystemBondAction.ProceedToGatt"),
            bondAction.indexOf("is AndroidSystemBondAction.Failed"),
        )
        assertTrue(bondedBranch.contains("bondPrerequisite.latchFreshAttemptProof()"))
        assertTrue(bondedBranch.contains("discoverServicesOnOwnedGatt()"))
        assertFalse(bondedBranch.contains("openGatt()"))

        val discovery = source.substring(
            source.indexOf("private fun discoverServicesOnOwnedGatt", gattLeaseStart),
            source.indexOf("private fun handleServicesDiscovered", gattLeaseStart),
        )
        assertTrue(discovery.contains("val current = gatt ?: return false"))
        assertTrue(discovery.contains("owns(current)"))
        assertTrue(discovery.contains("bondedPrerequisiteSatisfied()"))
        assertTrue(discovery.contains("current.discoverServices()"))
    }

    @Test
    fun serviceChangedUsesOneSameGattRediscoveryWithoutRebondingOrHiddenRefresh() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val leaseStart = source.indexOf("private inner class PlatformGattLease")
        val lease = source.substring(leaseStart, source.indexOf("private data class DiscoveredGattProfile"))
        val callback = lease.substring(
            lease.indexOf("override fun onServiceChanged"),
            lease.indexOf("override fun start()"),
        )
        assertTrue(callback.contains("handleServiceChanged(callbackGatt)"))
        assertFalse(callback.contains("fail(BleGattFailure.PLATFORM_FAILURE)"))
        assertTrue(lease.contains("AndroidServiceChangedDiscoveryGate()"))
        assertTrue(lease.contains("retryDiscoveryAfterServiceChanged()"))
        assertTrue(lease.contains("current.discoverServices()"))
        assertTrue(lease.contains("mainHandler.removeCallbacks(serviceChangedFailureGrace)"))
        assertTrue(lease.contains("mainHandler.removeCallbacks(serviceRediscovery)"))
        assertFalse(lease.contains("refresh()"))
        assertEquals(1, Regex("""device\.connectGatt\(""").findAll(lease).count())
        assertEquals(1, Regex("""device\.createBond\(\)""").findAll(lease).count())
        assertFalse(lease.contains("removeBond("))
    }

    @Test
    fun resetVerificationUsesExactBoundedReceiptWithoutPersistingDeviceIdentity() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val verificationFactory = source.substring(
            source.indexOf("override fun createFactoryResetVerificationScan"),
            source.indexOf("override fun completeFactoryResetVerification"),
        )
        assertTrue(verificationFactory.contains("resetReceiptStore.load() != receipt"))
        assertTrue(verificationFactory.contains("expectedResetReceipt = receipt"))
        assertTrue(verificationFactory.contains("ANDROID_FACTORY_RESET_VERIFICATION_WINDOW_MILLIS"))
        assertFalse(verificationFactory.contains("endpointToken"))
        assertFalse(verificationFactory.contains("BluetoothDevice"))

        val scan = source.substring(
            source.indexOf("private inner class PlatformScanLease"),
            source.indexOf("private inner class PlatformGattLease"),
        )
        assertFalse(scan.contains("setServiceData("))
        assertTrue(scan.contains("FactoryResetReceiptAdvertisementCodec.decode"))
        assertTrue(scan.contains("observedResetReceipt == expectedResetReceipt"))
        assertTrue(scan.contains("BleScanEvent.FactoryResetReceiptObserved"))
        assertTrue(scan.contains("if (purpose == ScanPurpose.FACTORY_RESET_VERIFICATION)"))
        assertFalse(scan.contains("result.device == exactDevice"))
        assertFalse(scan.contains("exactDevice"))

        val cleanup = source.substring(
            source.indexOf("override fun completeFactoryResetVerification"),
            source.indexOf("override fun createConnection"),
        )
        assertTrue(cleanup.contains("verifiedResetReceipt != receipt"))
        assertTrue(cleanup.contains("resetReceiptStore.clearExact(receipt)"))
        assertTrue(cleanup.contains("FactoryResetLocalCleanupResult.SYSTEM_BOND_REMAINS"))

        val persistence = source.substring(
            source.indexOf("internal class AndroidFactoryResetReceiptStore"),
            source.indexOf("object AndroidBlePermissionContract"),
        )
        assertTrue(persistence.contains("ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS"))
        assertTrue(persistence.contains("receipt_bits"))
        assertTrue(persistence.contains("issued_at_epoch_millis"))
        assertTrue(persistence.contains("expires_at_epoch_millis"))
        assertTrue(persistence.contains("now >= issuedAt"))
        assertTrue(persistence.contains("expiry - issuedAt == ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS"))
        assertFalse(persistence.contains("endpointToken"))
        assertFalse(persistence.contains("BluetoothDevice"))
        assertFalse(persistence.contains("address"))
        assertFalse(source.contains("removeBond("))
        assertFalse(source.contains("device.address"))
    }

    @Test
    fun platformScanIsUnfilteredBoundedSingleOwnerAndAlwaysCleansUp() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val factories = source.substring(
            source.indexOf("override fun createScan"),
            source.indexOf("override fun completeFactoryResetVerification"),
        )
        assertTrue(factories.contains("activeScan != null || activeGatt != null"))
        assertTrue(factories.contains("also { activeScan = it }"))

        val scan = source.substring(
            source.indexOf("private inner class PlatformScanLease"),
            source.indexOf("private inner class PlatformGattLease"),
        )
        assertTrue(scan.contains("private var platformRegistered = false"))
        assertTrue(scan.contains("scanner.startScan(null, settings, callback)"))
        assertTrue(scan.contains("mainHandler.postDelayed(timeout, timeoutMillis)"))
        assertTrue(scan.contains("platformRegistered && activeScan === this"))
        assertTrue(scan.contains("if (platformRegistered && operationAllowed(AndroidBlePlatformOperation.STOP_SCAN))"))
        assertTrue(scan.contains("mainHandler.removeCallbacks(timeout)"))
        assertTrue(scan.contains("stopScan(callback)"))
        assertTrue(scan.contains("platformRegistered = false"))
        assertTrue(scan.contains("if (activeScan === this) activeScan = null"))
        assertTrue(scan.contains("if (leaseClosed) return"))

        val emptyReturningOwner = scan.substring(
            scan.indexOf("if (purpose == ScanPurpose.RETURNING_OWNER && bondedDevicesAtStart.isEmpty())"),
            scan.indexOf("val scanner ="),
        )
        assertFalse(emptyReturningOwner.contains("platformRegistered = true"))
        val failure = scan.substring(
            scan.indexOf("override fun onScanFailed"),
            scan.indexOf("override fun start()"),
        )
        assertTrue(failure.indexOf("closePlatform()") < failure.indexOf("observer(BleScanEvent.Failed"))
        val completion = scan.substring(scan.indexOf("private fun finish()"))
        assertTrue(completion.indexOf("closePlatform()") < completion.indexOf("observer(BleScanEvent.Complete)"))
    }

    @Test
    fun profileReadyIsDeferredOnceAndBoundToTheExactLiveGattLease() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/AndroidBluetoothGattFacade.kt",
        ).readText()
        val gattLeaseStart = source.indexOf("private inner class PlatformGattLease")
        val runnableStart = source.indexOf("private val profileReadyRunnable", gattLeaseStart)
        val runnable = source.substring(
            runnableStart,
            source.indexOf("private var leaseClosed", runnableStart),
        )
        assertTrue(runnable.contains("val expectedGatt = queuedProfileReadyGatt"))
        assertTrue(runnable.contains("profileReadyGate.deliver()"))
        assertTrue(runnable.contains("expectedGatt == null || !owns(expectedGatt)"))
        assertTrue(runnable.contains("observer(BleGattEvent.ProfileReady)"))

        val discovery = source.substring(
            source.indexOf("private fun handleServicesDiscovered", gattLeaseStart),
            source.indexOf("private fun handleProtocolInfoRead", gattLeaseStart),
        )
        val duplicateGuard = discovery.indexOf("profileReadyGate.hasStarted()")
        assertTrue(duplicateGuard >= 0)
        assertTrue(duplicateGuard < discovery.indexOf("val profileAccepted = status == BluetoothGatt.GATT_SUCCESS"))
        assertTrue(duplicateGuard < discovery.indexOf("AndroidGattProfilePolicy.accepts(profile)"))
        assertTrue(duplicateGuard < discovery.indexOf("operations.acceptProfile()"))
        assertTrue(discovery.contains("if (!mainHandler.post(profileReadyRunnable))"))
        assertFalse(discovery.contains("postToMain { observer(BleGattEvent.ProfileReady)"))
        assertFalse(discovery.contains("observer(BleGattEvent.ProfileReady)"))

        val postRejection = discovery.substring(discovery.indexOf("if (!mainHandler.post(profileReadyRunnable))"))
        assertTrue(postRejection.contains("profileReadyGate.rejectPost()"))
        assertTrue(postRejection.contains("fail(BleGattFailure.PLATFORM_FAILURE)"))

        val closeStart = source.indexOf("override fun close()", gattLeaseStart)
        val close = source.substring(closeStart, source.indexOf("private fun handleConnectionState", closeStart))
        assertTrue(close.contains("mainHandler.removeCallbacks(profileReadyRunnable)"))
        assertTrue(close.contains("queuedProfileReadyGatt = null"))
        assertTrue(close.contains("profileReadyGate.close()"))
    }

    private fun projectFile(path: String): File {
        val direct = File(path)
        if (direct.isFile) return direct
        val fromAndroid = File("app", path)
        require(fromAndroid.isFile) { "Required Android source file is missing: $path" }
        return fromAndroid
    }
}
