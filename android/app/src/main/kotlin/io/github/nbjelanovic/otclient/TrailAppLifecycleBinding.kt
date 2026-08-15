package io.github.nbjelanovic.otclient

import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner

/** Activity lifecycle binding for Local/mode presentation and service observation only. */
class TrailAppLifecycleBinding(
    private val lifecycle: Lifecycle,
    private val controller: TrailLifecycleController,
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
