package io.github.nbjelanovic.otclient

private val TrailAppUiState.bluetoothRuntime: BleRuntimeState?
    get() = (this as? TrailAppUiState.BluetoothDevice)?.runtimeState

private fun BleRuntimeState?.awaitingSetupReadback(): Boolean =
    V1OnboardingProjection.stage(this) == V1SetupStage.SECURE_PAIR_AND_AUTHORIZE

/** ChooseMode does not tell us whether Android has an existing pairing. */
internal fun v1InitialHomeDestination(state: TrailAppUiState): V1AppDestination {
    if (state == TrailAppUiState.ChooseMode) return V1AppDestination.DEVICE
    val ready = state.bluetoothRuntime as? BleRuntimeState.Ready ?: return V1AppDestination.MESSAGES
    return if (ready.session.configuration.available && !V1OnboardingProjection.canComplete(ready)) {
        V1AppDestination.DEVICE
    } else V1AppDestination.MESSAGES
}

/** Only UI navigation markers are saveable; every handoff rechecks live protected readbacks. */
internal class V1HomeLaunchState private constructor(
    val destination: V1AppDestination,
    val setupArmed: Boolean,
    val completionHandled: Boolean,
    val awaitingSetupReadback: Boolean = false,
) {
    fun select(destination: V1AppDestination) = V1HomeLaunchState(destination,false,completionHandled)
    fun beginSetup(runtime: BleRuntimeState?) = V1HomeLaunchState(V1AppDestination.DEVICE,
        !completionHandled && !V1OnboardingProjection.canComplete(runtime),completionHandled)
    fun finish(scope: V1OnboardingScope, liveRuntime: () -> BleRuntimeState?): V1HomeLaunchState =
        finish(scope,liveRuntime())
    fun finish(scope: V1OnboardingScope, runtime: BleRuntimeState?): V1HomeLaunchState {
        val ready=runtime as? BleRuntimeState.Ready ?: return this
        if(destination!=V1AppDestination.DEVICE || !setupArmed || completionHandled ||
            !scope.matches(ready.session) || !V1OnboardingProjection.canComplete(runtime)) return this
        return V1HomeLaunchState(V1AppDestination.MESSAGES,false,true)
    }
    fun resolveInitialSetup(state: TrailAppUiState): V1HomeLaunchState {
        if (!awaitingSetupReadback || state.bluetoothRuntime.awaitingSetupReadback()) return this
        return initial(state)
    }

    fun save(): List<Any> = listOf(destination.name, setupArmed, completionHandled, awaitingSetupReadback)
    companion object {
        fun initial(state: TrailAppUiState): V1HomeLaunchState {
            val destination=v1InitialHomeDestination(state)
            return V1HomeLaunchState(
                destination,
                setupArmed = destination == V1AppDestination.DEVICE,
                completionHandled = false,
                awaitingSetupReadback = state.bluetoothRuntime.awaitingSetupReadback(),
            )
        }
        fun restore(value: Any): V1HomeLaunchState? {
            val fields=value as? List<*> ?: return null
            if (fields.size !in 3..4) return null
            val destination=V1AppDestination.entries.firstOrNull { it.name==fields[0] } ?: return null
            val armed=fields[1] as? Boolean ?: return null
            val handled=fields[2] as? Boolean ?: return null
            if(armed && (destination!=V1AppDestination.DEVICE || handled)) return null
            val awaitingReadback = if (fields.size == 4) fields[3] as? Boolean ?: return null else false
            if (awaitingReadback && (destination != V1AppDestination.MESSAGES || armed || handled)) return null
            return V1HomeLaunchState(destination, armed, handled, awaitingReadback)
        }
    }
}
