package io.github.nbjelanovic.otclient

internal data class FactoryResetRecoveryPresentation(
    val detail: String,
    val bondGuidance: String?,
    val windowGuidance: String,
)

/** The reset receipt proves firmware cleanup, not Android system-pairing removal. */
internal fun factoryResetRecoveryPresentation(state: BleRuntimeState.FactoryResetComplete) =
    FactoryResetRecoveryPresentation(
        detail="Your device reset is confirmed.\nThe old connection is closed.",
        bondGuidance=if(state.systemBondRemovalRequired)
            "Android may still show the old pairing.\n" +
                "In Bluetooth settings, forget only the entry you're sure belongs to this device.\n" +
                "MeshTrail can't check whether Android removed it."
            else null,
        windowGuidance="Tap Start fresh device setup, then choose your device and enter its PIN.\n" +
            "The 60-second timer keeps running.\n" +
            "If the PIN is gone, restart your device when you're ready.",
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
