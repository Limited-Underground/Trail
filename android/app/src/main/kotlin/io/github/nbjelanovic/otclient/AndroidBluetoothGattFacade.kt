package io.github.nbjelanovic.otclient

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import io.github.nbjelanovic.otprotocol.COMPANION_FRAGMENT_HEADER_BYTES
import io.github.nbjelanovic.otprotocol.COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES
import java.security.SecureRandom
import java.util.UUID

internal const val ANDROID_BLE_SCAN_WINDOW_MILLIS = AndroidBlePlatformPlan.SCAN_WINDOW_MILLIS
internal const val ANDROID_FACTORY_RESET_VERIFICATION_WINDOW_MILLIS = 75_000L
internal const val ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS = 120_000L
internal const val FACTORY_RESET_RECEIPT_SERVICE_DATA_BYTES = 13
private const val FACTORY_RESET_RECEIPT_GENERATION_ATTEMPTS = 16
private const val CLIENT_CONFIGURATION_UUID = "00002902-0000-1000-8000-00805f9b34fb"
private const val BLUETOOTH_SCAN_PERMISSION = "android.permission.BLUETOOTH_SCAN"
private const val BLUETOOTH_CONNECT_PERMISSION = "android.permission.BLUETOOTH_CONNECT"
internal const val ANDROID_SYSTEM_BOND_TIMEOUT_MILLIS = 60_000L
internal const val ANDROID_SYSTEM_BOND_POLL_INTERVAL_MILLIS = 100L
private const val ANDROID_SERVICE_CHANGED_FAILURE_GRACE_MILLIS = 250L

internal object FactoryResetReceiptAdvertisementCodec {
    private val magic = byteArrayOf(0x4f, 0x54, 0x52, 0x52)

    fun encode(receipt: ULong): ByteArray? {
        if (receipt == 0uL) return null
        return ByteArray(FACTORY_RESET_RECEIPT_SERVICE_DATA_BYTES).also { output ->
            magic.copyInto(output)
            output[4] = 0x01
            repeat(8) { index -> output[5 + index] = (receipt shr (index * 8)).toByte() }
        }
    }

    fun decode(payload: ByteArray?): ULong? {
        if (
            payload == null ||
            payload.size != FACTORY_RESET_RECEIPT_SERVICE_DATA_BYTES ||
            magic.indices.any { payload[it] != magic[it] } ||
            payload[4].toInt() != 0x01
        ) return null
        var receipt = 0uL
        repeat(8) { index ->
            receipt = receipt or ((payload[5 + index].toInt() and 0xff).toULong() shl (index * 8))
        }
        return receipt.takeIf { it != 0uL }
    }
}

internal interface FactoryResetReceiptStorage {
    fun readLong(key: String): Long?
    fun writeLongs(values: Map<String, Long>): Boolean
    fun remove(keys: Set<String>): Boolean
}

private class SharedPreferencesFactoryResetReceiptStorage(
    private val preferences: SharedPreferences,
) : FactoryResetReceiptStorage {
    override fun readLong(key: String): Long? =
        if (preferences.contains(key)) preferences.getLong(key, 0L) else null

    override fun writeLongs(values: Map<String, Long>): Boolean {
        val editor = preferences.edit()
        values.forEach { (key, value) -> editor.putLong(key, value) }
        return editor.commit()
    }

    override fun remove(keys: Set<String>): Boolean {
        val editor = preferences.edit()
        keys.forEach(editor::remove)
        return editor.commit()
    }
}

internal class AndroidFactoryResetReceiptStore(
    private val storage: FactoryResetReceiptStorage,
    private val nowMillis: () -> Long,
    private val receiptFactory: () -> ULong,
) {
    constructor(
        context: Context,
        nowMillis: () -> Long,
        receiptFactory: () -> ULong,
    ) : this(
        SharedPreferencesFactoryResetReceiptStorage(
            context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE),
        ),
        nowMillis,
        receiptFactory,
    )

    fun stage(): ULong? {
        if (load() != null) return null
        val now = nowMillis()
        if (now < 0L || now > Long.MAX_VALUE - ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS) return null
        repeat(FACTORY_RESET_RECEIPT_GENERATION_ATTEMPTS) {
            val receipt = receiptFactory()
            if (receipt != 0uL) {
                val committed = storage.writeLongs(
                    mapOf(
                        RECEIPT_KEY to receipt.toLong(),
                        ISSUED_AT_KEY to now,
                        EXPIRY_KEY to (now + ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS),
                    ),
                )
                if (committed) return receipt
            }
        }
        return null
    }

    fun load(): ULong? {
        val receiptBits = storage.readLong(RECEIPT_KEY)
        val issuedAt = storage.readLong(ISSUED_AT_KEY)
        val expiry = storage.readLong(EXPIRY_KEY)
        if (receiptBits == null || issuedAt == null || expiry == null) {
            clearAll()
            return null
        }
        val receipt = receiptBits.toULong()
        val now = nowMillis()
        val coherentBounds =
            issuedAt >= 0L &&
                expiry >= issuedAt &&
                expiry - issuedAt == ANDROID_FACTORY_RESET_RECEIPT_TTL_MILLIS &&
                now >= issuedAt &&
                now < expiry
        if (receipt == 0uL || !coherentBounds) {
            clearAll()
            return null
        }
        return receipt
    }

    fun clearExact(receipt: ULong): Boolean {
        if (receipt == 0uL || load() != receipt) return false
        return clearAll()
    }

    private fun clearAll(): Boolean = storage.remove(setOf(RECEIPT_KEY, ISSUED_AT_KEY, EXPIRY_KEY))

    companion object {
        const val PREFERENCES_NAME = "trail_pending_factory_reset_receipt_v1"
        const val RECEIPT_KEY = "receipt_bits"
        const val ISSUED_AT_KEY = "issued_at_epoch_millis"
        const val EXPIRY_KEY = "expires_at_epoch_millis"
    }
}

object AndroidBlePermissionContract {
    val runtimePermissions: List<String> = listOf(
        BLUETOOTH_SCAN_PERMISSION,
        BLUETOOTH_CONNECT_PERMISSION,
    )
}

class AndroidMainThreadBleRuntimeVerifier : BleRuntimeThreadVerifier {
    override fun isOwnerThread(): Boolean = Looper.myLooper() === Looper.getMainLooper()
}

class AndroidMainThreadBleRuntimeScheduler(
    private val handler: Handler = Handler(Looper.getMainLooper()),
) : BleRuntimeScheduler {
    override fun schedule(delayMillis: Long, callback: () -> Unit): BleReconnectLease {
        check(Looper.myLooper() === handler.looper) { "BLE timers must be scheduled on their owner thread." }
        require(delayMillis >= 0)
        val lease = HandlerTimerLease(handler, callback)
        check(handler.postDelayed(lease.runnable, delayMillis)) { "BLE timer scheduling was rejected." }
        return lease
    }

    private class HandlerTimerLease(
        private val handler: Handler,
        callback: () -> Unit,
    ) : BleReconnectLease {
        private var closed = false
        val runnable = Runnable {
            if (closed) return@Runnable
            closed = true
            callback()
        }

        override fun close() {
            if (closed) return
            closed = true
            handler.removeCallbacks(runnable)
        }
    }
}

/**
 * Concrete API-31+ Android BLE central facade. Permissions are never requested here; absent explicit
 * grants, [preflight] blocks all scan/connect work. Android bond state is only a prerequisite. The
 * exact successful device-protected ProtocolInfo read is the provisional protected-path evidence.
 */
class AndroidBluetoothGattFacade(
    context: Context,
    private val mainHandler: Handler = Handler(Looper.getMainLooper()),
    private val tokenFactory: () -> String = { UUID.randomUUID().toString() },
    resetReceiptFactory: () -> ULong = { SecureRandom().nextLong().toULong() },
    nowMillis: () -> Long = System::currentTimeMillis,
) : AndroidBluetoothFacade, AutoCloseable {
    private val appContext = context.applicationContext
    private val gattServiceUuid = UUID.fromString(AndroidBlePlatformPlan.GATT_SERVICE_UUID)
    private val pairableAdvertisingUuid = UUID.fromString(AndroidBlePlatformPlan.PAIRABLE_ADVERTISING_UUID)
    private val protocolInfoUuid = UUID.fromString(CompanionGattV0Contract.PROTOCOL_INFO_UUID)
    private val commandUuid = UUID.fromString(CompanionGattV0Contract.COMMAND_UUID)
    private val streamUuid = UUID.fromString(CompanionGattV0Contract.STREAM_UUID)
    private val cccdUuid = UUID.fromString(CLIENT_CONFIGURATION_UUID)
    private val resetReceiptStore = AndroidFactoryResetReceiptStore(appContext, nowMillis, resetReceiptFactory)
    private val candidates = LinkedHashMap<BluetoothDevice, CandidateBinding>()
    private var activeScan: PlatformScanLease? = null
    private var activeGatt: PlatformGattLease? = null
    private var systemBondGeneration: Long = 0
    private var verifiedResetReceipt: ULong? = null
    private var closed = false

    private data class CandidateBinding(
        val endpointToken: String,
        val publicLabel: String,
        val allowSystemBondCreation: Boolean,
    )

    private enum class ScanPurpose { ADD_DEVICE, RETURNING_OWNER, FACTORY_RESET_VERIFICATION }

    override fun preflight(): BlePreflight = AndroidBlePreflightPolicy.evaluate(platformSnapshot())

    override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease? {
        if (!onMainThread() || closed || !preflight().isReady || activeScan != null || activeGatt != null) return null
        candidates.clear()
        return PlatformScanLease(observer, ScanPurpose.ADD_DEVICE, emptySet()).also { activeScan = it }
    }

    override fun createReturningOwnerScan(observer: (BleScanEvent) -> Unit): BleScanLease? {
        if (!onMainThread() || closed || !preflight().isReady || activeScan != null || activeGatt != null) return null
        val bondedDevices = currentBondedDevices() ?: return null
        candidates.clear()
        return PlatformScanLease(observer, ScanPurpose.RETURNING_OWNER, bondedDevices).also { activeScan = it }
    }

    override fun stageFactoryResetReceipt(): ULong? =
        if (onMainThread() && !closed) resetReceiptStore.stage() else null

    override fun loadPendingFactoryResetReceipt(): ULong? =
        if (onMainThread() && !closed) resetReceiptStore.load() else null

    override fun clearPendingFactoryResetReceipt(receipt: ULong): Boolean =
        onMainThread() && !closed && resetReceiptStore.clearExact(receipt)

    override fun createFactoryResetVerificationScan(
        receipt: ULong,
        observer: (BleScanEvent) -> Unit,
    ): BleScanLease? {
        if (!onMainThread() || closed || !preflight().isReady || activeScan != null || activeGatt != null) return null
        if (receipt == 0uL || resetReceiptStore.load() != receipt) return null
        return PlatformScanLease(
            observer = observer,
            purpose = ScanPurpose.FACTORY_RESET_VERIFICATION,
            bondedDevicesAtStart = emptySet(),
            expectedResetReceipt = receipt,
            timeoutMillis = ANDROID_FACTORY_RESET_VERIFICATION_WINDOW_MILLIS,
        ).also { activeScan = it }
    }

    override fun completeFactoryResetVerification(receipt: ULong): FactoryResetLocalCleanupResult {
        if (!onMainThread() || closed || activeScan != null || activeGatt != null) {
            return FactoryResetLocalCleanupResult.FAILED
        }
        if (receipt == 0uL || verifiedResetReceipt != receipt) return FactoryResetLocalCleanupResult.FAILED
        if (!resetReceiptStore.clearExact(receipt)) return FactoryResetLocalCleanupResult.FAILED
        verifiedResetReceipt = null
        candidates.clear()
        // The post-reset RPA cannot be linked back to Android's stale bond without retaining private
        // identity. Prompt the user to remove the old system pairing instead of guessing or using
        // hidden removeBond APIs.
        return FactoryResetLocalCleanupResult.SYSTEM_BOND_REMAINS
    }

    override fun createConnection(
        endpointToken: String,
        purpose: BleConnectionPurpose,
        observer: (BleGattEvent) -> Unit,
    ): BleGattLease? {
        if (!onMainThread() || closed || !preflight().isReady || activeScan != null || activeGatt != null) return null
        val entry = candidates.entries.singleOrNull { it.value.endpointToken == endpointToken } ?: return null
        val generation = nextSystemBondGeneration() ?: return null
        return PlatformGattLease(
            endpointToken,
            generation,
            entry.key,
            purpose == BleConnectionPurpose.INITIAL_AUTHORIZATION && entry.value.allowSystemBondCreation,
            observer,
        ).also { activeGatt = it }
    }

    override fun close() {
        if (!onMainThread()) {
            mainHandler.post { close() }
            return
        }
        if (closed) return
        closed = true
        verifiedResetReceipt = null
        activeScan?.close()
        activeGatt?.close()
        candidates.clear()
    }

    private fun platformSnapshot(): AndroidBlePlatformSnapshot {
        val apiLevel = Build.VERSION.SDK_INT
        val packageManager = appContext.packageManager
        val hasFeature = packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)
        val manager = appContext.getSystemService(BluetoothManager::class.java)
        val adapter = manager?.adapter
        val scanGranted = apiLevel >= Build.VERSION_CODES.S &&
            appContext.checkSelfPermission(BLUETOOTH_SCAN_PERMISSION) == PackageManager.PERMISSION_GRANTED
        val connectGranted = apiLevel >= Build.VERSION_CODES.S &&
            appContext.checkSelfPermission(BLUETOOTH_CONNECT_PERMISSION) == PackageManager.PERMISSION_GRANTED
        val enabled = if (connectGranted) {
            try {
                adapter?.isEnabled == true
            } catch (_: SecurityException) {
                false
            }
        } else false
        return AndroidBlePlatformSnapshot(
            apiLevel = apiLevel,
            hasBleFeature = hasFeature,
            hasBluetoothAdapter = adapter != null,
            scanPermissionGranted = scanGranted,
            connectPermissionGranted = connectGranted,
            bluetoothEnabled = enabled,
        )
    }

    private fun bluetoothManager(): BluetoothManager? = appContext.getSystemService(BluetoothManager::class.java)

    private fun connectPermissionGranted(): Boolean =
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            appContext.checkSelfPermission(BLUETOOTH_CONNECT_PERMISSION) == PackageManager.PERMISSION_GRANTED

    private fun scanPermissionGranted(): Boolean =
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            appContext.checkSelfPermission(BLUETOOTH_SCAN_PERMISSION) == PackageManager.PERMISSION_GRANTED

    @SuppressLint("MissingPermission")
    private fun currentBondedDevices(): Set<BluetoothDevice>? {
        if (!operationAllowed(AndroidBlePlatformOperation.CONNECT)) return null
        val adapter = bluetoothManager()?.adapter ?: return null
        return try {
            adapter.bondedDevices.toSet()
        } catch (_: SecurityException) {
            null
        } catch (_: IllegalStateException) {
            null
        }
    }

    @SuppressLint("MissingPermission")
    private fun currentBondState(device: BluetoothDevice): AndroidSystemBondState? {
        if (!operationAllowed(AndroidBlePlatformOperation.CONNECT)) return null
        return try {
            when (device.bondState) {
                BluetoothDevice.BOND_NONE -> AndroidSystemBondState.NONE
                BluetoothDevice.BOND_BONDING -> AndroidSystemBondState.BONDING
                BluetoothDevice.BOND_BONDED -> AndroidSystemBondState.BONDED
                else -> null
            }
        } catch (_: SecurityException) {
            null
        }
    }

    private fun operationAllowed(operation: AndroidBlePlatformOperation): Boolean =
        Build.VERSION.SDK_INT >= ANDROID_BLE_MINIMUM_API && AndroidBleOperationPermissionPolicy.allows(
            operation = operation,
            scanPermissionGranted = scanPermissionGranted(),
            connectPermissionGranted = connectPermissionGranted(),
        )

    private fun onMainThread(): Boolean = Looper.myLooper() === mainHandler.looper

    private fun nextSystemBondGeneration(): Long? {
        if (systemBondGeneration == Long.MAX_VALUE) return null
        systemBondGeneration += 1
        return systemBondGeneration
    }

    private fun postToMain(block: () -> Unit) {
        if (onMainThread()) block() else mainHandler.post(block)
    }

    private inner class PlatformScanLease(
        private val observer: (BleScanEvent) -> Unit,
        private val purpose: ScanPurpose,
        private val bondedDevicesAtStart: Set<BluetoothDevice>,
        private val expectedResetReceipt: ULong? = null,
        private val timeoutMillis: Long = ANDROID_BLE_SCAN_WINDOW_MILLIS,
    ) : BleScanLease {
        private var started = false
        private var platformRegistered = false
        private var leaseClosed = false
        private val timeout = Runnable { finish() }
        private val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                postToMain { acceptResult(result) }
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                postToMain { results.forEach(::acceptResult) }
            }

            override fun onScanFailed(errorCode: Int) {
                postToMain {
                    if (leaseClosed) return@postToMain
                    val failure = if (operationAllowed(AndroidBlePlatformOperation.START_SCAN)) {
                        BleGattFailure.PLATFORM_FAILURE
                    } else {
                        BleGattFailure.PERMISSION_REVOKED
                    }
                    closePlatform()
                    observer(BleScanEvent.Failed(failure))
                }
            }
        }

        override fun start(): Boolean {
            if (
                !onMainThread() ||
                started ||
                leaseClosed ||
                preflight().isReady.not() ||
                !operationAllowed(AndroidBlePlatformOperation.START_SCAN)
            ) return false
            if (purpose == ScanPurpose.RETURNING_OWNER && bondedDevicesAtStart.isEmpty()) {
                started = true
                finish()
                return true
            }
            if (purpose == ScanPurpose.FACTORY_RESET_VERIFICATION && expectedResetReceipt == null) {
                closePlatform()
                return false
            }
            val scanner = bluetoothManager()?.adapter?.bluetoothLeScanner ?: return false
            val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_BALANCED).build()
            return try {
                // Android shares a small controller-filter pool with system services. Keep the
                // registration unfiltered and bounded, then enforce every identity in software.
                scanner.startScan(null, settings, callback)
                started = true
                platformRegistered = true
                if (!mainHandler.postDelayed(timeout, timeoutMillis)) {
                    closePlatform()
                    return false
                }
                true
            } catch (_: SecurityException) {
                closePlatform()
                false
            } catch (_: IllegalStateException) {
                closePlatform()
                false
            }
        }

        override fun close() {
            if (!onMainThread()) {
                mainHandler.post { close() }
                return
            }
            closePlatform()
        }

        private fun acceptResult(result: ScanResult) {
            when (
                AndroidBleScanCallbackPolicy.evaluate(
                    active = !leaseClosed && started && platformRegistered && activeScan === this,
                    scanPermissionGranted = operationAllowed(AndroidBlePlatformOperation.START_SCAN),
                )
            ) {
                AndroidBleCallbackAdmission.IGNORE -> return
                AndroidBleCallbackAdmission.PERMISSION_REVOKED -> {
                    closePlatform()
                    observer(BleScanEvent.Failed(BleGattFailure.PERMISSION_REVOKED))
                    return
                }
                AndroidBleCallbackAdmission.ACCEPT -> Unit
            }
            val scanRecord = result.scanRecord ?: return
            val advertisedServices = scanRecord.serviceUuids ?: return
            val advertisedUuids = advertisedServices.map { it.uuid }
            val observedResetReceipt = if (purpose == ScanPurpose.FACTORY_RESET_VERIFICATION) {
                FactoryResetReceiptAdvertisementCodec.decode(
                    scanRecord.getServiceData(ParcelUuid(pairableAdvertisingUuid)),
                )
            } else null
            val accepted = when (purpose) {
                ScanPurpose.ADD_DEVICE -> AndroidPairableAdvertisementPolicy.accepts(advertisedUuids)
                ScanPurpose.RETURNING_OWNER -> {
                    val currentBonds = currentBondedDevices()
                    if (currentBonds == null) {
                        closePlatform()
                        observer(BleScanEvent.Failed(BleGattFailure.PERMISSION_REVOKED))
                        return
                    }
                    val stillBonded = AndroidReturningOwnerBondAdmissionPolicy.accepts(
                        hadBondedInventoryAtStart = bondedDevicesAtStart.isNotEmpty(),
                        hasCurrentBondedInventory = currentBonds.isNotEmpty(),
                        scanResultBondState = currentBondState(result.device),
                    )
                    AndroidReturningOwnerAdvertisementPolicy.accepts(advertisedUuids, stillBonded)
                }
                ScanPurpose.FACTORY_RESET_VERIFICATION ->
                    observedResetReceipt == expectedResetReceipt &&
                        AndroidPairableAdvertisementPolicy.accepts(advertisedUuids)
            }
            // The platform scan is intentionally unfiltered. Software revalidation is mandatory:
            // Add Device is D1-only; returning owner is bonded D0-only and rejects D1; reset
            // verification also requires the exact short-lived receipt.
            if (!accepted) return
            if (purpose == ScanPurpose.FACTORY_RESET_VERIFICATION) {
                verifiedResetReceipt = observedResetReceipt
                closePlatform()
                observer(BleScanEvent.FactoryResetReceiptObserved(observedResetReceipt ?: return))
                return
            }
            val existing = candidates[result.device]
            val binding = existing ?: if (candidates.size < MAX_DISCOVERED_COMPANIONS) {
                val endpointToken = OpaqueEndpointTokenPolicy.generate(
                    existing = candidates.values.map(CandidateBinding::endpointToken),
                    factory = tokenFactory,
                ) ?: run {
                    // A distinct returning-owner device must never disappear behind token-factory
                    // exhaustion, because that could turn an ambiguous set into an apparent single.
                    if (purpose == ScanPurpose.RETURNING_OWNER) {
                        closePlatform()
                        observer(BleScanEvent.Failed(BleGattFailure.PLATFORM_FAILURE))
                    }
                    return
                }
                CandidateBinding(
                    endpointToken = endpointToken,
                    publicLabel = if (purpose == ScanPurpose.ADD_DEVICE) {
                        "Nearby compatible device ${candidates.size + 1}"
                    } else {
                        "Authorized device ${candidates.size + 1}"
                    },
                    allowSystemBondCreation = purpose == ScanPurpose.ADD_DEVICE,
                ).also { candidates[result.device] = it }
            } else return
            observer(BleScanEvent.Candidate(BleDiscoveredCompanion(binding.endpointToken, binding.publicLabel)))
        }

        private fun finish() {
            if (leaseClosed) return
            val permissionRevoked =
                !operationAllowed(AndroidBlePlatformOperation.START_SCAN) ||
                    (purpose == ScanPurpose.RETURNING_OWNER &&
                        !operationAllowed(AndroidBlePlatformOperation.CONNECT))
            closePlatform()
            if (permissionRevoked) {
                observer(BleScanEvent.Failed(BleGattFailure.PERMISSION_REVOKED))
            } else {
                observer(BleScanEvent.Complete)
            }
        }

        private fun closePlatform() {
            if (leaseClosed) return
            leaseClosed = true
            mainHandler.removeCallbacks(timeout)
            if (platformRegistered && operationAllowed(AndroidBlePlatformOperation.STOP_SCAN)) {
                try {
                    bluetoothManager()?.adapter?.bluetoothLeScanner?.stopScan(callback)
                } catch (_: SecurityException) {
                    // Closing remains best-effort after permission revocation.
                } catch (_: IllegalStateException) {
                    // The platform scanner may already be unavailable.
                }
            }
            platformRegistered = false
            started = false
            if (activeScan === this) activeScan = null
        }
    }

    private inner class PlatformGattLease(
        private val endpointToken: String,
        private val systemBondGeneration: Long,
        private val device: BluetoothDevice,
        private val allowSystemBondCreation: Boolean,
        private val observer: (BleGattEvent) -> Unit,
    ) : BleGattLease {
        private val operations = AndroidGattOperationGate()
        private val systemBond = AndroidSystemBondCoordinator()
        private val bondAttemptGate = AndroidSystemBondAttemptGate(systemBondGeneration)
        private val bondPrerequisite = AndroidLeaseBondPrerequisite()
        private val profileReadyGate = AndroidDeferredProfileReadyGate()
        private var queuedProfileReadyGatt: BluetoothGatt? = null
        private val profileReadyRunnable = Runnable {
            val expectedGatt = queuedProfileReadyGatt
            queuedProfileReadyGatt = null
            if (!profileReadyGate.deliver()) return@Runnable
            if (expectedGatt == null || !owns(expectedGatt)) {
                return@Runnable
            }
            observer(BleGattEvent.ProfileReady)
        }
        private val serviceChangedDiscovery = AndroidServiceChangedDiscoveryGate()
        private var serviceChangedFailureGatt: BluetoothGatt? = null
        private var serviceRediscoveryGatt: BluetoothGatt? = null
        private val serviceChangedFailureGrace = Runnable {
            val expectedGatt = serviceChangedFailureGatt
            serviceChangedFailureGatt = null
            if (expectedGatt == null || !owns(expectedGatt)) return@Runnable
            if (
                serviceChangedDiscovery.onFailureGraceExpired() ==
                AndroidServiceChangedDiscoveryAction.SCHEDULE_REDISCOVERY
            ) {
                scheduleServiceRediscovery(expectedGatt)
            } else {
                fail(BleGattFailure.PLATFORM_FAILURE)
            }
        }
        private val serviceRediscovery = Runnable {
            val expectedGatt = serviceRediscoveryGatt
            serviceRediscoveryGatt = null
            if (expectedGatt == null || !owns(expectedGatt)) return@Runnable
            if (!requireConnectCallbackPermission()) return@Runnable
            if (!serviceChangedDiscovery.beginRediscovery() || !retryDiscoveryAfterServiceChanged()) {
                fail(BleGattFailure.PLATFORM_FAILURE)
            }
        }
        private var leaseClosed = false
        private var bondReceiverRegistered = false
        private var gatt: BluetoothGatt? = null
        private var bondRequiredAfterConnect = false
        private var protocolInfo: BluetoothGattCharacteristic? = null
        private var command: BluetoothGattCharacteristic? = null
        private var stream: BluetoothGattCharacteristic? = null
        private var streamCccd: BluetoothGattDescriptor? = null
        private val bondTimeout = Runnable {
            if (
                leaseClosed ||
                activeGatt !== this ||
                !bondAttemptGate.allows(systemBondGeneration)
            ) return@Runnable
            handleBondAction(systemBond.fail(AndroidSystemBondFailure.TIMEOUT))
        }
        private var bondPollActive = false
        private var bondingObserved = false
        private val bondStatePoll = object : Runnable {
            override fun run() {
                if (
                    leaseClosed ||
                    activeGatt !== this@PlatformGattLease ||
                    !bondPollActive ||
                    !bondReceiverRegistered ||
                    !bondAttemptGate.allows(systemBondGeneration)
                ) return
                val state = currentSystemBondState()
                if (state == null) {
                    handleBondAction(systemBond.fail(AndroidSystemBondFailure.PERMISSION_LOST))
                    return
                }
                handleObservedBondState(state, fromPoll = true)
                if (
                    !leaseClosed &&
                    activeGatt === this@PlatformGattLease &&
                    bondPollActive &&
                    bondReceiverRegistered &&
                    !mainHandler.postDelayed(this, ANDROID_SYSTEM_BOND_POLL_INTERVAL_MILLIS)
                ) {
                    handleBondAction(systemBond.fail(AndroidSystemBondFailure.START_REJECTED))
                }
            }
        }
        private val bondReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                postToMain { handleBondBroadcast(intent) }
            }
        }

        private val callback = object : BluetoothGattCallback() {
            override fun onConnectionStateChange(callbackGatt: BluetoothGatt, status: Int, newState: Int) {
                postToMain { handleConnectionState(callbackGatt, status, newState) }
            }

            override fun onServicesDiscovered(callbackGatt: BluetoothGatt, status: Int) {
                postToMain { handleServicesDiscovered(callbackGatt, status) }
            }

            override fun onMtuChanged(callbackGatt: BluetoothGatt, mtu: Int, status: Int) {
                postToMain {
                    if (!owns(callbackGatt)) return@postToMain
                    if (!requireProtectedCallbackPrerequisites()) return@postToMain
                    val statusFailure = AndroidGattStatusPolicy.failure(status)
                    if (statusFailure != null || !operations.acceptMtu()) {
                        fail(statusFailure ?: BleGattFailure.PLATFORM_FAILURE)
                    } else observer(BleGattEvent.MtuChanged(mtu))
                }
            }

            override fun onCharacteristicRead(
                callbackGatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
                status: Int,
            ) {
                handleProtocolInfoRead(callbackGatt, characteristic, value, status)
            }

            @Suppress("DEPRECATION")
            override fun onCharacteristicRead(
                callbackGatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int,
            ) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
                val value = characteristic.value?.copyOf() ?: byteArrayOf()
                handleProtocolInfoRead(callbackGatt, characteristic, value, status)
            }

            override fun onDescriptorWrite(
                callbackGatt: BluetoothGatt,
                descriptor: BluetoothGattDescriptor,
                status: Int,
            ) {
                postToMain {
                    if (!owns(callbackGatt)) return@postToMain
                    if (!requireProtectedCallbackPrerequisites()) return@postToMain
                    val statusFailure = AndroidGattStatusPolicy.failure(status)
                    if (
                        !AndroidGattCharacteristicOwnershipPolicy.owns(streamCccd, descriptor) ||
                        !AndroidGattCharacteristicOwnershipPolicy.owns(stream, descriptor.characteristic) ||
                        statusFailure != null ||
                        !operations.acceptIndicationSubscription()
                    ) {
                        fail(statusFailure ?: BleGattFailure.PLATFORM_FAILURE)
                    } else observer(BleGattEvent.StreamIndicationsSubscribed)
                }
            }

            override fun onCharacteristicChanged(
                callbackGatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
            ) {
                handleStreamIndication(callbackGatt, characteristic, value)
            }

            @Suppress("DEPRECATION")
            override fun onCharacteristicChanged(
                callbackGatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
            ) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
                handleStreamIndication(callbackGatt, characteristic, characteristic.value?.copyOf() ?: byteArrayOf())
            }

            override fun onCharacteristicWrite(
                callbackGatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int,
            ) {
                postToMain {
                    if (!owns(callbackGatt)) return@postToMain
                    if (!requireProtectedCallbackPrerequisites()) return@postToMain
                    val statusFailure = AndroidGattStatusPolicy.failure(status)
                    if (
                        !AndroidGattCharacteristicOwnershipPolicy.owns(command, characteristic) ||
                        statusFailure != null ||
                        !operations.acceptCommandWrite()
                    ) fail(statusFailure ?: BleGattFailure.TRANSIENT_LINK)
                }
            }

            override fun onServiceChanged(callbackGatt: BluetoothGatt) {
                postToMain { handleServiceChanged(callbackGatt) }
            }
        }

        override fun start(): Boolean {
            if (
                !onMainThread() ||
                leaseClosed ||
                Build.VERSION.SDK_INT < ANDROID_BLE_MINIMUM_API ||
                !operationAllowed(AndroidBlePlatformOperation.CONNECT) ||
                !operations.beginConnection()
            ) return false
            val alreadyBonded = bondedPrerequisiteSatisfied()
            if (!allowSystemBondCreation && !alreadyBonded) {
                fail(BleGattFailure.BOND_REQUIRED)
                return true
            }
            return when (
                val action = systemBond.start(
                    endpointToken = endpointToken,
                    generation = systemBondGeneration,
                    alreadyBonded = alreadyBonded,
                )
            ) {
                AndroidSystemBondAction.RequestSystemBond -> {
                    bondRequiredAfterConnect = true
                    openGatt()
                }
                AndroidSystemBondAction.ProceedToGatt -> openGatt()
                AndroidSystemBondAction.Await -> rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
                is AndroidSystemBondAction.Failed -> rejectBondStart(action.reason)
            }
        }

        @SuppressLint("MissingPermission")
        private fun requestSystemBond(): Boolean {
            val receiverFailure = registerBondReceiver()
            if (receiverFailure != null) return rejectBondStart(receiverFailure)
            if (!bondAttemptGate.begin()) return rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
            if (!mainHandler.postDelayed(bondTimeout, ANDROID_SYSTEM_BOND_TIMEOUT_MILLIS)) {
                return rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
            }
            return try {
                if (device.createBond()) {
                    bondPollActive = true
                    if (mainHandler.post(bondStatePoll)) {
                        true
                    } else {
                        rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
                    }
                } else {
                    rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
                }
            } catch (_: SecurityException) {
                rejectBondStart(AndroidSystemBondFailure.PERMISSION_LOST)
            } catch (_: IllegalStateException) {
                rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
            }
        }

        private fun rejectBondStart(reason: AndroidSystemBondFailure): Boolean {
            handleBondAction(systemBond.fail(reason))
            return false
        }

        @SuppressLint("MissingPermission")
        private fun openGatt(): Boolean = try {
                val opened = device.connectGatt(
                    appContext,
                    AndroidBlePlatformPlan.AUTO_CONNECT,
                    callback,
                    BluetoothDevice.TRANSPORT_LE,
                    BluetoothDevice.PHY_LE_1M_MASK,
                    mainHandler,
                ) ?: return false
                gatt = opened
                observer(BleGattEvent.GattOpened)
                true
            } catch (_: SecurityException) {
                false
            } catch (_: IllegalArgumentException) {
                false
            }

        private fun registerBondReceiver(): AndroidSystemBondFailure? {
            if (bondReceiverRegistered || leaseClosed || activeGatt !== this) {
                return AndroidSystemBondFailure.START_REJECTED
            }
            return try {
                val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    // Bluetooth is a highly privileged framework app, not the system UID. Android
                    // therefore requires an exported context receiver for its protected broadcasts.
                    appContext.registerReceiver(bondReceiver, filter, Context.RECEIVER_EXPORTED)
                } else {
                    @Suppress("DEPRECATION")
                    appContext.registerReceiver(bondReceiver, filter)
                }
                bondReceiverRegistered = true
                null
            } catch (_: SecurityException) {
                AndroidSystemBondFailure.PERMISSION_LOST
            } catch (_: IllegalArgumentException) {
                AndroidSystemBondFailure.START_REJECTED
            }
        }

        @Suppress("DEPRECATION")
        private fun handleBondBroadcast(intent: Intent?) {
            if (
                leaseClosed ||
                activeGatt !== this ||
                !bondAttemptGate.allows(systemBondGeneration)
            ) return
            if (!operationAllowed(AndroidBlePlatformOperation.CONNECT)) {
                handleBondAction(systemBond.fail(AndroidSystemBondFailure.PERMISSION_LOST))
                return
            }
            if (intent?.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) {
                handleBondAction(systemBond.fail(AndroidSystemBondFailure.CALLBACK_MISMATCH))
                return
            }
            val callbackDevice = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE) as? BluetoothDevice
            if (callbackDevice != device) {
                handleBondAction(systemBond.fail(AndroidSystemBondFailure.CALLBACK_MISMATCH))
                return
            }
            val state = when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, Int.MIN_VALUE)) {
                BluetoothDevice.BOND_NONE -> AndroidSystemBondState.NONE
                BluetoothDevice.BOND_BONDING -> AndroidSystemBondState.BONDING
                BluetoothDevice.BOND_BONDED -> AndroidSystemBondState.BONDED
                else -> {
                    handleBondAction(systemBond.fail(AndroidSystemBondFailure.CALLBACK_MISMATCH))
                    return
                }
            }
            handleObservedBondState(state, fromPoll = false)
        }

        private fun handleObservedBondState(state: AndroidSystemBondState, fromPoll: Boolean) {
            if (!bondAttemptGate.allows(systemBondGeneration)) return
            if (state == AndroidSystemBondState.BONDING) bondingObserved = true
            if (fromPoll && !AndroidSystemBondPollingPolicy.shouldForward(state, bondingObserved)) return
            handleBondAction(systemBond.onBondState(endpointToken, systemBondGeneration, state))
        }

        private fun handleBondAction(action: AndroidSystemBondAction) {
            when (action) {
                AndroidSystemBondAction.Await,
                AndroidSystemBondAction.RequestSystemBond,
                -> Unit
                AndroidSystemBondAction.ProceedToGatt -> {
                    bondPrerequisite.latchFreshAttemptProof()
                    bondRequiredAfterConnect = false
                    cleanupBondAttempt()
                    if (!discoverServicesOnOwnedGatt()) fail(BleGattFailure.PLATFORM_FAILURE)
                }
                is AndroidSystemBondAction.Failed -> {
                    cleanupBondAttempt()
                    fail(
                        if (action.reason == AndroidSystemBondFailure.PERMISSION_LOST) {
                            BleGattFailure.PERMISSION_REVOKED
                        } else {
                            BleGattFailure.BOND_REQUIRED
                        },
                    )
                }
            }
        }

        private fun cleanupBondAttempt() {
            bondAttemptGate.end()
            bondPollActive = false
            mainHandler.removeCallbacks(bondStatePoll)
            mainHandler.removeCallbacks(bondTimeout)
            if (!bondReceiverRegistered) return
            bondReceiverRegistered = false
            try {
                appContext.unregisterReceiver(bondReceiver)
            } catch (_: IllegalArgumentException) {
                // The receiver may already have been removed during process or context teardown.
            }
        }

        // Each annotated call is guarded by operationAllowed and by SecurityException containment
        // in beginGattOperation; lint cannot follow that contract through the operation lambda.
        @SuppressLint("MissingPermission")
        override fun requestMtu(mtu: Int): Boolean =
            beginGattOperation(AndroidBlePlatformOperation.REQUEST_MTU, { operations.beginMtuRequest() }) {
                it.requestMtu(mtu)
            }

        @SuppressLint("MissingPermission")
        override fun readProtocolInfo(): Boolean =
            beginGattOperation(AndroidBlePlatformOperation.READ_PROTOCOL_INFO, { operations.beginProtocolInfoRead() }) { current ->
                val characteristic = protocolInfo ?: return@beginGattOperation false
                current.readCharacteristic(characteristic)
            }

        @SuppressLint("MissingPermission")
        override fun subscribeStreamIndications(): Boolean =
            beginGattOperation(
                AndroidBlePlatformOperation.ENABLE_STREAM_INDICATIONS,
                { operations.beginIndicationSubscription() },
            ) { current ->
                val characteristic = stream ?: return@beginGattOperation false
                val descriptor = streamCccd ?: return@beginGattOperation false
                current.setCharacteristicNotification(characteristic, true) && writeIndicationDescriptor(current, descriptor)
            }

        override fun writeCommandWithResponse(value: ByteArray): Boolean {
            if (value.isEmpty() || value.size > COMPANION_FRAGMENT_HEADER_BYTES + COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) {
                return false
            }
            return beginGattOperation(AndroidBlePlatformOperation.WRITE_COMMAND, { operations.beginCommandWrite() }) { current ->
                val characteristic = command ?: return@beginGattOperation false
                writeCommand(current, characteristic, value)
            }
        }

        override fun close() {
            if (!onMainThread()) {
                mainHandler.post { close() }
                return
            }
            if (leaseClosed) return
            leaseClosed = true
            mainHandler.removeCallbacks(profileReadyRunnable)
            queuedProfileReadyGatt = null
            profileReadyGate.close()
            mainHandler.removeCallbacks(serviceChangedFailureGrace)
            mainHandler.removeCallbacks(serviceRediscovery)
            serviceChangedFailureGatt = null
            serviceRediscoveryGatt = null
            serviceChangedDiscovery.close()
            systemBond.fail(AndroidSystemBondFailure.LIFECYCLE_ENDED)
            cleanupBondAttempt()
            bondPrerequisite.clear()
            bondRequiredAfterConnect = false
            operations.close()
            val current = gatt
            gatt = null
            try {
                try {
                    if (operationAllowed(AndroidBlePlatformOperation.DISCONNECT)) {
                        current?.disconnect()
                    }
                } catch (_: SecurityException) {
                    // Permission may be revoked between callback and cleanup.
                } catch (_: IllegalStateException) {
                    // The platform stack may already be tearing down.
                }
                try {
                    current?.close()
                } catch (_: SecurityException) {
                    // Android 12+ also guards close with BLUETOOTH_CONNECT.
                } catch (_: IllegalStateException) {
                    // The platform stack may already be closed.
                }
            } finally {
                protocolInfo = null
                command = null
                stream = null
                streamCccd = null
                if (activeGatt === this) activeGatt = null
            }
        }

        private fun handleConnectionState(callbackGatt: BluetoothGatt, status: Int, newState: Int) {
            if (!owns(callbackGatt)) return
            if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                disconnectEvent()
                return
            }
            if (!requireConnectCallbackPermission()) return
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                if (bondRequiredAfterConnect) {
                    if (!bondAttemptGate.allows(systemBondGeneration) && !requestSystemBond()) {
                        fail(BleGattFailure.PLATFORM_FAILURE)
                    }
                } else if (!discoverServicesOnOwnedGatt()) {
                    fail(BleGattFailure.PLATFORM_FAILURE)
                }
            } else if (status != BluetoothGatt.GATT_SUCCESS) {
                fail(BleGattFailure.TRANSIENT_LINK)
            }
        }

        @SuppressLint("MissingPermission")
        private fun discoverServicesOnOwnedGatt(): Boolean {
            val current = gatt ?: return false
            if (!owns(current) || !operationAllowed(AndroidBlePlatformOperation.DISCOVER_SERVICES)) return false
            if (!bondedPrerequisiteSatisfied() || !operations.beginDiscovery()) return false
            return requestServiceDiscovery(current)
        }

        @SuppressLint("MissingPermission")
        private fun retryDiscoveryAfterServiceChanged(): Boolean {
            val current = gatt ?: return false
            if (!owns(current) || !operationAllowed(AndroidBlePlatformOperation.DISCOVER_SERVICES)) return false
            if (!bondedPrerequisiteSatisfied() || !operations.retryDiscoveryAfterServiceChanged()) return false
            return requestServiceDiscovery(current)
        }

        @SuppressLint("MissingPermission")
        private fun requestServiceDiscovery(current: BluetoothGatt): Boolean = try {
            current.discoverServices()
        } catch (_: SecurityException) {
            false
        } catch (_: IllegalStateException) {
            false
        }

        private fun handleServiceChanged(callbackGatt: BluetoothGatt) {
            if (!owns(callbackGatt)) return
            if (!requireConnectCallbackPermission()) return
            when (serviceChangedDiscovery.onServiceChanged()) {
                AndroidServiceChangedDiscoveryAction.WAIT -> Unit
                AndroidServiceChangedDiscoveryAction.CANCEL_FAILURE_AND_SCHEDULE_REDISCOVERY -> {
                    mainHandler.removeCallbacks(serviceChangedFailureGrace)
                    serviceChangedFailureGatt = null
                    scheduleServiceRediscovery(callbackGatt)
                }
                else -> fail(BleGattFailure.PLATFORM_FAILURE)
            }
        }

        private fun scheduleServiceRediscovery(callbackGatt: BluetoothGatt) {
            protocolInfo = null
            command = null
            stream = null
            streamCccd = null
            serviceRediscoveryGatt = callbackGatt
            if (!mainHandler.post(serviceRediscovery)) {
                serviceRediscoveryGatt = null
                fail(BleGattFailure.PLATFORM_FAILURE)
            }
        }

        private fun handleServicesDiscovered(callbackGatt: BluetoothGatt, status: Int) {
            if (!owns(callbackGatt)) return
            if (profileReadyGate.hasStarted()) return
            if (!operationAllowed(AndroidBlePlatformOperation.DISCOVER_SERVICES)) {
                fail(BleGattFailure.PERMISSION_REVOKED)
                return
            }
            val discovered = if (status == BluetoothGatt.GATT_SUCCESS) {
                try {
                    val service = callbackGatt.getService(gattServiceUuid)
                    DiscoveredGattProfile(
                        service != null,
                        service?.getCharacteristic(protocolInfoUuid),
                        service?.getCharacteristic(commandUuid),
                        service?.getCharacteristic(streamUuid),
                    )
                } catch (_: SecurityException) {
                    fail(BleGattFailure.PERMISSION_REVOKED)
                    return
                }
            } else {
                DiscoveredGattProfile(false, null, null, null)
            }
            val info = discovered.protocolInfo
            val commandCharacteristic = discovered.command
            val streamCharacteristic = discovered.stream
            val cccd = try {
                streamCharacteristic?.getDescriptor(cccdUuid)
            } catch (_: SecurityException) {
                fail(BleGattFailure.PERMISSION_REVOKED)
                return
            }
            val profile = AndroidGattProfileSnapshot(
                hasService = discovered.hasService,
                protocolInfoReadable = info.hasProperty(BluetoothGattCharacteristic.PROPERTY_READ),
                commandWriteWithResponse = commandCharacteristic.hasProperty(BluetoothGattCharacteristic.PROPERTY_WRITE),
                commandWriteWithoutResponse = commandCharacteristic.hasProperty(
                    BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE,
                ),
                streamIndicate = streamCharacteristic.hasProperty(BluetoothGattCharacteristic.PROPERTY_INDICATE),
                streamHasClientConfigurationDescriptor = cccd != null,
            )
            val profileAccepted = status == BluetoothGatt.GATT_SUCCESS && AndroidGattProfilePolicy.accepts(profile)
            when (serviceChangedDiscovery.onDiscoveryResult(profileAccepted)) {
                AndroidServiceChangedDiscoveryAction.ACCEPT_PROFILE -> {
                    if (!profileAccepted || !operations.acceptProfile()) {
                        fail(BleGattFailure.PLATFORM_FAILURE)
                        return
                    }
                    protocolInfo = info
                    command = commandCharacteristic
                    stream = streamCharacteristic
                    streamCccd = cccd
                    if (!bondedPrerequisiteSatisfied()) {
                        fail(BleGattFailure.BOND_REQUIRED)
                        return
                    }
                    if (!profileReadyGate.begin()) {
                        fail(BleGattFailure.PLATFORM_FAILURE)
                        return
                    }
                    queuedProfileReadyGatt = callbackGatt
                    if (!mainHandler.post(profileReadyRunnable)) {
                        queuedProfileReadyGatt = null
                        if (profileReadyGate.rejectPost()) {
                            fail(BleGattFailure.PLATFORM_FAILURE)
                        }
                    }
                }
                AndroidServiceChangedDiscoveryAction.SCHEDULE_FAILURE_GRACE -> {
                    serviceChangedFailureGatt = callbackGatt
                    if (!mainHandler.postDelayed(
                            serviceChangedFailureGrace,
                            ANDROID_SERVICE_CHANGED_FAILURE_GRACE_MILLIS,
                        )
                    ) {
                        serviceChangedFailureGatt = null
                        serviceChangedDiscovery.onFailureGraceExpired()
                        fail(BleGattFailure.PLATFORM_FAILURE)
                    }
                }
                AndroidServiceChangedDiscoveryAction.SCHEDULE_REDISCOVERY ->
                    scheduleServiceRediscovery(callbackGatt)
                else -> fail(BleGattFailure.PLATFORM_FAILURE)
            }
        }
        private fun handleProtocolInfoRead(
            callbackGatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            postToMain {
                if (!owns(callbackGatt)) return@postToMain
                when (
                    AndroidProtectedProtocolInfoReadPolicy.evaluate(
                        active = owns(callbackGatt),
                        connectPermissionGranted = operationAllowed(AndroidBlePlatformOperation.READ_PROTOCOL_INFO),
                        bondedPrerequisite = bondedPrerequisiteSatisfied(),
                        exactCharacteristic = AndroidGattCharacteristicOwnershipPolicy.owns(protocolInfo, characteristic),
                        status = status,
                        valueBytes = value.size,
                    )
                ) {
                    AndroidProtectedReadAdmission.IGNORE -> Unit
                    AndroidProtectedReadAdmission.ACCEPT -> {
                        if (!operations.acceptProtocolInfo()) {
                            fail(BleGattFailure.PLATFORM_FAILURE)
                        } else {
                            observer(BleGattEvent.ProtectedProtocolInfoRead(value.copyOf()))
                        }
                    }
                    AndroidProtectedReadAdmission.PERMISSION_REVOKED -> fail(BleGattFailure.PERMISSION_REVOKED)
                    AndroidProtectedReadAdmission.BOND_REQUIRED -> fail(BleGattFailure.BOND_REQUIRED)
                    AndroidProtectedReadAdmission.SECURITY_REQUIRED -> fail(BleGattFailure.SECURITY_REJECTED)
                    AndroidProtectedReadAdmission.AUTHORIZATION_REJECTED ->
                        fail(BleGattFailure.AUTHORIZATION_REJECTED)
                    AndroidProtectedReadAdmission.PLATFORM_FAILURE -> fail(BleGattFailure.PLATFORM_FAILURE)
                }
            }
        }

        private fun handleStreamIndication(
            callbackGatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            postToMain {
                if (!owns(callbackGatt)) return@postToMain
                if (!requireProtectedCallbackPrerequisites()) return@postToMain
                if (
                    !AndroidGattCharacteristicOwnershipPolicy.owns(stream, characteristic) ||
                    !operations.acceptsStreamIndication()
                ) {
                    fail(BleGattFailure.PLATFORM_FAILURE)
                } else observer(BleGattEvent.StreamIndication(value.copyOf()))
            }
        }

        @SuppressLint("MissingPermission")
        @Suppress("DEPRECATION")
        private fun writeIndicationDescriptor(current: BluetoothGatt, descriptor: BluetoothGattDescriptor): Boolean =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                current.writeDescriptor(descriptor, AndroidBlePlatformPlan.enableIndicationValue()) ==
                    BluetoothStatusCodes.SUCCESS
            } else {
                descriptor.value = AndroidBlePlatformPlan.enableIndicationValue()
                current.writeDescriptor(descriptor)
            }

        @SuppressLint("MissingPermission")
        @Suppress("DEPRECATION")
        private fun writeCommand(
            current: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            current.writeCharacteristic(
                characteristic,
                value.copyOf(),
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            characteristic.value = value.copyOf()
            current.writeCharacteristic(characteristic)
        }

        private fun beginGattOperation(
            platformOperation: AndroidBlePlatformOperation,
            advance: () -> Boolean,
            operation: (BluetoothGatt) -> Boolean,
        ): Boolean {
            if (
                !onMainThread() ||
                leaseClosed ||
                Build.VERSION.SDK_INT < ANDROID_BLE_MINIMUM_API ||
                !operationAllowed(platformOperation) ||
                !bondedPrerequisiteSatisfied()
            ) return false
            val current = gatt ?: return false
            if (!advance()) return false
            return try {
                operation(current)
            } catch (_: SecurityException) {
                false
            } catch (_: IllegalArgumentException) {
                false
            }
        }

        private fun owns(callbackGatt: BluetoothGatt): Boolean =
            !leaseClosed && activeGatt === this && gatt === callbackGatt

        private fun requireConnectCallbackPermission(): Boolean {
            if (operationAllowed(AndroidBlePlatformOperation.CONNECT)) return true
            fail(BleGattFailure.PERMISSION_REVOKED)
            return false
        }

        @SuppressLint("MissingPermission")
        private fun bondedPrerequisiteSatisfied(): Boolean =
            bondPrerequisite.isSatisfied(currentSystemBondState())

        @SuppressLint("MissingPermission")
        private fun currentSystemBondState(): AndroidSystemBondState? {
            if (!operationAllowed(AndroidBlePlatformOperation.CONNECT)) return null
            return try {
                when (device.bondState) {
                    BluetoothDevice.BOND_NONE -> AndroidSystemBondState.NONE
                    BluetoothDevice.BOND_BONDING -> AndroidSystemBondState.BONDING
                    BluetoothDevice.BOND_BONDED -> AndroidSystemBondState.BONDED
                    else -> null
                }
            } catch (_: SecurityException) {
                null
            }
        }

        private fun requireProtectedCallbackPrerequisites(): Boolean {
            if (!requireConnectCallbackPermission()) return false
            if (bondedPrerequisiteSatisfied()) return true
            fail(BleGattFailure.BOND_REQUIRED)
            return false
        }

        private fun fail(failure: BleGattFailure) {
            if (leaseClosed) return
            close()
            observer(BleGattEvent.Failed(failure))
        }

        private fun disconnectEvent() {
            if (leaseClosed) return
            close()
            observer(BleGattEvent.Disconnected)
        }
    }

    private data class DiscoveredGattProfile(
        val hasService: Boolean,
        val protocolInfo: BluetoothGattCharacteristic?,
        val command: BluetoothGattCharacteristic?,
        val stream: BluetoothGattCharacteristic?,
    )
}

private fun BluetoothGattCharacteristic?.hasProperty(property: Int): Boolean =
    this != null && (properties and property) != 0
