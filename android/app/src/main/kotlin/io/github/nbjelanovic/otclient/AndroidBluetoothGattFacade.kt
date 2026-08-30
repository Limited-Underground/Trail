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
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import io.github.nbjelanovic.otprotocol.COMPANION_FRAGMENT_HEADER_BYTES
import io.github.nbjelanovic.otprotocol.COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES
import java.util.UUID

internal const val ANDROID_BLE_SCAN_WINDOW_MILLIS = AndroidBlePlatformPlan.SCAN_WINDOW_MILLIS
private const val CLIENT_CONFIGURATION_UUID = "00002902-0000-1000-8000-00805f9b34fb"
private const val BLUETOOTH_SCAN_PERMISSION = "android.permission.BLUETOOTH_SCAN"
private const val BLUETOOTH_CONNECT_PERMISSION = "android.permission.BLUETOOTH_CONNECT"
internal const val ANDROID_SYSTEM_BOND_TIMEOUT_MILLIS = 30_000L

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
) : AndroidBluetoothFacade, AutoCloseable {
    private val appContext = context.applicationContext
    private val serviceUuid = UUID.fromString(CompanionGattV0Contract.SERVICE_UUID)
    private val protocolInfoUuid = UUID.fromString(CompanionGattV0Contract.PROTOCOL_INFO_UUID)
    private val commandUuid = UUID.fromString(CompanionGattV0Contract.COMMAND_UUID)
    private val streamUuid = UUID.fromString(CompanionGattV0Contract.STREAM_UUID)
    private val cccdUuid = UUID.fromString(CLIENT_CONFIGURATION_UUID)
    private val candidates = LinkedHashMap<BluetoothDevice, CandidateBinding>()
    private var activeScan: PlatformScanLease? = null
    private var activeGatt: PlatformGattLease? = null
    private var systemBondGeneration: Long = 0
    private var closed = false

    private data class CandidateBinding(
        val endpointToken: String,
        val publicLabel: String,
    )

    override fun preflight(): BlePreflight = AndroidBlePreflightPolicy.evaluate(platformSnapshot())

    override fun createScan(observer: (BleScanEvent) -> Unit): BleScanLease? {
        if (!onMainThread() || closed || !preflight().isReady || activeScan != null || activeGatt != null) return null
        candidates.clear()
        return PlatformScanLease(observer).also { activeScan = it }
    }

    override fun createConnection(endpointToken: String, observer: (BleGattEvent) -> Unit): BleGattLease? {
        if (!onMainThread() || closed || !preflight().isReady || activeScan != null || activeGatt != null) return null
        val device = candidates.entries.singleOrNull { it.value.endpointToken == endpointToken }?.key ?: return null
        val generation = nextSystemBondGeneration() ?: return null
        return PlatformGattLease(endpointToken, generation, device, observer).also { activeGatt = it }
    }

    override fun close() {
        if (!onMainThread()) {
            mainHandler.post { close() }
            return
        }
        if (closed) return
        closed = true
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
    ) : BleScanLease {
        private var started = false
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
            val scanner = bluetoothManager()?.adapter?.bluetoothLeScanner ?: return false
            val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(serviceUuid)).build()
            val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_BALANCED).build()
            return try {
                scanner.startScan(listOf(filter), settings, callback)
                started = true
                if (!mainHandler.postDelayed(timeout, ANDROID_BLE_SCAN_WINDOW_MILLIS)) {
                    closePlatform()
                    return false
                }
                true
            } catch (_: SecurityException) {
                false
            } catch (_: IllegalStateException) {
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
                    active = !leaseClosed && started && activeScan === this,
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
            val advertisedServices = result.scanRecord?.serviceUuids ?: return
            if (advertisedServices.none { it.uuid == serviceUuid }) return
            val existing = candidates[result.device]
            val binding = existing ?: if (candidates.size < MAX_DISCOVERED_COMPANIONS) {
                val endpointToken = OpaqueEndpointTokenPolicy.generate(
                    existing = candidates.values.map(CandidateBinding::endpointToken),
                    factory = tokenFactory,
                ) ?: return
                CandidateBinding(
                    endpointToken = endpointToken,
                    publicLabel = "Nearby compatible device ${candidates.size + 1}",
                ).also { candidates[result.device] = it }
            } else return
            observer(BleScanEvent.Candidate(BleDiscoveredCompanion(binding.endpointToken, binding.publicLabel)))
        }

        private fun finish() {
            if (leaseClosed) return
            val permissionRevoked = !operationAllowed(AndroidBlePlatformOperation.START_SCAN)
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
            if (started && operationAllowed(AndroidBlePlatformOperation.STOP_SCAN)) {
                try {
                    bluetoothManager()?.adapter?.bluetoothLeScanner?.stopScan(callback)
                } catch (_: SecurityException) {
                    // Closing remains best-effort after permission revocation.
                } catch (_: IllegalStateException) {
                    // The platform scanner may already be unavailable.
                }
            }
            started = false
            if (activeScan === this) activeScan = null
        }
    }

    private inner class PlatformGattLease(
        private val endpointToken: String,
        private val systemBondGeneration: Long,
        private val device: BluetoothDevice,
        private val observer: (BleGattEvent) -> Unit,
    ) : BleGattLease {
        private val operations = AndroidGattOperationGate()
        private val systemBond = AndroidSystemBondCoordinator()
        private var leaseClosed = false
        private var bondReceiverRegistered = false
        private var gatt: BluetoothGatt? = null
        private var protocolInfo: BluetoothGattCharacteristic? = null
        private var command: BluetoothGattCharacteristic? = null
        private var stream: BluetoothGattCharacteristic? = null
        private var streamCccd: BluetoothGattDescriptor? = null
        private val bondTimeout = Runnable {
            if (leaseClosed || activeGatt !== this) return@Runnable
            handleBondAction(systemBond.fail(AndroidSystemBondFailure.TIMEOUT))
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
                postToMain {
                    if (!owns(callbackGatt)) return@postToMain
                    if (!requireConnectCallbackPermission()) return@postToMain
                    fail(BleGattFailure.PLATFORM_FAILURE)
                }
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
            return when (
                val action = systemBond.start(
                    endpointToken = endpointToken,
                    generation = systemBondGeneration,
                    alreadyBonded = bondedPrerequisiteSatisfied(),
                )
            ) {
                AndroidSystemBondAction.RequestSystemBond -> requestSystemBond()
                AndroidSystemBondAction.ProceedToGatt -> openGatt()
                AndroidSystemBondAction.Await -> rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
                is AndroidSystemBondAction.Failed -> rejectBondStart(action.reason)
            }
        }

        @SuppressLint("MissingPermission")
        private fun requestSystemBond(): Boolean {
            val receiverFailure = registerBondReceiver()
            if (receiverFailure != null) return rejectBondStart(receiverFailure)
            if (!mainHandler.postDelayed(bondTimeout, ANDROID_SYSTEM_BOND_TIMEOUT_MILLIS)) {
                return rejectBondStart(AndroidSystemBondFailure.START_REJECTED)
            }
            return try {
                if (device.createBond()) {
                    true
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
                    appContext.registerReceiver(bondReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
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
            if (leaseClosed || activeGatt !== this) return
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
            handleBondAction(systemBond.onBondState(endpointToken, systemBondGeneration, state))
        }

        private fun handleBondAction(action: AndroidSystemBondAction) {
            when (action) {
                AndroidSystemBondAction.Await,
                AndroidSystemBondAction.RequestSystemBond,
                -> Unit
                AndroidSystemBondAction.ProceedToGatt -> {
                    cleanupBondAttempt()
                    if (!openGatt()) fail(BleGattFailure.PLATFORM_FAILURE)
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
            systemBond.fail(AndroidSystemBondFailure.LIFECYCLE_ENDED)
            cleanupBondAttempt()
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
                if (!operations.beginDiscovery()) {
                    fail(BleGattFailure.PLATFORM_FAILURE)
                    return
                }
                val started = try {
                    callbackGatt.discoverServices()
                } catch (_: SecurityException) {
                    false
                }
                if (!started) fail(BleGattFailure.PLATFORM_FAILURE)
            } else if (status != BluetoothGatt.GATT_SUCCESS) {
                fail(BleGattFailure.TRANSIENT_LINK)
            }
        }

        private fun handleServicesDiscovered(callbackGatt: BluetoothGatt, status: Int) {
            if (!owns(callbackGatt) || status != BluetoothGatt.GATT_SUCCESS) {
                fail(BleGattFailure.PLATFORM_FAILURE)
                return
            }
            if (!operationAllowed(AndroidBlePlatformOperation.DISCOVER_SERVICES)) {
                fail(BleGattFailure.PERMISSION_REVOKED)
                return
            }
            val discovered = try {
                val service = callbackGatt.getService(serviceUuid)
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
            if (!AndroidGattProfilePolicy.accepts(profile) || !operations.acceptProfile()) {
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
            observer(BleGattEvent.ProfileReady)
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
        private fun bondedPrerequisiteSatisfied(): Boolean {
            if (!operationAllowed(AndroidBlePlatformOperation.CONNECT)) return false
            return try {
                device.bondState == BluetoothDevice.BOND_BONDED
            } catch (_: SecurityException) {
                false
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
