package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class AndroidBlePlatformPolicyTest {
    @Test
    fun preflightRequiresApiFeatureAdapterExplicitPermissionsAndEnabledRadioInOrder() {
        val ready = AndroidBlePlatformSnapshot(
            apiLevel = 35,
            hasBleFeature = true,
            hasBluetoothAdapter = true,
            scanPermissionGranted = true,
            connectPermissionGranted = true,
            bluetoothEnabled = true,
        )
        assertTrue(AndroidBlePreflightPolicy.evaluate(ready).isReady)
        assertEquals(
            BleRuntimeBlock.PLATFORM_UNSUPPORTED,
            AndroidBlePreflightPolicy.evaluate(ready.copy(apiLevel = 30)).blocker,
        )
        assertTrue(AndroidBlePreflightPolicy.evaluate(ready.copy(apiLevel = 31)).isReady)
        assertTrue(AndroidBlePreflightPolicy.evaluate(ready.copy(apiLevel = 32)).isReady)
        assertEquals(
            BleRuntimeBlock.BLUETOOTH_UNAVAILABLE,
            AndroidBlePreflightPolicy.evaluate(ready.copy(hasBleFeature = false)).blocker,
        )
        assertEquals(
            BleRuntimeBlock.BLUETOOTH_UNAVAILABLE,
            AndroidBlePreflightPolicy.evaluate(ready.copy(hasBluetoothAdapter = false)).blocker,
        )
        assertEquals(
            BleRuntimeBlock.SCAN_PERMISSION_MISSING,
            AndroidBlePreflightPolicy.evaluate(ready.copy(scanPermissionGranted = false)).blocker,
        )
        assertEquals(
            BleRuntimeBlock.CONNECT_PERMISSION_MISSING,
            AndroidBlePreflightPolicy.evaluate(ready.copy(connectPermissionGranted = false)).blocker,
        )
        assertEquals(
            BleRuntimeBlock.BLUETOOTH_DISABLED,
            AndroidBlePreflightPolicy.evaluate(ready.copy(bluetoothEnabled = false)).blocker,
        )
    }

    @Test
    fun concretePlatformPlanIsExactBoundedAndUsesOpaqueTokens() {
        assertEquals(CompanionGattV0Contract.SERVICE_UUID, AndroidBlePlatformPlan.SERVICE_UUID)
        assertFalse(AndroidBlePlatformPlan.AUTO_CONNECT)
        assertEquals(2, AndroidBlePlatformPlan.TRANSPORT_LE)
        assertEquals(2, AndroidBlePlatformPlan.WRITE_TYPE_WITH_RESPONSE)
        assertEquals(15_000L, AndroidBlePlatformPlan.SCAN_WINDOW_MILLIS)
        assertTrue(AndroidBlePlatformPlan.enableIndicationValue().contentEquals(byteArrayOf(0x02, 0x00)))
        assertTrue(OpaqueEndpointTokenPolicy.accepts("opaque", emptyList()))
        assertFalse(OpaqueEndpointTokenPolicy.accepts("", emptyList()))
        assertFalse(OpaqueEndpointTokenPolicy.accepts("x".repeat(MAX_ENDPOINT_TOKEN_CHARS + 1), emptyList()))
        assertFalse(OpaqueEndpointTokenPolicy.accepts("duplicate", listOf("duplicate")))
        val generated = ArrayDeque(listOf("", "duplicate", "x".repeat(MAX_ENDPOINT_TOKEN_CHARS + 1), "opaque-2"))
        assertEquals("opaque-2", OpaqueEndpointTokenPolicy.generate(listOf("duplicate")) { generated.removeFirst() })
        assertEquals(null, OpaqueEndpointTokenPolicy.generate(listOf("same")) { "same" })
    }

    @Test
    fun platformOperationsUseThePermissionThatAndroidAssignsToThem() {
        for (operation in listOf(AndroidBlePlatformOperation.START_SCAN, AndroidBlePlatformOperation.STOP_SCAN)) {
            assertTrue(AndroidBleOperationPermissionPolicy.allows(operation, true, false))
            assertFalse(AndroidBleOperationPermissionPolicy.allows(operation, false, true))
        }
        for (operation in AndroidBlePlatformOperation.entries - setOf(
            AndroidBlePlatformOperation.START_SCAN,
            AndroidBlePlatformOperation.STOP_SCAN,
        )) {
            assertFalse(AndroidBleOperationPermissionPolicy.allows(operation, true, false))
            assertTrue(AndroidBleOperationPermissionPolicy.allows(operation, false, true))
        }
        assertFalse(
            AndroidBleOperationPermissionPolicy.allows(
                AndroidBlePlatformOperation.DISCONNECT,
                scanPermissionGranted = true,
                connectPermissionGranted = false,
            ),
        )
    }

    @Test
    fun queuedScanCallbacksFailClosedAfterPermissionRevocation() {
        assertEquals(
            AndroidBleCallbackAdmission.IGNORE,
            AndroidBleScanCallbackPolicy.evaluate(active = false, scanPermissionGranted = true),
        )
        assertEquals(
            AndroidBleCallbackAdmission.PERMISSION_REVOKED,
            AndroidBleScanCallbackPolicy.evaluate(active = true, scanPermissionGranted = false),
        )
        assertEquals(
            AndroidBleCallbackAdmission.ACCEPT,
            AndroidBleScanCallbackPolicy.evaluate(active = true, scanPermissionGranted = true),
        )
    }

    @Test
    fun exactGattProfileRequiresReadWriteWithResponseIndicateAndCccd() {
        val accepted = AndroidGattProfileSnapshot(
            hasService = true,
            protocolInfoReadable = true,
            commandWriteWithResponse = true,
            commandWriteWithoutResponse = false,
            streamIndicate = true,
            streamHasClientConfigurationDescriptor = true,
        )
        assertTrue(AndroidGattProfilePolicy.accepts(accepted))
        assertFalse(AndroidGattProfilePolicy.accepts(accepted.copy(hasService = false)))
        assertFalse(AndroidGattProfilePolicy.accepts(accepted.copy(protocolInfoReadable = false)))
        assertFalse(AndroidGattProfilePolicy.accepts(accepted.copy(commandWriteWithResponse = false)))
        assertFalse(AndroidGattProfilePolicy.accepts(accepted.copy(commandWriteWithoutResponse = true)))
        assertFalse(AndroidGattProfilePolicy.accepts(accepted.copy(streamIndicate = false)))
        assertFalse(AndroidGattProfilePolicy.accepts(accepted.copy(streamHasClientConfigurationDescriptor = false)))
    }

    @Test
    fun gattOperationsAreSingleOutstandingOrderedAndTerminallyClosed() {
        val gate = AndroidGattOperationGate()
        assertFalse(gate.beginMtuRequest())
        assertTrue(gate.beginConnection())
        assertFalse(gate.beginConnection())
        assertTrue(gate.beginDiscovery())
        assertTrue(gate.acceptProfile())
        assertTrue(gate.beginMtuRequest())
        assertFalse(gate.beginProtocolInfoRead())
        assertTrue(gate.acceptMtu())
        assertTrue(gate.beginProtocolInfoRead())
        assertTrue(gate.acceptProtocolInfo())
        assertTrue(gate.beginIndicationSubscription())
        assertTrue(gate.acceptIndicationSubscription())
        assertTrue(gate.acceptsStreamIndication())
        assertTrue(gate.beginCommandWrite())
        assertTrue(gate.acceptsStreamIndication())
        assertFalse(gate.beginCommandWrite())
        assertTrue(gate.acceptCommandWrite())
        gate.close()
        assertFalse(gate.beginConnection())
        assertFalse(gate.acceptsStreamIndication())
    }

    @Test
    fun provisionalProtocolInfoMayPrecedeMtuButCccdStillRequiresBoth() {
        val gate = AndroidGattOperationGate()
        assertTrue(gate.beginConnection())
        assertTrue(gate.beginDiscovery())
        assertTrue(gate.acceptProfile())
        assertTrue(gate.beginProtocolInfoRead())
        assertTrue(gate.acceptProtocolInfo())
        assertFalse(gate.beginIndicationSubscription())
        assertTrue(gate.beginMtuRequest())
        assertTrue(gate.acceptMtu())
        assertTrue(gate.beginIndicationSubscription())
        assertTrue(gate.acceptIndicationSubscription())
        assertTrue(gate.beginCommandWrite())
        assertTrue(gate.acceptCommandWrite())
    }

    @Test
    fun protectedProtocolInfoNeedsCurrentPermissionBondExactObjectSuccessAndBoundedValue() {
        fun admission(
            active: Boolean = true,
            permission: Boolean = true,
            bonded: Boolean = true,
            exact: Boolean = true,
            status: Int = ANDROID_GATT_SUCCESS_STATUS,
            bytes: Int = 20,
        ) = AndroidProtectedProtocolInfoReadPolicy.evaluate(active, permission, bonded, exact, status, bytes)

        assertEquals(AndroidProtectedReadAdmission.ACCEPT, admission())
        assertEquals(AndroidProtectedReadAdmission.ACCEPT, admission(bytes = 16))
        assertEquals(AndroidProtectedReadAdmission.IGNORE, admission(active = false))
        assertEquals(AndroidProtectedReadAdmission.PERMISSION_REVOKED, admission(permission = false))
        assertEquals(AndroidProtectedReadAdmission.BOND_REQUIRED, admission(bonded = false))
        assertEquals(AndroidProtectedReadAdmission.PLATFORM_FAILURE, admission(exact = false))
        assertEquals(
            AndroidProtectedReadAdmission.SECURITY_REQUIRED,
            admission(status = ANDROID_GATT_INSUFFICIENT_AUTHENTICATION_STATUS),
        )
        assertEquals(
            AndroidProtectedReadAdmission.SECURITY_REQUIRED,
            admission(status = ANDROID_GATT_INSUFFICIENT_ENCRYPTION_STATUS),
        )
        assertEquals(
            AndroidProtectedReadAdmission.AUTHORIZATION_REJECTED,
            admission(status = ANDROID_GATT_INSUFFICIENT_AUTHORIZATION_STATUS),
        )
        assertEquals(AndroidProtectedReadAdmission.PLATFORM_FAILURE, admission(status = 257))
        assertEquals(AndroidProtectedReadAdmission.PLATFORM_FAILURE, admission(bytes = 15))
        assertEquals(AndroidProtectedReadAdmission.PLATFORM_FAILURE, admission(bytes = 21))

        val current = Any()
        val distinctSameUuidStandIn = Any()
        assertTrue(AndroidGattCharacteristicOwnershipPolicy.owns(current, current))
        assertFalse(AndroidGattCharacteristicOwnershipPolicy.owns(current, distinctSameUuidStandIn))
        assertFalse(AndroidGattCharacteristicOwnershipPolicy.owns(null, current))
        assertEquals(null, AndroidGattStatusPolicy.failure(ANDROID_GATT_SUCCESS_STATUS))
        assertEquals(
            BleGattFailure.SECURITY_REJECTED,
            AndroidGattStatusPolicy.failure(ANDROID_GATT_INSUFFICIENT_AUTHENTICATION_STATUS),
        )
        assertEquals(
            BleGattFailure.SECURITY_REJECTED,
            AndroidGattStatusPolicy.failure(ANDROID_GATT_INSUFFICIENT_ENCRYPTION_STATUS),
        )
        assertEquals(
            BleGattFailure.AUTHORIZATION_REJECTED,
            AndroidGattStatusPolicy.failure(ANDROID_GATT_INSUFFICIENT_AUTHORIZATION_STATUS),
        )
        assertEquals(BleGattFailure.PLATFORM_FAILURE, AndroidGattStatusPolicy.failure(257))
    }
}
