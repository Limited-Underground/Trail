package io.github.nbjelanovic.otclient

/** ChooseMode means connection setup has not started, not that Android has no saved bond. */
internal fun v1InitialHomeDestination(state: TrailAppUiState): V1AppDestination =
    if (state == TrailAppUiState.ChooseMode) V1AppDestination.DEVICE
    else V1AppDestination.MESSAGES

/** Only UI navigation markers are saveable; every handoff rechecks live protected readbacks. */
internal class V1HomeLaunchState private constructor(
    val destination: V1AppDestination,
    val setupArmed: Boolean,
    val completionHandled: Boolean,
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
    fun save(): List<Any> = listOf(destination.name,setupArmed,completionHandled)
    companion object {
        fun initial(state: TrailAppUiState): V1HomeLaunchState {
            val destination=v1InitialHomeDestination(state)
            return V1HomeLaunchState(destination,destination==V1AppDestination.DEVICE,false)
        }
        fun restore(value: Any): V1HomeLaunchState? {
            val fields=value as? List<*> ?: return null
            if(fields.size!=3) return null
            val destination=V1AppDestination.entries.firstOrNull { it.name==fields[0] } ?: return null
            val armed=fields[1] as? Boolean ?: return null
            val handled=fields[2] as? Boolean ?: return null
            if(armed && (destination!=V1AppDestination.DEVICE || handled)) return null
            return V1HomeLaunchState(destination,armed,handled)
        }
    }
}
