package io.github.nbjelanovic.otclient

internal data class FactoryResetRecoveryPresentation(
    val detail: String,
    val bondGuidance: String?,
    val windowGuidance: String,
)

/** The reset receipt proves firmware cleanup, not Android system-pairing removal. */
internal fun factoryResetRecoveryPresentation(state: BleRuntimeState.FactoryResetComplete) =
    FactoryResetRecoveryPresentation(
        detail="The device reset is confirmed. Its old app connection is closed.",
        bondGuidance=if(state.systemBondRemovalRequired)
            "Android may still have an old pairing. In Bluetooth settings, forget only the entry you know belongs " +
                "to this device. Leave other entries alone. If you are unsure, do not remove anything. " +
                "Trail cannot check whether Android removed it."
            else null,
        windowGuidance="Start fresh setup, then choose your device and enter its PIN. The 60-second window keeps " +
            "running. If the PIN is gone, restart the reset device when you are ready.",
    )

/** Returns dispatch only; never a pairing, cleanup or Ready receipt. */
internal fun requestFreshSetupAfterReset(
    expected: BleRuntimeState.FactoryResetComplete,
    liveRuntime: () -> BleRuntimeState?,
    scan: () -> Unit,
): Boolean {
    if(liveRuntime() !== expected) return false
    scan()
    return true
}
