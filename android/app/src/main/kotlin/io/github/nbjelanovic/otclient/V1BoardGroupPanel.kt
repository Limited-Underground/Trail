package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

@Composable
internal fun V1BoardGroupPanel(
    state: V1BoardGroupState,
    connected: Boolean,
    onAction: (V1BoardGroupAction) -> Boolean,
    confirmationContent: @Composable () -> Unit = {},
) {
    val canRead = connected && !state.busy
    val currentRoster = connected && state.supported && state.available
    val canChange = currentRoster && !state.busy
    val groups = if (currentRoster) state.groups else emptyList()
    var rule by remember { mutableStateOf(V1BoardGroupLocation.OFF) }
    // A confirmation never moves to a different attachment or roster snapshot.
    var stopping by remember(state, connected) { mutableStateOf<V1BoardGroupSummary?>(null) }
    var actionDidNotStart by remember(state, connected) { mutableStateOf(false) }

    fun send(action: V1BoardGroupAction) {
        actionDidNotStart = !onAction(action)
    }

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Groups", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.semantics { heading() })
        val notice = state.notice ?: when {
            !connected -> "Connect to your device to see groups."
            state.busy -> "Waiting for your device."
            !currentRoster -> "Refresh groups to check your device and read its rosters."
            groups.isEmpty() -> "No groups on this device."
            else -> null
        }
        notice?.let { Text(it) }
        if (actionDidNotStart) Text("The group request could not start. Try refreshing again.")
        OutlinedButton(onClick = { send(V1BoardGroupAction.Refresh) }, enabled = canRead) {
            Text("Refresh groups")
        }
        confirmationContent()
        groups.forEach { group ->
            V1BoardGroupRoster(group, canChange) { stopping = group }
        }
        if (currentRoster && state.canCreate) {
            Text("Create a group", style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
            Text("Location sharing")
            Column(Modifier.selectableGroup()) {
                V1BoardGroupLocation.entries.forEach { option ->
                    Row(
                        Modifier.fillMaxWidth().selectable(
                            selected = rule == option,
                            enabled = canChange,
                            role = Role.RadioButton,
                            onClick = { rule = option },
                        ).padding(vertical = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = rule == option, onClick = null, enabled = canChange)
                        Text(option.label)
                    }
                }
            }
            Button(enabled = canChange, onClick = { send(V1BoardGroupAction.Create(rule.wireValue)) }) {
                Text("Create group")
            }
        }
        GroupScreenVariantContent()
    }

    stopping?.takeIf { selected -> canChange && !selected.distrusted && groups.any { it === selected } }?.let { group ->
        val name = group.displayName()
        AlertDialog(
            onDismissRequest = { stopping = null },
            title = { Text("Stop trusting $name?") },
            text = { Text("This device removes the group's pairings and rejects future roster updates for the group.") },
            confirmButton = {
                TextButton(onClick = {
                    stopping = null
                    send(V1BoardGroupAction.StopTrust(group))
                }) { Text("Stop trusting") }
            },
            dismissButton = { TextButton(onClick = { stopping = null }) { Text("Cancel") } },
        )
    }
}

private enum class V1BoardGroupLocation(val wireValue: Int, val label: String) {
    OFF(0, "Off"),
    PREFERRED(1, "Preferred"),
    REQUIRED(2, "Required"),
}

private fun V1BoardGroupSummary.displayName(): String = "Group " + groupId.toString(16).padStart(16, '0')

@Composable
private fun V1BoardGroupRoster(
    group: V1BoardGroupSummary,
    canChange: Boolean,
    onStopTrust: () -> Unit,
) {
    val version = group.version
    val location = V1BoardGroupLocation.entries.firstOrNull { it.wireValue == group.locationRule }?.label ?: "Unknown"
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(group.displayName(), style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
            Text("Signed roster · Version $version")
            Text("Location sharing: $location")
            if (group.distrusted) Text("Trust stopped")
            group.members.forEach { member ->
                val role = if (member.administrator) "Administrator" else "Member"
                Text(member.alias + " · " + role)
            }
            if (!group.distrusted) {
                OutlinedButton(enabled = canChange, onClick = onStopTrust) {
                    Text("Stop trusting this group")
                }
            }
        }
    }
}
