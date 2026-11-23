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
    val canShowCoordinates: Boolean = false,
)

data class V1GroupJoinRequestUi(val key: String, val name: String)

/** Issued by the current authenticated invitation source after verifying the signed rule. */
class V1GroupJoinOffer internal constructor(
    val groupName: String,
    val locationRule: V1GroupLocationRule,
)

/** Fix readiness applies only to this offer and contains no coordinates. */
data class V1GroupVerifiedJoinState(val offer: V1GroupJoinOffer, val freshFixAvailable: Boolean = false)

data class V1GroupScreenState(
    val groupName: String? = null,
    val members: List<V1GroupMemberUi> = emptyList(),
    val isAdministrator: Boolean = false,
    val locationRule: V1GroupLocationRule = V1GroupLocationRule.OFF,
    val locationSharingEnabled: Boolean = false,
    val requests: List<V1GroupJoinRequestUi> = emptyList(),
    val canSubmit: Boolean = false,
    val statusMessage: String? = null,
    val invitationStatus: String = "Invitations closed",
    val canShowQr: Boolean = false,
    val canScanQr: Boolean = false,
    val confirmation: V1GroupConfirmationState = V1GroupConfirmationState.Unsupported,
    val setupAvailable: Boolean = true,
    val verifiedJoin: V1GroupVerifiedJoinState? = null,
    val locationState: PhoneLocationState = PhoneLocationState.OFF,
)

internal fun V1GroupScreenState.currentJoinOffer(offer: V1GroupJoinOffer): V1GroupVerifiedJoinState? =
    verifiedJoin?.takeIf { setupAvailable && canSubmit && it.offer === offer }

internal fun V1GroupScreenState.canCompleteJoin(offer: V1GroupJoinOffer, choice: V1GroupJoinChoice): Boolean =
    currentJoinOffer(offer)?.let { offer.locationRule.allowsJoin(choice, it.freshFixAvailable) } == true

sealed interface V1GroupUiAction {
    data class Create(val name: String) : V1GroupUiAction
    data class ReviewInvitation(val pin: String) : V1GroupUiAction
    data class PrepareJoinLocation(val offer: V1GroupJoinOffer) : V1GroupUiAction
    data class CompleteJoin(val offer: V1GroupJoinOffer, val choice: V1GroupJoinChoice) : V1GroupUiAction
    data class CancelJoin(val offer: V1GroupJoinOffer) : V1GroupUiAction
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
    data object RefreshDeviceOffer : V1GroupUiAction
    data class ConfirmDeviceOffer(val offer: V1GroupConfirmationOffer) : V1GroupUiAction
    data class CancelDeviceOffer(val offer: V1GroupConfirmationOffer) : V1GroupUiAction
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
            V1DeviceGroupConfirmation(state.confirmation, onAction)
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
    if (!state.setupAvailable) {
        Text("Your group", style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.semantics { heading() })
        state.statusMessage?.let { Text(it, color = MaterialTheme.colorScheme.onSurfaceVariant) }
        if (state.confirmation != V1GroupConfirmationState.Unsupported) {
            V1DeviceGroupConfirmation(state.confirmation, onAction)
        }
        return
    }
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
      Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
      V1DeviceGroupConfirmation(state.confirmation, onAction)
      V1GroupCard(if (createSelected && state.verifiedJoin == null) "Create a group" else "Join a group") {
        val offer = state.verifiedJoin?.offer
        if (offer != null) {
            V1GroupJoinConsent(state, offer, onAction)
        } else if (createSelected) {
            OutlinedTextField(name, { if (it.length <= 40) name = it }, label = { Text("Group name") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            Text("You will be the administrator. Location sharing starts off.")
            Button(
                onClick = { onAction(V1GroupUiAction.Create(name.trim())) },
                enabled = state.canSubmit && name.isNotBlank(), modifier = Modifier.fillMaxWidth(),
            ) { Text("Create private group") }
        } else {
            OutlinedTextField(pin, { if (it.length <= 16) pin = it.filter(Char::isDigit) },
                label = { Text("Invitation PIN") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            Text("Review the group's location rule before joining.")
            Button(onClick = { onAction(V1GroupUiAction.ReviewInvitation(pin)); pin = "" },
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
private fun V1GroupJoinConsent(
    state: V1GroupScreenState,
    offer: V1GroupJoinOffer,
    onAction: (V1GroupUiAction) -> Unit,
) {
    val currentState by rememberUpdatedState(state)
    var agreed by remember(offer, state.canSubmit) { mutableStateOf(false) }
    val share = V1GroupJoinChoice.SHARE_LOCATION
    val noShare = V1GroupJoinChoice.WITHOUT_LOCATION
    val complete: (V1GroupJoinChoice) -> Unit = { choice ->
        if (currentState.canCompleteJoin(offer, choice)) onAction(V1GroupUiAction.CompleteJoin(offer, choice))
    }
    Text(offer.groupName, style = MaterialTheme.typography.titleMedium)
    when (offer.locationRule) {
        V1GroupLocationRule.OFF -> {
            Text("Location is off for this group.")
            Button(onClick = { complete(noShare) }, enabled = state.canCompleteJoin(offer, noShare)) {
                Text("Join group")
            }
        }
        V1GroupLocationRule.MEMBER_OPTIONAL -> {
            Text("Location sharing is preferred. You can join either way.")
            Button(onClick = { complete(share) }, enabled = state.canCompleteJoin(offer, share)) {
                Text("Share and join")
            }
            OutlinedButton(onClick = { complete(noShare) }, enabled = state.canCompleteJoin(offer, noShare)) {
                Text("Join without sharing")
            }
        }
        V1GroupLocationRule.REQUIRED -> {
            Text("This group requires location sharing. Agree and get a phone GPS fix to join.")
            if (!agreed) {
                Button(onClick = {
                    if (currentState.currentJoinOffer(offer) != null) {
                        agreed = true
                        onAction(V1GroupUiAction.PrepareJoinLocation(offer))
                    }
                }, enabled = state.currentJoinOffer(offer) != null) { Text("Agree to share") }
            } else {
                if (!state.canCompleteJoin(offer, share)) Text(PhoneLocationState.WAITING_FOR_FIX.label)
                Button(onClick = { complete(share) }, enabled = state.canCompleteJoin(offer, share)) {
                    Text("Join group")
                }
            }
        }
    }
    TextButton(onClick = {
        agreed = false
        if (currentState.currentJoinOffer(offer) != null) onAction(V1GroupUiAction.CancelJoin(offer))
    }, enabled = state.currentJoinOffer(offer) != null) { Text("Cancel") }
}

@Composable
internal fun V1DeviceGroupConfirmation(
    state: V1GroupConfirmationState,
    onAction: (V1GroupUiAction) -> Unit,
) {
    // The offer belongs to the current device session. Never retain or save a copy here.
    V1GroupCard("Confirm someone nearby") {
        when (state) {
            V1GroupConfirmationState.Unsupported -> {
                Text("You can't confirm group changes with this connection.")
                Button(onClick = {}, enabled = false, modifier = Modifier.fillMaxWidth()) {
                    Text("Confirm matching details")
                }
            }
            V1GroupConfirmationState.Idle -> {
                Text("Waiting for your device to ask for confirmation.")
                OutlinedButton(
                    onClick = { onAction(V1GroupUiAction.RefreshDeviceOffer) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Check your device") }
            }
            is V1GroupConfirmationState.Review -> {
                val offer = state.offer
                Text(if (offer.role == V1GroupConfirmationRole.INVITER)
                    "Check who wants to join." else "Check who invited you.")
                Text("Compare these details with the other person before confirming.")
                Text("Device check code", style = MaterialTheme.typography.labelLarge)
                Text(offer.peerFingerprint)
                Text("Confirmation", style = MaterialTheme.typography.labelLarge)
                Text(offer.confirmation)
                Button(
                    onClick = { onAction(V1GroupUiAction.ConfirmDeviceOffer(offer)) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Confirm matching details") }
                OutlinedButton(
                    onClick = { onAction(V1GroupUiAction.CancelDeviceOffer(offer)) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Cancel confirmation") }
            }
            is V1GroupConfirmationState.Submitted -> {
                Text(if (state.decision == V1GroupConfirmationDecision.CONFIRM)
                    "MeshTrail asked your device to confirm.\nJoining is still pending."
                    else "MeshTrail asked your device to cancel.\nWait for it to confirm.")
            }
            is V1GroupConfirmationState.DeviceResult -> Text(when (state.outcome) {
                V1GroupConfirmationDeviceOutcome.LOCAL_CONFIRMED -> "Your device confirmed this step.\nJoining is still pending."
                V1GroupConfirmationDeviceOutcome.CANCELLED -> "Your device cancelled this confirmation."
                V1GroupConfirmationDeviceOutcome.REFUSED -> "Your device refused the confirmation.\nReconnect before reviewing a new invitation."
            })
            is V1GroupConfirmationState.Closed -> Text(when (state.reason) {
                V1GroupConfirmationCloseReason.EXPIRED -> "This invitation expired.\nReconnect and ask for a new one."
                V1GroupConfirmationCloseReason.SESSION_ENDED -> "The connection ended.\nReconnect to review a new invitation."
                V1GroupConfirmationCloseReason.SUBMISSION_FAILED -> "Couldn't confirm that the request reached your device.\nReconnect to review a new invitation."
                else -> "This confirmation is no longer available.\nReconnect to review a new invitation."
            })
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
                Text(if (state.locationRule == V1GroupLocationRule.OFF) "Location off" else member.locationStatus,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (state.locationRule != V1GroupLocationRule.OFF && member.canShowCoordinates) {
                        TextButton(onClick = { onAction(V1GroupUiAction.ShowCoordinates(member.key)) }) {
                            Text("Coordinates")
                        }
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
    var pendingRule by remember(state.groupName, state.isAdministrator, state.locationRule) {
        mutableStateOf<V1GroupLocationRule?>(null)
    }
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
            Text("Current rule: ${state.locationRule.label}")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(V1GroupLocationRule.OFF, V1GroupLocationRule.MEMBER_OPTIONAL,
                    V1GroupLocationRule.REQUIRED).forEach { rule ->
                    OutlinedButton(onClick = { pendingRule = rule },
                        enabled = state.canSubmit && rule != state.locationRule) { Text(rule.label) }
                }
            }
        }
    }
    V1GroupCard("Your location") {
        when (state.locationRule) {
            V1GroupLocationRule.OFF -> Text("Location is off for this group.")
            V1GroupLocationRule.MEMBER_OPTIONAL -> Text("Choose whether to share your phone location with this group.")
            V1GroupLocationRule.REQUIRED -> Text(if (state.locationSharingEnabled)
                "You can stop sharing and stay a member. Participation pauses until you agree again."
                else "Group participation is paused until you agree to share or leave.")
        }
        if (state.locationRule != V1GroupLocationRule.OFF) {
            Text(if (state.locationSharingEnabled) state.locationState.label else PhoneLocationState.OFF.label)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("I agree to share my location")
                Switch(checked = state.locationSharingEnabled,
                    onCheckedChange = { onAction(V1GroupUiAction.SetLocationSharing(it)) },
                    enabled = state.canSubmit)
            }
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
    val rule = pendingRule
    if (rule != null && state.isAdministrator) {
        val effect = when (rule) {
            V1GroupLocationRule.OFF -> "This stops location sharing for everyone."
            V1GroupLocationRule.MEMBER_OPTIONAL -> "Members choose again whether to share. Everyone keeps their membership."
            V1GroupLocationRule.REQUIRED -> "Members keep their membership. Participation pauses until each person agrees."
        }
        V1GroupConfirmation("Set location rule to ${rule.label}?", effect, "Change rule", state.canSubmit,
            { pendingRule = null }) {
            onAction(V1GroupUiAction.SetLocationRule(rule))
            pendingRule = null
        }
    }
}

private val V1GroupLocationRule.label: String
    get() = when (this) {
        V1GroupLocationRule.OFF -> "Off"
        V1GroupLocationRule.MEMBER_OPTIONAL -> "Preferred"
        V1GroupLocationRule.REQUIRED -> "Required"
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
