package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

@Composable
internal fun V1AppShell(
    state: V1AppNavigationState,
    onDestinationSelected: (V1AppDestination) -> Unit,
    messages: @Composable () -> Unit,
    group: @Composable () -> Unit,
    device: @Composable () -> Unit,
) {
    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
        val content: @Composable () -> Unit = when (state.selectedDestination) {
            V1AppDestination.MESSAGES -> messages
            V1AppDestination.GROUP -> group
            V1AppDestination.DEVICE -> device
        }
        if (maxWidth > maxHeight) {
            Row(modifier = Modifier.fillMaxSize()) {
                V1NavigationRail(state.selectedDestination, onDestinationSelected)
                Box(modifier = Modifier.weight(1f).fillMaxSize()) { content() }
            }
        } else {
            Column(modifier = Modifier.fillMaxSize()) {
                Box(modifier = Modifier.weight(1f).fillMaxSize()) { content() }
                V1NavigationBar(state.selectedDestination, onDestinationSelected)
            }
        }
    }
}

@Composable
private fun V1NavigationBar(
    selected: V1AppDestination,
    onDestinationSelected: (V1AppDestination) -> Unit,
) {
    NavigationBar {
        V1AppDestination.entries.forEach { destination ->
            NavigationBarItem(
                selected = destination == selected,
                onClick = { onDestinationSelected(destination) },
                icon = { Text(destination.shortLabel) },
                label = { Text(destination.publicLabel) },
            )
        }
    }
}

@Composable
private fun V1NavigationRail(
    selected: V1AppDestination,
    onDestinationSelected: (V1AppDestination) -> Unit,
) {
    NavigationRail {
        V1AppDestination.entries.forEach { destination ->
            NavigationRailItem(
                selected = destination == selected,
                onClick = { onDestinationSelected(destination) },
                icon = { Text(destination.shortLabel) },
                label = { Text(destination.publicLabel) },
            )
        }
    }
}

private val V1AppDestination.publicLabel: String
    get() = when (this) {
        V1AppDestination.MESSAGES -> "Messages"
        V1AppDestination.GROUP -> "Group"
        V1AppDestination.DEVICE -> "Device"
    }

private val V1AppDestination.shortLabel: String
    get() = when (this) {
        V1AppDestination.MESSAGES -> "M"
        V1AppDestination.GROUP -> "G"
        V1AppDestination.DEVICE -> "D"
    }
