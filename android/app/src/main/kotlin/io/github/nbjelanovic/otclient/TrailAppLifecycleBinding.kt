package io.github.nbjelanovic.otclient

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner

/** Sole Android lifecycle owner for the mode controller and its nested BLE runtime. */
class TrailAppLifecycleBinding(
    private val lifecycle: Lifecycle,
    private val controller: TrailAppController,
) : DefaultLifecycleObserver, AutoCloseable {
    private var closed = false

    init {
        lifecycle.addObserver(this)
    }

    override fun onStart(owner: LifecycleOwner) = controller.onLifecycleStart()

    override fun onStop(owner: LifecycleOwner) = controller.onLifecycleStop()

    override fun onDestroy(owner: LifecycleOwner) = close()

    override fun close() {
        if (closed) return
        closed = true
        lifecycle.removeObserver(this)
        controller.close()
    }
}
