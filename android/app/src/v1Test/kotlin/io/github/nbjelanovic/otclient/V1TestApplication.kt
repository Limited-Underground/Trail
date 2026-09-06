package io.github.nbjelanovic.otclient

import android.app.Application

/** Variant-only provider; production has no diagnostic recorder or storage dependency. */
class V1TestApplication : Application(), ConnectedDeviceSessionObservationProvider {
    override fun createObservation(generation: Long): ConnectedDeviceSessionLifecycleObserver {
        val recorder = V1TestLogRuntime.get(applicationContext)
        return V1TestServiceObservation(generation, recorder::trace, recorder::connection, recorder::traceReleased)
    }
}
