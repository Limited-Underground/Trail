package io.github.nbjelanovic.otclient

/** ChooseMode means connection setup has not started, not that Android has no saved bond. */
internal fun v1InitialHomeDestination(state: TrailAppUiState): V1AppDestination =
    if (state == TrailAppUiState.ChooseMode) V1AppDestination.DEVICE
    else V1AppDestination.MESSAGES
