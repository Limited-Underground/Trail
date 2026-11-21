package io.github.nbjelanovic.otclient

import androidx.compose.runtime.Composable

internal fun createActivityLocalController(): CompanionAppController? = null

// A controller without local authority cannot publish a local connection state.
@Composable
@Suppress("UNUSED_PARAMETER")
internal fun VariantLocalConnection(state: CompanionUiState, controller: TrailUiController) = Unit
