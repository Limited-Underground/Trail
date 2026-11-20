package io.github.nbjelanovic.otclient

import android.content.Context
import android.os.SystemClock
import io.github.nbjelanovic.otprotocol.CompanionPairingReason
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import kotlin.coroutines.resume
import kotlinx.coroutines.suspendCancellableCoroutine

/** Process-owned bounded background writer. No unprojected runtime state is queued. */
internal class MeshTrailOperationLogging private constructor(context: Context) {
    private val store by lazy {
        runCatching { MeshTrailOperationLog(context.noBackupFilesDir.resolve("meshtrail-operation-log")) }.getOrNull()
    }
    private val startedAt = SystemClock.elapsedRealtime()
    private val writer = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, ArrayBlockingQueue(128)).apply {
        allowCoreThreadTimeOut(true)
    }

    fun appOpened() {
        record(MeshTrailLogKind.APP, "opened")
        record(MeshTrailLogKind.APP_VERSION, BuildConfig.VERSION_CODE.toString())
    }

    fun appObservation(): MeshTrailLogTransitions = MeshTrailLogTransitions(::record)

    private val recoveryLog = MeshTrailLogTransitions(::record)

    fun recordBluetoothRecovery(trigger: BluetoothRecoveryTrigger) = recoveryLog.observeBluetoothRecovery(trigger)

    fun recordBluetoothRetryRefusal(reason: BluetoothRecoveryRefusal) = recoveryLog.observeBluetoothRetryRefusal(reason)

    fun serviceObservation(variant: ConnectedDeviceSessionLifecycleObserver?) =
        object : ConnectedDeviceSessionLifecycleObserver {
            private val transitions = MeshTrailLogTransitions(::record)

            override fun onState(state: TrailAppUiState.BluetoothDevice) {
                try {
                    transitions.observeService(state)
                } finally {
                    variant?.onState(state)
                }
            }

            override fun close() {
                try {
                    record(MeshTrailLogKind.SERVICE, "owner closed")
                } finally {
                    variant?.close()
                }
            }
        }

    // Called only at the future protected F11 receive boundary, after transport/session checks.
    // No polling, local fixture or radio action is installed here.
    fun recordDeviceParameters(record: ByteArray) {
        val seconds = seconds()
        if (record.size != io.github.nbjelanovic.otprotocol.CompanionDeviceParametersCodec.RECORD_BYTES) {
            record(MeshTrailLogKind.PARAMETERS, "invalid record")
            return
        }
        val copy = record.copyOf()
        enqueue { store?.recordDeviceParameters(seconds, copy) }
    }

    suspend fun exportText(): String? = suspendCancellableCoroutine { continuation ->
        if (!enqueue { continuation.resume(runCatching { store?.exportText() }.getOrNull()) }) continuation.resume(null)
    }

    private fun record(kind: MeshTrailLogKind, value: String) {
        val seconds = seconds()
        enqueue { store?.append(seconds, kind, value) }
    }

    private fun seconds() = (SystemClock.elapsedRealtime() - startedAt).coerceAtLeast(0L) / 1_000L

    private fun enqueue(job: () -> Unit): Boolean = try {
        writer.execute(job)
        true
    } catch (_: RejectedExecutionException) {
        // Logging never blocks a connection or grows its memory budget under load.
        false
    }

    companion object {
        private var instance: MeshTrailOperationLogging? = null

        @Synchronized
        fun from(context: Context): MeshTrailOperationLogging =
            instance ?: MeshTrailOperationLogging(context.applicationContext).also { instance = it }
    }
}

/** Map only closed state labels and reasons. Names, codes, handles and arbitrary errors stay out. */
internal class MeshTrailLogTransitions(private val append: (MeshTrailLogKind, String) -> Unit) {
    private var last = emptyMap<MeshTrailLogKind, String>()

    fun observeApp(state: TrailAppUiState) {
        val bluetooth = state as? TrailAppUiState.BluetoothDevice
        observe(buildMap {
            if (bluetooth?.permissionWasDenied == true) put(MeshTrailLogKind.APP, "Nearby Devices permission denied")
            bluetooth?.serviceFailure?.let { put(MeshTrailLogKind.SERVICE_REFUSED, it.name) }
            if (bluetooth?.serviceState != ConnectedDeviceServiceUiState.RUNNING &&
                bluetooth?.serviceState != ConnectedDeviceServiceUiState.BOUND_ONLY) {
                (bluetooth?.runtimeState as? BleRuntimeState.Blocked)?.let {
                    put(MeshTrailLogKind.CONNECTION_BLOCK, it.reason.name)
                }
            }
        })
    }

    fun observeService(state: TrailAppUiState.BluetoothDevice) {
        val runtime = state.runtimeState
        observe(buildMap {
            val connection = when (runtime) {
                BleRuntimeState.Inactive -> "inactive"
                BleRuntimeState.Idle -> "idle"
                is BleRuntimeState.Blocked -> "blocked"
                is BleRuntimeState.Scanning -> "scanning"
                is BleRuntimeState.ScanComplete -> "scan complete"
                BleRuntimeState.FindingReturningOwner -> "finding saved board"
                is BleRuntimeState.AwaitingAuthorization -> "awaiting authorization"
                is BleRuntimeState.Connecting -> "connecting"
                is BleRuntimeState.Negotiating -> "negotiating"
                is BleRuntimeState.Ready -> "ready"
                is BleRuntimeState.Reconnecting -> "reconnecting"
                is BleRuntimeState.Failed -> "failed"
                BleRuntimeState.Closed -> "closed"
                is BleRuntimeState.FactoryResetRequesting -> "board erase requesting"
                is BleRuntimeState.FactoryResetErasing -> "board erase in progress"
                is BleRuntimeState.FactoryResetVerifying -> "board erase verifying"
                is BleRuntimeState.FactoryResetNotVerified -> "board erase unverified"
                is BleRuntimeState.FactoryResetComplete -> "board erase complete"
            }
            put(MeshTrailLogKind.CONNECTION, connection)
            when (runtime) {
                is BleRuntimeState.Blocked -> put(MeshTrailLogKind.CONNECTION_BLOCK, runtime.reason.name)
                is BleRuntimeState.Negotiating -> put(MeshTrailLogKind.CONNECTION_PHASE, runtime.phase.name)
                is BleRuntimeState.Failed -> put(MeshTrailLogKind.CONNECTION_ERROR, runtime.reason.name)
                else -> Unit
            }
            val detail = when (runtime) {
                is BleRuntimeState.Reconnecting -> runtime.connectionDiagnostic
                is BleRuntimeState.Failed -> runtime.connectionDiagnostic
                else -> null
            }
            detail?.let { put(MeshTrailLogKind.CONNECTION_DETAIL, it.name) }
            val authorization = when (state.authorizationState) {
                DeviceAuthorizationUiState.None -> null
                is DeviceAuthorizationUiState.Starting -> "starting"
                is DeviceAuthorizationUiState.Pending -> "pending"
                is DeviceAuthorizationUiState.Accepted -> "accepted"
                is DeviceAuthorizationUiState.Denied -> "denied"
                is DeviceAuthorizationUiState.InvalidResult -> "invalid result"
                is DeviceAuthorizationUiState.Expired -> "expired"
                is DeviceAuthorizationUiState.Unavailable -> "unavailable"
                is DeviceAuthorizationUiState.Unsupported -> "unsupported"
                is DeviceAuthorizationUiState.AuthorityUnknown -> "unknown"
            }
            authorization?.let { put(MeshTrailLogKind.AUTHORIZATION, it) }
            val pairing = (runtime as? BleRuntimeState.Ready)?.session?.boardPairing
            when (pairing) {
                null -> Unit
                BoardPairingState.Unknown -> put(MeshTrailLogKind.PAIRING, "unknown")
                is BoardPairingState.Result -> {
                    put(MeshTrailLogKind.PAIRING, pairingResultLogValue(pairing.result.state, pairing.historical))
                    val reason = pairing.result.reason
                    if (reason != CompanionPairingReason.NONE) put(MeshTrailLogKind.PAIRING_RESULT_REASON, reason.name)
                }
                BoardPairingState.Available -> put(MeshTrailLogKind.PAIRING, "available")
                BoardPairingState.Waiting -> put(MeshTrailLogKind.PAIRING, "waiting")
                is BoardPairingState.Ready -> put(MeshTrailLogKind.PAIRING, "ready")
                is BoardPairingState.Rejected -> {
                    put(MeshTrailLogKind.PAIRING, "refused")
                    put(MeshTrailLogKind.PAIRING_REFUSED, pairing.reason.name)
                }
            }
        })
    }

    fun observeBluetoothRecovery(trigger: BluetoothRecoveryTrigger) =
        append(MeshTrailLogKind.BLUETOOTH_RECOVERY, trigger.name)

    fun observeBluetoothRetryRefusal(reason: BluetoothRecoveryRefusal) =
        append(MeshTrailLogKind.BLUETOOTH_RETRY_REFUSED, reason.name)

    private fun observe(next: Map<MeshTrailLogKind, String>) {
        next.forEach { (kind, value) ->
            if (last[kind] != value) append(kind, value)
        }
        last = next
    }
}
