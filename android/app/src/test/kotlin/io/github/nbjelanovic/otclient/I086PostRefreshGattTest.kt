package io.github.nbjelanovic.otclient

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanRecord
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.pm.PackageManager
import android.os.Looper
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionAuthorizationProtocolInfoCodec
import io.github.nbjelanovic.otprotocol.CompanionFrameKind
import io.github.nbjelanovic.otprotocol.CompanionProtocolCodec
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.time.Duration
import java.util.UUID
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements
import org.robolectric.annotation.LooperMode
import org.robolectric.shadow.api.Shadow
import org.robolectric.shadows.ShadowBluetoothGatt
import org.robolectric.shadows.ShadowLooper
import org.robolectric.util.ReflectionHelpers
import org.robolectric.util.ReflectionHelpers.ClassParameter

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], shadows = [I086QueuedGattShadow::class])
@LooperMode(LooperMode.Mode.PAUSED)
class I086PostRefreshGattTest {
    @Test
    fun returningOwnerRetriesOnePostRefreshTransportFailureAndReachesClaimOnFreshLease() = withOwner { owner ->
        val successor = successorAfterRefresh(owner)
        completeRead(successor, 0x81)
        val retry = assertIs<BleRuntimeState.Reconnecting>(owner.runtime.state)
        assertEquals(2, retry.attempt)
        assertNull(successor.shadow.latestWrittenBytes)

        owner.main.idleFor(Duration.ofSeconds(2))
        val fresh = attach(owner.device, owner.serviceUuid, expectedConnections = 3)
        val nonce = 73L
        completeRead(fresh, BluetoothGatt.GATT_SUCCESS, protocolInfo(nonce))
        assertEquals(BleNegotiationPhase.STREAM_SUBSCRIPTION,
            assertIs<BleRuntimeState.Negotiating>(owner.runtime.state).phase)
        assertNull(fresh.shadow.latestWrittenBytes)
        assertNotNull(fresh.shadow.gattCallback).onDescriptorWrite(fresh.gatt, fresh.cccd, BluetoothGatt.GATT_SUCCESS)
        assertEquals(BleNegotiationPhase.AUTHORIZATION_CLAIM,
            assertIs<BleRuntimeState.Negotiating>(owner.runtime.state).phase)
        val claim = assertNotNull(CompanionProtocolCodec.decodeFragment(
            assertNotNull(fresh.shadow.latestWrittenBytes),
        ).value)
        assertEquals(CompanionFrameKind.AUTHORIZATION_CLAIM_START, claim.kind)
        assertEquals(nonce, claim.sessionNonce)
    }

    @Test
    fun aSecondPlatformReadFailureDoesNotStartAnotherRetry() = withOwner { owner ->
        completeRead(successorAfterRefresh(owner), 0x85)
        assertEquals(2, assertIs<BleRuntimeState.Reconnecting>(owner.runtime.state).attempt)
        owner.main.idleFor(Duration.ofSeconds(2))
        val fresh = attach(owner.device, owner.serviceUuid, expectedConnections = 3)
        completeRead(fresh, 0x81)
        assertTerminal(owner, BleConnectionDiagnostic.GATT_PLATFORM_FAILURE, expectedConnections = 3)
        assertNull(fresh.shadow.latestWrittenBytes)
    }

    @Test
    fun anAuthenticationFailureAfterRefreshDoesNotRetry() = withOwner { owner ->
        val successor = successorAfterRefresh(owner)
        completeRead(successor, ANDROID_GATT_INSUFFICIENT_AUTHENTICATION_STATUS)
        assertTerminal(owner, BleConnectionDiagnostic.GATT_SECURITY_REJECTED, expectedConnections = 2)
        assertNull(successor.shadow.latestWrittenBytes)
    }

    @Test
    fun aDifferentCharacteristicWithTheSameUuidDoesNotQualifyForRetry() = withOwner { owner ->
        val successor = successorAfterRefresh(owner)
        val foreign = characteristic(CompanionGattV0Contract.PROTOCOL_INFO_UUID, BluetoothGattCharacteristic.PROPERTY_READ)
        assertNotNull(successor.shadow.gattCallback).onCharacteristicRead(
            successor.gatt, foreign, byteArrayOf(), 0x81,
        )
        assertTerminal(owner, BleConnectionDiagnostic.GATT_PLATFORM_FAILURE, expectedConnections = 2)
        assertNull(successor.shadow.latestWrittenBytes)
    }

    private fun successorAfterRefresh(owner: Owner): Link {
        val original = attach(owner.device, owner.serviceUuid, expectedConnections = 1)
        assertInProtocolRead(owner)
        assertNotNull(original.shadow.gattCallback).onServiceChanged(original.gatt)
        // Drain the outstanding read from the invalidated profile. Its valid-looking
        // bytes cannot advance the handshake on a lease that only refreshes the cache.
        completeRead(original, BluetoothGatt.GATT_SUCCESS, protocolInfo(42L))
        owner.main.idle()
        assertEquals(BleConnectionDiagnostic.GATT_TRANSIENT_LINK,
            assertIs<BleRuntimeState.Reconnecting>(owner.runtime.state).connectionDiagnostic)
        assertNull(original.shadow.latestWrittenBytes)

        owner.main.idleFor(Duration.ofSeconds(1))
        val successor = attach(owner.device, owner.serviceUuid, expectedConnections = 2)
        assertInProtocolRead(owner)
        // A callback from the retired GATT cannot fail the successor or spend its retry.
        completeRead(original, 0x81)
        assertInProtocolRead(owner)
        return successor
    }

    private fun assertInProtocolRead(owner: Owner) {
        assertEquals(BleNegotiationPhase.PROTOCOL_INFO, assertIs<BleRuntimeState.Negotiating>(owner.runtime.state).phase)
    }

    private fun completeRead(link: Link, status: Int, value: ByteArray = byteArrayOf()) {
        assertNotNull(link.shadow.gattCallback).onCharacteristicRead(link.gatt, link.info, value, status)
    }

    private fun assertTerminal(owner: Owner, diagnostic: BleConnectionDiagnostic, expectedConnections: Int) {
        assertEquals(diagnostic, assertIs<BleRuntimeState.Failed>(owner.runtime.state).connectionDiagnostic)
        owner.main.idleFor(Duration.ofSeconds(30))
        assertEquals(expectedConnections, shadowOf(owner.device).bluetoothGatts.size)
    }

    @Suppress("DEPRECATION") // This deprecated constructor is limited to the synthetic scan result.
    private fun withOwner(check: (Owner) -> Unit) {
        val app = RuntimeEnvironment.getApplication()
        shadowOf(app).grantPermissions(*AndroidBlePermissionContract.runtimePermissions.toTypedArray())
        shadowOf(app.packageManager).setSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE, true)
        val adapter = assertNotNull(app.getSystemService(BluetoothManager::class.java)?.adapter)
        shadowOf(adapter).setState(BluetoothAdapter.STATE_ON)
        val address = (0..5).joinToString(":") { "%02X".format(it + 32) }
        val device = adapter.getRemoteDevice(address)
        shadowOf(device).setBondState(BluetoothDevice.BOND_BONDED)
        shadowOf(adapter).setBondedDevices(setOf(device))

        val platform = AndroidBluetoothGattFacade(app)
        val runtime = BleCompanionRuntime(platform, AndroidMainThreadBleRuntimeScheduler())
        val main = shadowOf(Looper.getMainLooper())
        try {
            runtime.onLifecycleStart()
            val scanner = assertNotNull(adapter.bluetoothLeScanner)
            val serviceUuid = UUID.fromString(AndroidBlePlatformPlan.GATT_SERVICE_UUID)
            val record = returningOwnerAdvertisement(serviceUuid)
            shadowOf(scanner).scanCallbacks.single().onScanResult(
                ScanSettings.CALLBACK_TYPE_ALL_MATCHES, ScanResult(device, record, -42, 1L),
            )
            main.idleFor(Duration.ofMillis(ANDROID_BLE_SCAN_WINDOW_MILLIS))
            check(Owner(device, serviceUuid, runtime, main))
        } finally {
            runtime.close()
            platform.close()
        }
    }

    private data class Owner(
        val device: BluetoothDevice,
        val serviceUuid: UUID,
        val runtime: BleCompanionRuntime,
        val main: ShadowLooper,
    )

    private fun attach(device: BluetoothDevice, serviceUuid: UUID, expectedConnections: Int): Link {
        val gatts = shadowOf(device).bluetoothGatts
        assertEquals(expectedConnections, gatts.size)
        val gatt = gatts.last()
        val shadow = Shadow.extract<I086QueuedGattShadow>(gatt)
        val service = BluetoothGattService(serviceUuid, BluetoothGattService.SERVICE_TYPE_PRIMARY)
        val info = characteristic(CompanionGattV0Contract.PROTOCOL_INFO_UUID, BluetoothGattCharacteristic.PROPERTY_READ)
        service.addCharacteristic(info)
        service.addCharacteristic(characteristic(CompanionGattV0Contract.COMMAND_UUID,
            BluetoothGattCharacteristic.PROPERTY_WRITE))
        val stream = characteristic(CompanionGattV0Contract.STREAM_UUID, BluetoothGattCharacteristic.PROPERTY_INDICATE)
        val cccd = BluetoothGattDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"),
            BluetoothGattDescriptor.PERMISSION_WRITE)
        stream.addDescriptor(cccd)
        service.addCharacteristic(stream)
        shadow.addDiscoverableService(service)
        shadow.allowCharacteristicNotification(stream)
        assertNotNull(shadow.gattCallback).onConnectionStateChange(
            gatt, BluetoothGatt.GATT_SUCCESS, BluetoothProfile.STATE_CONNECTED,
        )
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals(info, shadow.pendingRead)
        return Link(gatt, shadow, info, cccd)
    }

    private fun characteristic(uuid: String, properties: Int) =
        BluetoothGattCharacteristic(UUID.fromString(uuid), properties, BluetoothGattCharacteristic.PERMISSION_READ)

    private fun returningOwnerAdvertisement(uuid: UUID): ScanRecord {
        val data = ByteBuffer.allocate(18).order(ByteOrder.LITTLE_ENDIAN)
            .put(17).put(7).putLong(uuid.leastSignificantBits).putLong(uuid.mostSignificantBits).array()
        return assertNotNull(ReflectionHelpers.callStaticMethod(
            ScanRecord::class.java, "parseFromBytes", ClassParameter.from(ByteArray::class.java, data),
        ))
    }

    private fun protocolInfo(nonce: Long): ByteArray = assertNotNull(
        CompanionAuthorizationProtocolInfoCodec.encode(
            CompanionAuthorizationProtocolInfo(provisionalSessionNonce = nonce),
        ).value,
    )

    private data class Link(
        val gatt: BluetoothGatt,
        val shadow: I086QueuedGattShadow,
        val info: BluetoothGattCharacteristic,
        val cccd: BluetoothGattDescriptor,
    )
}

/** Accept platform requests without inventing completion; the case delivers the exact callbacks. */
@Implements(BluetoothGatt::class)
class I086QueuedGattShadow : ShadowBluetoothGatt() {
    var pendingRead: BluetoothGattCharacteristic? = null
        private set

    @Implementation
    fun readCharacteristic(characteristic: BluetoothGattCharacteristic): Boolean {
        pendingRead = characteristic
        return true
    }

    @Implementation
    fun writeDescriptor(descriptor: BluetoothGattDescriptor, value: ByteArray): Int =
        BluetoothStatusCodes.SUCCESS
}
