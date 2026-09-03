package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.COMPANION_AUTHORIZATION_PROTOCOL_INFO_BYTES
import io.github.nbjelanovic.otprotocol.COMPANION_PROTOCOL_INFO_BYTES
import java.util.UUID

internal const val ANDROID_BLE_MINIMUM_API = 31

data object AndroidBlePlatformPlan {
    const val GATT_SERVICE_UUID = CompanionGattV0Contract.SERVICE_UUID
    const val RETURNING_OWNER_ADVERTISING_UUID = CompanionGattV0Contract.SERVICE_UUID
    const val PAIRABLE_ADVERTISING_UUID = CompanionGattV0Contract.PAIRABLE_ADVERTISING_UUID
    const val AUTO_CONNECT = false
    const val TRANSPORT_LE = 2
    const val WRITE_TYPE_WITH_RESPONSE = 2
    const val SCAN_WINDOW_MILLIS = 30_000L
    fun enableIndicationValue(): ByteArray = byteArrayOf(0x02, 0x00)
}

/** Add Device admits only the advertising-only D1 marker, never the owned D0 GATT UUID by itself. */
internal object AndroidPairableAdvertisementPolicy {
    private val gattServiceUuid = UUID.fromString(AndroidBlePlatformPlan.GATT_SERVICE_UUID)
    private val pairableAdvertisingUuid = UUID.fromString(AndroidBlePlatformPlan.PAIRABLE_ADVERTISING_UUID)

    fun accepts(advertisedServiceUuids: Collection<UUID>): Boolean =
        advertisedServiceUuids.any { it == pairableAdvertisingUuid } &&
            advertisedServiceUuids.none { it == gattServiceUuid }
}

/**
 * Returning-owner discovery is intentionally disjoint from Add Device. It accepts only the owned
 * D0 advertisement from a device that is already in Android's bonded-device inventory, and rejects
 * any result that also exposes D1 because D1 is reserved for the unowned authorization window.
 */
internal object AndroidReturningOwnerAdvertisementPolicy {
    private val returningOwnerAdvertisingUuid = UUID.fromString(AndroidBlePlatformPlan.RETURNING_OWNER_ADVERTISING_UUID)
    private val pairableAdvertisingUuid = UUID.fromString(AndroidBlePlatformPlan.PAIRABLE_ADVERTISING_UUID)

    fun accepts(advertisedServiceUuids: Collection<UUID>, bonded: Boolean): Boolean =
        bonded &&
            advertisedServiceUuids.any { it == returningOwnerAdvertisingUuid } &&
            advertisedServiceUuids.none { it == pairableAdvertisingUuid }
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
    PROFILE_READY,
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
    fun acceptProfile() = move(AndroidGattStage.DISCOVERING, AndroidGattStage.PROFILE_READY)
    fun beginMtuRequest(): Boolean {
        if (stage != AndroidGattStage.PROFILE_READY && stage != AndroidGattStage.PROTOCOL_INFO_READY) return false
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
        if (stage != AndroidGattStage.PROFILE_READY && stage != AndroidGattStage.MTU_READY) return false
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

internal const val ANDROID_GATT_SUCCESS_STATUS = 0
internal const val ANDROID_GATT_INSUFFICIENT_AUTHENTICATION_STATUS = 5
internal const val ANDROID_GATT_INSUFFICIENT_AUTHORIZATION_STATUS = 8
internal const val ANDROID_GATT_INSUFFICIENT_ENCRYPTION_STATUS = 15

internal enum class AndroidProtectedReadAdmission {
    IGNORE,
    ACCEPT,
    PERMISSION_REVOKED,
    BOND_REQUIRED,
    SECURITY_REQUIRED,
    AUTHORIZATION_REJECTED,
    PLATFORM_FAILURE,
}

/**
 * Pure callback admission for the device-protected ProtocolInfo read. A bonded OS record is only a
 * prerequisite. ACCEPT means the exact characteristic returned one bounded supported-length value;
 * the device-side ATT access checks, not Android bond state, establish protected-path evidence.
 */
internal object AndroidProtectedProtocolInfoReadPolicy {
    fun evaluate(
        active: Boolean,
        connectPermissionGranted: Boolean,
        bondedPrerequisite: Boolean,
        exactCharacteristic: Boolean,
        status: Int,
        valueBytes: Int,
    ): AndroidProtectedReadAdmission = when {
        !active -> AndroidProtectedReadAdmission.IGNORE
        !connectPermissionGranted -> AndroidProtectedReadAdmission.PERMISSION_REVOKED
        !bondedPrerequisite -> AndroidProtectedReadAdmission.BOND_REQUIRED
        !exactCharacteristic -> AndroidProtectedReadAdmission.PLATFORM_FAILURE
        status == ANDROID_GATT_INSUFFICIENT_AUTHENTICATION_STATUS ||
            status == ANDROID_GATT_INSUFFICIENT_ENCRYPTION_STATUS -> AndroidProtectedReadAdmission.SECURITY_REQUIRED
        status == ANDROID_GATT_INSUFFICIENT_AUTHORIZATION_STATUS ->
            AndroidProtectedReadAdmission.AUTHORIZATION_REJECTED
        status != ANDROID_GATT_SUCCESS_STATUS -> AndroidProtectedReadAdmission.PLATFORM_FAILURE
        valueBytes != COMPANION_PROTOCOL_INFO_BYTES &&
            valueBytes != COMPANION_AUTHORIZATION_PROTOCOL_INFO_BYTES -> AndroidProtectedReadAdmission.PLATFORM_FAILURE
        else -> AndroidProtectedReadAdmission.ACCEPT
    }
}

internal object AndroidGattCharacteristicOwnershipPolicy {
    fun owns(expected: Any?, callbackValue: Any?): Boolean = expected != null && expected === callbackValue
}

internal object AndroidGattStatusPolicy {
    fun failure(status: Int): BleGattFailure? = when (status) {
        ANDROID_GATT_SUCCESS_STATUS -> null
        ANDROID_GATT_INSUFFICIENT_AUTHENTICATION_STATUS,
        ANDROID_GATT_INSUFFICIENT_ENCRYPTION_STATUS,
        -> BleGattFailure.SECURITY_REJECTED
        ANDROID_GATT_INSUFFICIENT_AUTHORIZATION_STATUS -> BleGattFailure.AUTHORIZATION_REJECTED
        else -> BleGattFailure.PLATFORM_FAILURE
    }
}
