package io.github.nbjelanovic.otclient

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner

/** Releases scan, GATT, and reconnect leases when the bound Android lifecycle stops. */
class BleCompanionLifecycleBinding(
    private val lifecycle: Lifecycle,
    private val runtime: BleCompanionRuntime,
) : DefaultLifecycleObserver, AutoCloseable {
    private var closed = false

    init {
        lifecycle.addObserver(this)
    }

    override fun onStart(owner: LifecycleOwner) = runtime.onLifecycleStart()

    override fun onStop(owner: LifecycleOwner) = runtime.onLifecycleStop()

    override fun onDestroy(owner: LifecycleOwner) = close()

    override fun close() {
        if (closed) return
        closed = true
        lifecycle.removeObserver(this)
        runtime.close()
    }
}
