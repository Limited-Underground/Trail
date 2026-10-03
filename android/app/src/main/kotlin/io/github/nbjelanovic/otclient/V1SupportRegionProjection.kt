package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.RegionSelectionCatalog

/** Projects only this Ready session's verified setting; it neither reads nor retains authority. */
internal object V1SupportRegionProjector {
    fun from(state: TrailAppUiState): V1RadioRegion? {
        val runtime = (state as? TrailAppUiState.BluetoothDevice)?.runtimeState
        val configuration = (runtime as? BleRuntimeState.Ready)?.session?.configuration ?: return null
        if (!configuration.available || !configuration.regionAvailable ||
            configuration.regionRevision?.let { it > 0uL } != true
        ) return null
        val selection = configuration.regionSelectionId?.let(RegionSelectionCatalog::find) ?: return null
        // Other catalog settings remain unavailable until the support representation is extended.
        return V1RadioRegion.US915.takeIf { selection.id == 1 && selection.code == it.publicCode }
    }
}
