package io.github.nbjelanovic.otclient

import android.Manifest
import android.app.Activity
import android.app.Instrumentation
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.ParcelUuid
import android.os.SystemClock
import java.util.Collections
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** One-use OT-085B acceptance that passively awaits target-side link termination. */
class PublicLinkProbeInstrumentation : Instrumentation() {
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }

    override fun onStart() {
        val observation = Observation()
        try {
            require(Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            require(hasPermission(Manifest.permission.BLUETOOTH_SCAN))
            require(hasPermission(Manifest.permission.BLUETOOTH_CONNECT))
            val adapter = targetContext.getSystemService(BluetoothManager::class.java)?.adapter
                ?: error("adapter")
            require(adapter.isEnabled)
            val initial = scan(adapter.bluetoothLeScanner, INITIAL_SCAN_MILLIS)
            require(initial.size == 1)

            val callback = PublicGattCallback()
            val selectedDevice = initial.single()
            val gatt = try {
                selectedDevice.connectGatt(
                    targetContext,
                    false,
                    callback,
                    BluetoothDevice.TRANSPORT_LE,
                )
            } catch (_: SecurityException) {
                null
            } ?: error("connect")
            try {
                require(callback.readFinished.await(CONNECTION_AND_READ_MILLIS, TimeUnit.MILLISECONDS))
                require(callback.readAccepted)
                observation.publicRead = true
                require(
                    callback.disconnected.await(
                        PublicLinkAutomaticTerminationPolicy.WAIT_MILLIS,
                        TimeUnit.MILLISECONDS,
                    ),
                )
                require(callback.disconnectAccepted)
                observation.automaticTermination = true
            } finally {
                gatt.close()
            }

            Thread.sleep(READVERTISE_SETTLE_MILLIS)
            val postTermination = scan(adapter.bluetoothLeScanner, READVERTISE_SCAN_MILLIS)
            require(postTermination.size == 1)
            observation.compatibleAdvertiserReturned = true
        } catch (_: Exception) {
            // Intentionally emit only fixed booleans below: never exception text or endpoint data.
        }
        val outcome = observation.render()
        finish(
            if (observation.passed) Activity.RESULT_OK else Activity.RESULT_CANCELED,
            Bundle().apply { putString("stream", outcome) },
        )
    }

    private fun scan(scanner: android.bluetooth.le.BluetoothLeScanner, durationMillis: Long): Set<BluetoothDevice> {
        val results = Collections.synchronizedSet(LinkedHashSet<BluetoothDevice>())
        val active = AtomicBoolean(true)
        val failed = AtomicBoolean(false)
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                if (active.get()) results.add(result.device)
            }

            override fun onBatchScanResults(batchResults: MutableList<ScanResult>) {
                if (active.get()) batchResults.forEach { results.add(it.device) }
            }

            override fun onScanFailed(errorCode: Int) {
                failed.set(true)
            }
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, callback)
        try {
            Thread.sleep(durationMillis)
        } finally {
            active.set(false)
            scanner.stopScan(callback)
        }
        check(!failed.get())
        return synchronized(results) { results.toSet() }
    }

    private fun hasPermission(permission: String): Boolean =
        targetContext.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private class PublicGattCallback : BluetoothGattCallback() {
        private enum class Stage {
            CONNECTING,
            DISCOVERING,
            READING,
            HOLDING,
            DISCONNECTED,
            FAILED,
        }

        private val boundGatt = AtomicReference<BluetoothGatt?>()
        private val stage = AtomicReference(Stage.CONNECTING)
        val readFinished = CountDownLatch(1)
        val disconnected = CountDownLatch(1)
        @Volatile var readAccepted = false
            private set
        @Volatile var disconnectAccepted = false
            private set
        private val connectedAtElapsedMillis = AtomicLong(0L)

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (!acceptsGatt(gatt)) return fail()
            if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                if (status == BluetoothGatt.GATT_SUCCESS &&
                    stage.compareAndSet(Stage.HOLDING, Stage.DISCONNECTED)
                ) {
                    val connectedAt = connectedAtElapsedMillis.get()
                    val elapsedMillis = SystemClock.elapsedRealtime() - connectedAt
                    disconnectAccepted =
                        connectedAt > 0L &&
                        PublicLinkAutomaticTerminationPolicy.acceptsElapsed(elapsedMillis)
                    disconnected.countDown()
                } else {
                    fail()
                }
                return
            }
            if (status != BluetoothGatt.GATT_SUCCESS ||
                newState != BluetoothProfile.STATE_CONNECTED ||
                !stage.compareAndSet(Stage.CONNECTING, Stage.DISCOVERING)
            ) return fail()
            connectedAtElapsedMillis.set(SystemClock.elapsedRealtime())
            val discoveryStarted = try {
                gatt.discoverServices()
            } catch (_: SecurityException) {
                false
            }
            if (!discoveryStarted) fail()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (!acceptsGatt(gatt) || status != BluetoothGatt.GATT_SUCCESS ||
                !stage.compareAndSet(Stage.DISCOVERING, Stage.READING)
            ) return fail()
            val characteristic = try {
                gatt.getService(SERVICE_UUID)?.getCharacteristic(PUBLIC_LINK_INFO_UUID)
            } catch (_: SecurityException) {
                null
            }
            if (characteristic == null ||
                characteristic.properties != BluetoothGattCharacteristic.PROPERTY_READ
            ) return fail()
            val readStarted = try {
                gatt.readCharacteristic(characteristic)
            } catch (_: SecurityException) {
                false
            }
            if (!readStarted) fail()
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            if (!acceptsGatt(gatt) || characteristic.uuid != PUBLIC_LINK_INFO_UUID ||
                status != BluetoothGatt.GATT_SUCCESS ||
                !CompanionGattV0Contract.acceptsPublicLinkInfo(value) ||
                !stage.compareAndSet(Stage.READING, Stage.HOLDING)
            ) return fail()
            readAccepted = true
            readFinished.countDown()
        }

        @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
            onCharacteristicRead(gatt, characteristic, characteristic.value?.copyOf() ?: byteArrayOf(), status)
        }

        private fun acceptsGatt(gatt: BluetoothGatt): Boolean {
            val current = boundGatt.get()
            return if (current == null) boundGatt.compareAndSet(null, gatt) else current === gatt
        }

        private fun fail() {
            stage.set(Stage.FAILED)
            readFinished.countDown()
            disconnected.countDown()
        }
    }

    private data class Observation(
        var publicRead: Boolean = false,
        var automaticTermination: Boolean = false,
        var compatibleAdvertiserReturned: Boolean = false,
    ) {
        val passed: Boolean
            get() = publicRead && automaticTermination && compatibleAdvertiserReturned

        fun render(): String = listOf(
            "OT085B_PUBLIC_READ=${publicRead.status()}",
            "OT085B_AUTOMATIC_TERMINATION=${automaticTermination.status()}",
            "OT085B_COMPATIBLE_ADVERTISER_RETURNED=${compatibleAdvertiserReturned.status()}",
            "OT085B_PHONE_ACCEPTANCE=${passed.status()}",
        ).joinToString("\n")

        private fun Boolean.status(): String = if (this) "PASS" else "DENY"
    }

    private companion object {
        val SERVICE_UUID: UUID = UUID.fromString(CompanionGattV0Contract.SERVICE_UUID)
        val PUBLIC_LINK_INFO_UUID: UUID = UUID.fromString(CompanionGattV0Contract.PUBLIC_LINK_INFO_UUID)
        const val INITIAL_SCAN_MILLIS = 4_000L
        const val CONNECTION_AND_READ_MILLIS = 10_000L
        const val READVERTISE_SETTLE_MILLIS = 1_000L
        const val READVERTISE_SCAN_MILLIS = 6_000L
    }
}
