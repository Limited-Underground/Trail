package io.github.nbjelanovic.otclient

internal const val ANDROID_BLE_MINIMUM_API = 31

data object AndroidBlePlatformPlan {
    const val SERVICE_UUID = CompanionGattV0Contract.SERVICE_UUID
    const val AUTO_CONNECT = false
    const val TRANSPORT_LE = 2
    const val WRITE_TYPE_WITH_RESPONSE = 2
    const val SCAN_WINDOW_MILLIS = 15_000L
    fun enableIndicationValue(): ByteArray = byteArrayOf(0x02, 0x00)
}

object OpaqueEndpointTokenPolicy {
    private const val MAX_GENERATION_ATTEMPTS = 4

    fun accepts(token: String, existing: Collection<String>): Boolean =
        token.isNotBlank() && token.length <= MAX_ENDPOINT_TOKEN_CHARS && token !in existing

    fun generate(existing: Collection<String>, factory: () -> String): String? {
        repeat(MAX_GENERATION_ATTEMPTS) {
            val candidate = factory()
            if (accepts(candidate, existing)) return candidate
        }
        return null
    }
}

internal enum class AndroidBlePlatformOperation {
    START_SCAN,
    STOP_SCAN,
    CONNECT,
    DISCOVER_SERVICES,
    REQUEST_MTU,
    READ_PROTOCOL_INFO,
    ENABLE_STREAM_INDICATIONS,
    WRITE_COMMAND,
    DISCONNECT,
}

/** Permission ownership remains explicit so cleanup cannot accidentally depend on CONNECT. */
internal object AndroidBleOperationPermissionPolicy {
    fun allows(
        operation: AndroidBlePlatformOperation,
        scanPermissionGranted: Boolean,
        connectPermissionGranted: Boolean,
    ): Boolean = when (operation) {
        AndroidBlePlatformOperation.START_SCAN,
        AndroidBlePlatformOperation.STOP_SCAN,
        -> scanPermissionGranted

        AndroidBlePlatformOperation.CONNECT,
        AndroidBlePlatformOperation.DISCOVER_SERVICES,
        AndroidBlePlatformOperation.REQUEST_MTU,
        AndroidBlePlatformOperation.READ_PROTOCOL_INFO,
        AndroidBlePlatformOperation.ENABLE_STREAM_INDICATIONS,
        AndroidBlePlatformOperation.WRITE_COMMAND,
        AndroidBlePlatformOperation.DISCONNECT,
        -> connectPermissionGranted
    }
}

internal enum class AndroidBleCallbackAdmission {
    IGNORE,
    ACCEPT,
    PERMISSION_REVOKED,
}

internal object AndroidBleScanCallbackPolicy {
    fun evaluate(active: Boolean, scanPermissionGranted: Boolean): AndroidBleCallbackAdmission = when {
        !active -> AndroidBleCallbackAdmission.IGNORE
        !scanPermissionGranted -> AndroidBleCallbackAdmission.PERMISSION_REVOKED
        else -> AndroidBleCallbackAdmission.ACCEPT
    }
}

data class AndroidBlePlatformSnapshot(
    val apiLevel: Int,
    val hasBleFeature: Boolean,
    val hasBluetoothAdapter: Boolean,
    val scanPermissionGranted: Boolean,
    val connectPermissionGranted: Boolean,
    val bluetoothEnabled: Boolean,
)

object AndroidBlePreflightPolicy {
    fun evaluate(snapshot: AndroidBlePlatformSnapshot): BlePreflight = when {
        snapshot.apiLevel < ANDROID_BLE_MINIMUM_API -> BlePreflight(BleRuntimeBlock.PLATFORM_UNSUPPORTED)
        !snapshot.hasBleFeature || !snapshot.hasBluetoothAdapter -> BlePreflight(BleRuntimeBlock.BLUETOOTH_UNAVAILABLE)
        !snapshot.scanPermissionGranted -> BlePreflight(BleRuntimeBlock.SCAN_PERMISSION_MISSING)
        !snapshot.connectPermissionGranted -> BlePreflight(BleRuntimeBlock.CONNECT_PERMISSION_MISSING)
        !snapshot.bluetoothEnabled -> BlePreflight(BleRuntimeBlock.BLUETOOTH_DISABLED)
        else -> BlePreflight()
    }
}

data class AndroidGattProfileSnapshot(
    val hasService: Boolean,
    val protocolInfoReadable: Boolean,
    val commandWriteWithResponse: Boolean,
    val commandWriteWithoutResponse: Boolean,
    val streamIndicate: Boolean,
    val streamHasClientConfigurationDescriptor: Boolean,
)

object AndroidGattProfilePolicy {
    fun accepts(profile: AndroidGattProfileSnapshot): Boolean =
        profile.hasService &&
            profile.protocolInfoReadable &&
            profile.commandWriteWithResponse &&
            !profile.commandWriteWithoutResponse &&
            profile.streamIndicate &&
            profile.streamHasClientConfigurationDescriptor
}

internal enum class AndroidGattStage {
    NEW,
    CONNECTING,
    DISCOVERING,
    SECURED,
    MTU_PENDING,
    MTU_READY,
    PROTOCOL_INFO_PENDING,
    PROTOCOL_INFO_READY,
    INDICATION_SUBSCRIPTION_PENDING,
    READY,
    COMMAND_WRITE_PENDING,
    CLOSED,
}

/** Pure ordering guard around Android's one-outstanding-GATT-operation requirement. */
internal class AndroidGattOperationGate {
    var stage: AndroidGattStage = AndroidGattStage.NEW
        private set
    private var mtuReady = false
    private var protocolInfoReady = false

    fun beginConnection() = move(AndroidGattStage.NEW, AndroidGattStage.CONNECTING)
    fun beginDiscovery() = move(AndroidGattStage.CONNECTING, AndroidGattStage.DISCOVERING)
    fun acceptProfileAndSecurity() = move(AndroidGattStage.DISCOVERING, AndroidGattStage.SECURED)
    fun beginMtuRequest(): Boolean {
        if (stage != AndroidGattStage.SECURED && stage != AndroidGattStage.PROTOCOL_INFO_READY) return false
        stage = AndroidGattStage.MTU_PENDING
        return true
    }

    fun acceptMtu(): Boolean {
        if (stage != AndroidGattStage.MTU_PENDING) return false
        mtuReady = true
        stage = AndroidGattStage.MTU_READY
        return true
    }

    fun beginProtocolInfoRead(): Boolean {
        if (stage != AndroidGattStage.SECURED && stage != AndroidGattStage.MTU_READY) return false
        stage = AndroidGattStage.PROTOCOL_INFO_PENDING
        return true
    }

    fun acceptProtocolInfo(): Boolean {
        if (stage != AndroidGattStage.PROTOCOL_INFO_PENDING) return false
        protocolInfoReady = true
        stage = AndroidGattStage.PROTOCOL_INFO_READY
        return true
    }

    fun beginIndicationSubscription(): Boolean {
        if (!mtuReady || !protocolInfoReady) return false
        if (stage != AndroidGattStage.MTU_READY && stage != AndroidGattStage.PROTOCOL_INFO_READY) return false
        stage = AndroidGattStage.INDICATION_SUBSCRIPTION_PENDING
        return true
    }
    fun acceptIndicationSubscription() = move(AndroidGattStage.INDICATION_SUBSCRIPTION_PENDING, AndroidGattStage.READY)
    fun beginCommandWrite() = move(AndroidGattStage.READY, AndroidGattStage.COMMAND_WRITE_PENDING)
    fun acceptCommandWrite() = move(AndroidGattStage.COMMAND_WRITE_PENDING, AndroidGattStage.READY)

    fun acceptsStreamIndication(): Boolean =
        stage == AndroidGattStage.READY || stage == AndroidGattStage.COMMAND_WRITE_PENDING

    fun close() {
        stage = AndroidGattStage.CLOSED
    }

    private fun move(expected: AndroidGattStage, next: AndroidGattStage): Boolean {
        if (stage != expected) return false
        stage = next
        return true
    }
}
