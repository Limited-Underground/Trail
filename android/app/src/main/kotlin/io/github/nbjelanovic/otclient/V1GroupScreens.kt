@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.movableContentOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

/** Presentation keys are caller-owned opaque handles, never radio or Bluetooth identities. */
data class V1GroupMemberUi(
    val key: String,
    val name: String,
    val isAdministrator: Boolean = false,
    val isYou: Boolean = false,
    val locationStatus: String = "Location unavailable",
)

data class V1GroupJoinRequestUi(val key: String, val name: String)

data class V1GroupScreenState(
    val groupName: String? = null,
    val members: List<V1GroupMemberUi> = emptyList(),
    val isAdministrator: Boolean = false,
    val locationRule: V1GroupLocationRule = V1GroupLocationRule.REQUIRED,
    val locationSharingEnabled: Boolean = true,
    val requests: List<V1GroupJoinRequestUi> = emptyList(),
    val canSubmit: Boolean = false,
    val statusMessage: String? = null,
    val invitationStatus: String = "Invitations closed",
    val canShowQr: Boolean = false,
    val canScanQr: Boolean = false,
)

sealed interface V1GroupUiAction {
    data class Create(val name: String) : V1GroupUiAction
    data class Join(val pin: String) : V1GroupUiAction
    data object ScanQr : V1GroupUiAction
    data class Rename(val name: String) : V1GroupUiAction
    data class SetLocationRule(val rule: V1GroupLocationRule) : V1GroupUiAction
    data class SetLocationSharing(val enabled: Boolean) : V1GroupUiAction
    data class Approve(val key: String) : V1GroupUiAction
    data class Deny(val key: String) : V1GroupUiAction
    data class Remove(val key: String) : V1GroupUiAction
    data class OpenInvitations(val minutes: Int) : V1GroupUiAction
    data object RevokeInvitations : V1GroupUiAction
    data object ShowQr : V1GroupUiAction
    data class ShowCoordinates(val key: String) : V1GroupUiAction
    data object Leave : V1GroupUiAction
    data object Delete : V1GroupUiAction
}

/** Mutations are requests only. The caller supplies device-confirmed state and results. */
@Composable
fun V1GroupScreen(
    state: V1GroupScreenState,
    onAction: (V1GroupUiAction) -> Unit,
    modifier: Modifier = Modifier,
    initiallyCreateGroup: Boolean = true,
) {
    val currentState by rememberUpdatedState(state)
    val currentAction by rememberUpdatedState(onAction)
    // Preserve edits and confirmations when members and controls move into columns.
    val retainedMembers = remember {
        movableContentOf { V1GroupMembers(currentState, currentAction) }
    }
    val retainedControls = remember {
        movableContentOf { V1GroupControls(currentState, currentAction) }
    }
    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        if (state.groupName == null) {
            V1NoGroupContent(state, onAction, initiallyCreateGroup)
        } else {
            Text(state.groupName, style = MaterialTheme.typography.headlineMedium,
                modifier = Modifier.semantics { heading() })
            state.statusMessage?.let { Text(it, color = MaterialTheme.colorScheme.onSurfaceVariant) }
            Text("${state.members.size} of 6 members · One administrator")
            BoxWithConstraints(Modifier.fillMaxWidth()) {
                if (maxWidth >= 720.dp) {
                    Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                            retainedMembers()
                            V1GroupRequests(state, onAction)
                        }
                        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                            retainedControls()
                        }
                    }
                } else {
                    Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                        retainedMembers()
                        V1GroupRequests(state, onAction)
                        retainedControls()
                    }
                }
            }
        }
    }
}

@Composable
private fun V1NoGroupContent(
    state: V1GroupScreenState, onAction: (V1GroupUiAction) -> Unit, initiallyCreateGroup: Boolean,
) {
    var createSelected by rememberSaveable { mutableStateOf(initiallyCreateGroup) }
    var name by rememberSaveable { mutableStateOf("") }
    // Invitation secrets deliberately do not enter saved instance state.
    var pin by androidx.compose.runtime.remember { mutableStateOf("") }
    val introduction: @Composable () -> Unit = {
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Your group", style = MaterialTheme.typography.headlineMedium,
                modifier = Modifier.semantics { heading() })
            Text("One private group. Up to six people.")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { createSelected = true }, modifier = Modifier.weight(1f)) {
                    Text(if (createSelected) "Create ✓" else "Create")
                }
                OutlinedButton(onClick = { createSelected = false }, modifier = Modifier.weight(1f)) {
                    Text(if (!createSelected) "Join ✓" else "Join")
                }
            }
            state.statusMessage?.let { Text(it, color = MaterialTheme.colorScheme.onSurfaceVariant) }
        }
    }
    val form: @Composable () -> Unit = {
      V1GroupCard(if (createSelected) "Create a group" else "Join a group") {
        if (createSelected) {
            OutlinedTextField(name, { if (it.length <= 40) name = it }, label = { Text("Group name") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            Text("You will be the administrator. Location sharing starts ON.")
            Button(
                onClick = { onAction(V1GroupUiAction.Create(name.trim())) },
                enabled = state.canSubmit && name.isNotBlank(), modifier = Modifier.fillMaxWidth(),
            ) { Text("Create private group") }
        } else {
            OutlinedTextField(pin, { if (it.length <= 16) pin = it.filter(Char::isDigit) },
                label = { Text("Invitation PIN") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            Text("Review the group's location rule before joining. Sharing starts ON.")
            Button(onClick = { onAction(V1GroupUiAction.Join(pin)); pin = "" },
                enabled = state.canSubmit && pin.isNotBlank(), modifier = Modifier.fillMaxWidth()) {
                Text("Review invitation")
            }
            OutlinedButton(onClick = { onAction(V1GroupUiAction.ScanQr) },
                enabled = state.canSubmit && state.canScanQr, modifier = Modifier.fillMaxWidth()) {
                Text("Scan invitation QR")
            }
        }
      }
    }
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        if (maxWidth >= 600.dp) {
            Row(horizontalArrangement = Arrangement.spacedBy(20.dp)) {
                Column(Modifier.weight(0.85f)) { introduction() }
                Column(Modifier.weight(1.15f)) { form() }
            }
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                introduction()
                form()
            }
        }
    }
}

@Composable
private fun V1GroupMembers(state: V1GroupScreenState, onAction: (V1GroupUiAction) -> Unit) {
    var removeKey by rememberSaveable(state.groupName) { mutableStateOf<String?>(null) }
    V1GroupCard("Members") {
        state.members.forEach { member ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(member.name + if (member.isYou) " (you)" else "",
                    style = MaterialTheme.typography.titleMedium)
                Text(if (member.isAdministrator) "Administrator" else "Member")
                Text(member.locationStatus, color = MaterialTheme.colorScheme.onSurfaceVariant)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { onAction(V1GroupUiAction.ShowCoordinates(member.key)) }) {
                        Text("Coordinates")
                    }
                    if (state.isAdministrator && !member.isAdministrator) {
                        TextButton(onClick = { removeKey = member.key }, enabled = state.canSubmit) {
                            Text("Remove")
                        }
                    }
                }
            }
        }
    }
    val member = state.members.firstOrNull { it.key == removeKey && !it.isAdministrator }
    if (member != null && state.isAdministrator) {
        V1GroupConfirmation("Remove ${member.name}?",
            "They will lose access to group messages, members, and shared locations.", "Remove",
            state.canSubmit, { removeKey = null }) {
            onAction(V1GroupUiAction.Remove(member.key)); removeKey = null
        }
    }
}

@Composable
private fun V1GroupRequests(state: V1GroupScreenState, onAction: (V1GroupUiAction) -> Unit) {
    if (!state.isAdministrator) return
    V1GroupCard("Join requests") {
        if (state.requests.isEmpty()) Text("No requests waiting")
        state.requests.forEach { request ->
            Text(request.name, style = MaterialTheme.typography.titleMedium)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { onAction(V1GroupUiAction.Approve(request.key)) },
                    enabled = state.canSubmit && state.members.size < V1_MAX_GROUP_MEMBERS) { Text("Approve") }
                OutlinedButton(onClick = { onAction(V1GroupUiAction.Deny(request.key)) },
                    enabled = state.canSubmit) { Text("Deny") }
            }
        }
        if (state.members.size >= V1_MAX_GROUP_MEMBERS) Text("Group full. Remove a member before approving someone else.")
    }
}

@Composable
private fun V1GroupControls(state: V1GroupScreenState, onAction: (V1GroupUiAction) -> Unit) {
    var destructiveConfirmation by rememberSaveable(state.groupName, state.isAdministrator) { mutableStateOf(false) }
    var requiredConfirmation by rememberSaveable(state.groupName, state.isAdministrator) { mutableStateOf(false) }
    var editedName by rememberSaveable(state.groupName) { mutableStateOf(state.groupName.orEmpty()) }
    if (state.isAdministrator) {
        V1GroupCard("Invitations & joining") {
            Text(state.invitationStatus)
            Text("Open discovery briefly so nearby people can request to join. Only the group name is visible.")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(5, 15, 30).forEach { minutes ->
                    OutlinedButton(onClick = { onAction(V1GroupUiAction.OpenInvitations(minutes)) },
                        enabled = state.canSubmit && state.members.size < V1_MAX_GROUP_MEMBERS) { Text("$minutes min") }
                }
            }
            OutlinedButton(onClick = { onAction(V1GroupUiAction.ShowQr) },
                enabled = state.canSubmit && state.canShowQr, modifier = Modifier.fillMaxWidth()) {
                Text("Show QR / temporary PIN")
            }
            TextButton(onClick = { onAction(V1GroupUiAction.RevokeInvitations) },
                enabled = state.canSubmit) { Text("Close joining & revoke invitations") }
        }
        V1GroupCard("Group settings") {
            OutlinedTextField(editedName, { if (it.length <= 40) editedName = it },
                label = { Text("Group name") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedButton(onClick = { onAction(V1GroupUiAction.Rename(editedName.trim())) },
                enabled = state.canSubmit && editedName.isNotBlank() && editedName.trim() != state.groupName) {
                Text("Save name")
            }
            Text("Location sharing", style = MaterialTheme.typography.titleMedium)
            Text(if (state.locationRule == V1GroupLocationRule.REQUIRED)
                "Required for everyone. Members leave the group to stop sharing."
                else "ON when joining. Each member may turn sharing off.")
            OutlinedButton(onClick = {
                if (state.locationRule == V1GroupLocationRule.REQUIRED) {
                    onAction(V1GroupUiAction.SetLocationRule(V1GroupLocationRule.MEMBER_OPTIONAL))
                } else requiredConfirmation = true
            }, enabled = state.canSubmit, modifier = Modifier.fillMaxWidth()) {
                Text(if (state.locationRule == V1GroupLocationRule.REQUIRED)
                    "Let members turn sharing off" else "Require location sharing")
            }
        }
    }
    V1GroupCard("Your location") {
        Text(if (state.locationRule == V1GroupLocationRule.REQUIRED)
            "Your administrator requires sharing. GPS loss is shown as unavailable."
            else "Choose whether this group receives your location.")
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(if (state.locationSharingEnabled) "Sharing ON" else "Sharing OFF")
            Switch(checked = state.locationSharingEnabled,
                onCheckedChange = { onAction(V1GroupUiAction.SetLocationSharing(it)) },
                enabled = state.canSubmit && state.locationRule == V1GroupLocationRule.MEMBER_OPTIONAL)
        }
    }
    OutlinedButton(onClick = { destructiveConfirmation = true },
        enabled = state.canSubmit, modifier = Modifier.fillMaxWidth()) {
        Text(if (state.isAdministrator) "Delete group" else "Leave group")
    }
    if (state.isAdministrator) Text("V1 has one administrator. Delete the group before leaving.")
    if (destructiveConfirmation) {
        V1GroupConfirmation(if (state.isAdministrator) "Delete this group?" else "Leave this group?",
            if (state.isAdministrator) "This ends the group for all members. You cannot undo this action."
            else "You will stop sharing location and lose group access. You need a new invitation to return.",
            if (state.isAdministrator) "Delete group" else "Leave group", state.canSubmit,
            { destructiveConfirmation = false }) {
            onAction(if (state.isAdministrator) V1GroupUiAction.Delete else V1GroupUiAction.Leave)
            destructiveConfirmation = false
        }
    }
    if (requiredConfirmation && state.isAdministrator) {
        V1GroupConfirmation("Require location sharing?",
            "Members who turned sharing off must enable it themselves or leave before this rule can change.",
            "Request required sharing", state.canSubmit, { requiredConfirmation = false }) {
            onAction(V1GroupUiAction.SetLocationRule(V1GroupLocationRule.REQUIRED))
            requiredConfirmation = false
        }
    }
}

@Composable
private fun V1GroupCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
            content()
        }
    }
}

@Composable
private fun V1GroupConfirmation(
    title: String, message: String, confirmLabel: String, enabled: Boolean,
    onDismiss: () -> Unit, onConfirm: () -> Unit,
) {
    AlertDialog(onDismissRequest = onDismiss, title = { Text(title) }, text = { Text(message) },
        confirmButton = { TextButton(onClick = onConfirm, enabled = enabled) { Text(confirmLabel) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}
