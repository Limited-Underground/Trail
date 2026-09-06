package io.github.nbjelanovic.otclient

/** Same coarse runtime categories as the original recorder; no device-health projection. */
internal object V1TestConnectionCategory {
    fun from(state: BleRuntimeState): V1TestPhoneConnectionState = when (state) {
        BleRuntimeState.Inactive,
        BleRuntimeState.Idle,
        is BleRuntimeState.ScanComplete,
        is BleRuntimeState.FactoryResetErasing,
        is BleRuntimeState.FactoryResetVerifying,
        is BleRuntimeState.FactoryResetComplete,
        BleRuntimeState.Closed,
        -> V1TestPhoneConnectionState.DISCONNECTED
        is BleRuntimeState.Blocked,
        is BleRuntimeState.FactoryResetNotVerified,
        is BleRuntimeState.Failed,
        -> V1TestPhoneConnectionState.FAILED
        is BleRuntimeState.Scanning,
        BleRuntimeState.FindingReturningOwner,
        is BleRuntimeState.Connecting,
        is BleRuntimeState.Negotiating,
        -> V1TestPhoneConnectionState.CONNECTING
        is BleRuntimeState.AwaitingAuthorization -> V1TestPhoneConnectionState.AUTHORIZING
        is BleRuntimeState.Reconnecting -> V1TestPhoneConnectionState.RECONNECTING
        is BleRuntimeState.Ready,
        is BleRuntimeState.FactoryResetRequesting,
        -> V1TestPhoneConnectionState.READY
    }
}
