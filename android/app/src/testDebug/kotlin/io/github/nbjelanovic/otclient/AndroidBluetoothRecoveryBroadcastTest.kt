package io.github.nbjelanovic.otclient

import android.app.Application
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Looper
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AndroidBluetoothRecoveryBroadcastTest {
    @Test fun activeOwnerRecoversWhenAndroidReportsBluetoothOn() = withOwner { app, adapter, runtime ->
        val blocked = BleRuntimeState.Blocked(BleRuntimeBlock.BLUETOOTH_DISABLED)
        assertEquals(blocked, runtime.state)
        // A broadcast is a hint, so the platform's actual preflight still wins.
        sendState(app, BluetoothAdapter.STATE_ON)
        assertEquals(blocked, runtime.state)

        shadowOf(adapter).setState(BluetoothAdapter.STATE_ON)
        sendState(app, BluetoothAdapter.STATE_OFF)
        assertEquals(blocked, runtime.state)
        sendState(app, BluetoothAdapter.STATE_ON)
        assertEquals(BleRuntimeState.Idle, runtime.state)
    }

    @Test fun onlyTheActiveRuntimeOwnsTheReceiver() = withOwner { app, _, runtime ->
        val filter = Intent(BluetoothAdapter.ACTION_STATE_CHANGED)
        val shadow = shadowOf(app)
        assertTrue(shadow.hasReceiverForIntent(filter))
        runtime.onLifecycleStop()
        assertFalse(shadow.hasReceiverForIntent(filter))
        runtime.onLifecycleStart()
        assertTrue(shadow.hasReceiverForIntent(filter))
        runtime.close()
        assertFalse(shadow.hasReceiverForIntent(filter))
    }

    private fun withOwner(check: (Application, BluetoothAdapter, BleCompanionRuntime) -> Unit) {
        val app = RuntimeEnvironment.getApplication()
        shadowOf(app).grantPermissions(*AndroidBlePermissionContract.runtimePermissions.toTypedArray())
        shadowOf(app.packageManager).setSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE, true)
        val adapter = checkNotNull(app.getSystemService(BluetoothManager::class.java)?.adapter)
        shadowOf(adapter).setState(BluetoothAdapter.STATE_OFF)
        val platform = AndroidBluetoothGattFacade(app)
        // Exercise real Android preflight and broadcasts. There is no saved board or scanner lease.
        val facade = object : AndroidBluetoothFacade by platform {
            override fun createReturningOwnerScan(observer: (BleScanEvent) -> Unit): BleScanLease? = null
        }
        val runtime = BleCompanionRuntime(facade, AndroidMainThreadBleRuntimeScheduler())
        try {
            runtime.onLifecycleStart()
            check(app, adapter, runtime)
        } finally {
            runtime.close()
            platform.close()
        }
    }

    private fun sendState(app: Application, state: Int) {
        app.sendBroadcast(Intent(BluetoothAdapter.ACTION_STATE_CHANGED).putExtra(BluetoothAdapter.EXTRA_STATE, state))
        shadowOf(Looper.getMainLooper()).idle()
    }
}
