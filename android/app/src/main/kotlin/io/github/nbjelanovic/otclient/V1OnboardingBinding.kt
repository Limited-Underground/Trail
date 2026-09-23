package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.RegionSelectionCatalog

/** Ephemeral callback fence. Never saved, logged, or used as device authorization. */
internal class V1OnboardingScope private constructor(
    private val endpoint: String,
    private val nonce: Long,
    private val configurationOwner: Long,
) {
    fun matches(session: BleActiveSession): Boolean = endpoint == session.companion.endpointToken &&
        nonce == session.sessionNonce && configurationOwner == session.configuration.editorSessionId
    override fun toString() = "V1OnboardingScope(redacted)"
    companion object {
        fun from(session: BleActiveSession) = V1OnboardingScope(
            session.companion.endpointToken, session.sessionNonce, session.configuration.editorSessionId,
        )
    }
}

/** Resume from this protected session's readbacks, never a cached step or local draft. */
internal object V1OnboardingProjection {
    fun nameVerified(configuration: BleConfigurationState): Boolean = configuration.available &&
        configuration.nameRevision != null && configuration.nameRevision != 0uL &&
        configuration.deviceName?.let(V1DeviceName::create) != null

    fun stage(runtime: BleRuntimeState?): V1SetupStage = when (runtime) {
        is BleRuntimeState.Ready -> {
            val configuration = runtime.session.configuration
            when {
                !nameVerified(configuration) ->
                    V1SetupStage.NAME_DEVICE
                !configuration.regionAvailable || configuration.regionRevision == null ||
                    configuration.regionRevision == 0uL ||
                    configuration.regionSelectionId?.let(RegionSelectionCatalog::find) == null ->
                    V1SetupStage.CONFIRM_RADIO_REGION
                else -> V1SetupStage.PUBLIC_PROFILE
            }
        }
        is BleRuntimeState.Connecting, is BleRuntimeState.Negotiating,
        is BleRuntimeState.AwaitingAuthorization, is BleRuntimeState.Reconnecting,
        BleRuntimeState.FindingReturningOwner -> V1SetupStage.SECURE_PAIR_AND_AUTHORIZE
        else -> V1SetupStage.MATCH_DEVICE
    }

    // Saved region selection is not compatible RF configuration or TX authority.
    val radioTransmissionAllowed: Boolean get() = false
    // No protected public-name/visibility command exists in this device profile.
    val canComplete: Boolean get() = false
}

internal sealed interface V1OnboardingCommand {
    data object ReadName : V1OnboardingCommand
    data class WriteName(val value: String) : V1OnboardingCommand
    data object ReadRegion : V1OnboardingCommand
    data class WriteRegion(val selectionId: Int) : V1OnboardingCommand
}

/** Validate at click time; a stale composable cannot mutate a replacement session. */
internal class V1OnboardingBinding(
    private val runtime: () -> BleRuntimeState?,
    private val send: (V1OnboardingCommand) -> Boolean,
) {
    fun submit(scope: V1OnboardingScope, command: V1OnboardingCommand, onboarding: Boolean = true): Boolean {
        val ready = runtime() as? BleRuntimeState.Ready ?: return false
        val state = ready.session.configuration
        if (!scope.matches(ready.session) || !state.available || state.busy) return false
        val stage = V1OnboardingProjection.stage(ready)
        val permitted = when (command) {
            V1OnboardingCommand.ReadName -> !onboarding || stage == V1SetupStage.NAME_DEVICE
            is V1OnboardingCommand.WriteName -> (!onboarding || stage == V1SetupStage.NAME_DEVICE) &&
                state.nameRevision != null && state.nameRevision != ULong.MAX_VALUE &&
                V1DeviceName.create(command.value) != null
            V1OnboardingCommand.ReadRegion -> (!onboarding || stage == V1SetupStage.CONFIRM_RADIO_REGION) &&
                V1OnboardingProjection.nameVerified(state) && state.regionAvailable
            is V1OnboardingCommand.WriteRegion -> (!onboarding || stage == V1SetupStage.CONFIRM_RADIO_REGION) &&
                V1OnboardingProjection.nameVerified(state) &&
                state.regionAvailable && state.regionRevision != null && state.regionRevision != ULong.MAX_VALUE &&
                RegionSelectionCatalog.find(command.selectionId) != null
        }
        return permitted && send(command)
    }
}
