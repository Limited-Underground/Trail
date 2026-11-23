@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package io.github.nbjelanovic.otclient

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.CompanionGroupConfigurationCodec
import io.github.nbjelanovic.otprotocol.CompanionGroupInvite
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.compose.LocalLifecycleOwner

@Composable
internal fun V1BoardGroupPanel(
    state: V1BoardGroupState,
    connected: Boolean,
    onAction: (V1BoardGroupAction) -> Boolean,
    locationAuthority: ((V1BoardGroupSummary) -> V1GroupLocationAuthority?)? = null,
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
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    var location by remember(context, lifecycleOwner) { mutableStateOf<BoardGroupLocationSelection?>(null) }
    var permissionRequest by remember { mutableStateOf<BoardGroupLocationSelection?>(null) }
    var locationRefused by remember { mutableStateOf<V1BoardGroupSummary?>(null) }
    val permissionLauncher = if (locationAuthority != null) {
        rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
            val requested = permissionRequest
            permissionRequest = null
            if (requested != null && location === requested && requested.authority.isCurrent()) {
                requested.adapter.refresh()
            }
        }
    } else null

    val selection = location
    val selected = selection?.takeIf { current ->
        groups.any { it === current.group } && current.authority.isCurrent()
    }
    LaunchedEffect(selection, selected) {
        if (selection != null && selected == null && location === selection) {
            location = null
            permissionRequest = null
            selection.adapter.close()
        }
    }
    DisposableEffect(context, lifecycleOwner) {
        onDispose {
            location?.adapter?.close()
            location = null
            permissionRequest = null
        }
    }

    fun selectLocation(group: V1BoardGroupSummary) {
        if (!canChange || location != null || group.distrusted || group.locationRule !in 1..2 ||
            groups.none { it === group }
        ) return
        val authority = locationAuthority?.invoke(group)?.takeIf { it.isCurrent() }
        if (authority == null) {
            locationRefused = group
            return
        }
        locationRefused = null
        val picked = BoardGroupLocationSelection(group, authority, context, lifecycleOwner)
        location = picked
        picked.adapter.chooseShare()
    }

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
        state.invite?.takeIf { currentRoster }?.let { V1BoardGroupInviteCard(it) }
        groups.forEach { group ->
            V1BoardGroupRoster(
                group,
                canChange,
                onInvite = { send(V1BoardGroupAction.Invite(group, group.displayName())) },
                onStopTrust = { stopping = group },
            ) {
                if (!group.distrusted && group.locationRule in 1..2) {
                    if (selected != null && selected.group === group) {
                        BoardGroupLocationControls(
                            state = selected.state,
                            canRequestPermission = permissionLauncher != null && permissionRequest == null,
                            onShare = selected.adapter::chooseShare,
                            onResume = selected.adapter::resumeSharing,
                            onPermission = {
                                if (permissionLauncher != null && permissionRequest == null &&
                                    location === selected && selected.authority.isCurrent()
                                ) {
                                    selected.adapter.askPermission { permissions ->
                                        permissionRequest = selected
                                        permissionLauncher.launch(permissions)
                                    }
                                }
                            },
                            onSettings = {
                                if (location === selected && selected.authority.isCurrent()) {
                                    try {
                                        context.startActivity(Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS))
                                    } catch (_: ActivityNotFoundException) {
                                        selected.settingsUnavailable = true
                                    }
                                }
                            },
                            settingsUnavailable = selected.settingsUnavailable,
                            onStop = {
                                selected.adapter.stopSharing()
                                selected.adapter.close()
                                if (location === selected) location = null
                                if (permissionRequest === selected) permissionRequest = null
                            },
                        )
                    } else if (locationAuthority != null && selection == null) {
                        if (locationRefused === group) Text("Location sharing is unavailable for this group.")
                        OutlinedButton(onClick = { selectLocation(group) }, enabled = canChange) {
                            Text("Share my location")
                        }
                    }
                }
            }
        }
        if (currentRoster && state.canCreate) V1BoardGroupJoin(canChange) { send(V1BoardGroupAction.Join(it)) }
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

private class BoardGroupLocationSelection(
    val group: V1BoardGroupSummary,
    val authority: V1GroupLocationAuthority,
    context: Context,
    lifecycleOwner: LifecycleOwner,
) {
    var state by mutableStateOf(PhoneLocationState.OFF)
        private set
    var settingsUnavailable by mutableStateOf(false)

    val adapter = AndroidV1ForegroundLocation(
        context,
        lifecycleOwner,
        boardBinding = { authority.binding.takeIf { authority.isCurrent() } },
        board = authority.board,
        onStateChanged = { state = it },
    )
}

@Composable
private fun BoardGroupLocationControls(
    state: PhoneLocationState,
    canRequestPermission: Boolean,
    onShare: () -> Unit,
    onResume: () -> Unit,
    onPermission: () -> Unit,
    onSettings: () -> Unit,
    settingsUnavailable: Boolean,
    onStop: () -> Unit,
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        when (state) {
            PhoneLocationState.OFF -> OutlinedButton(onClick = onShare) { Text("Share my location") }
            PhoneLocationState.ALLOW_PHONE_LOCATION ->
                OutlinedButton(onClick = onPermission, enabled = canRequestPermission) { Text(state.label) }
            PhoneLocationState.TURN_ON_PHONE_LOCATION ->
                OutlinedButton(onClick = onSettings) { Text(state.label) }
            PhoneLocationState.PAUSED -> OutlinedButton(onClick = onResume) { Text("Resume sharing") }
            else -> Unit
        }
        TextButton(onClick = onStop) { Text("Stop sharing") }
    }
    when {
        settingsUnavailable && state == PhoneLocationState.TURN_ON_PHONE_LOCATION ->
            Text("Open phone settings to turn on location.")
        state == PhoneLocationState.PAUSED || state == PhoneLocationState.WAITING_FOR_FIX ||
            state == PhoneLocationState.SHARING -> Text(state.label)
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
    onInvite: () -> Unit,
    onStopTrust: () -> Unit,
    locationContent: @Composable () -> Unit,
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
            if (!group.distrusted && group.members.size < 6) {
                Button(enabled = canChange, onClick = onInvite) { Text("Invite someone") }
            }
            if (!group.distrusted) {
                locationContent()
                OutlinedButton(enabled = canChange, onClick = onStopTrust) {
                    Text("Stop trusting this group")
                }
            }
        }
    }
}

@Composable
private fun V1BoardGroupInviteCard(invite: V1BoardGroupInvite) {
    val qr = V1InviteQrBinding.current
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Invitation to ${invite.label}", style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() })
            Text("Works once, for 3 minutes, while your device stays on.")
            val matrix = qr?.matrix(invite.code)
            if (matrix != null) {
                V1InviteQrImage(matrix)
                Text("Let the person joining scan this code.")
            } else {
                Text("Invite code. Send it to the person joining, then they paste it under Join a group.")
                SelectionContainer { Text(invite.code, style = MaterialTheme.typography.bodySmall) }
            }
        }
    }
}

@Composable
private fun V1InviteQrImage(matrix: Array<BooleanArray>) {
    val size = matrix.size
    androidx.compose.foundation.Canvas(Modifier.fillMaxWidth().padding(16.dp).height(240.dp)) {
        val cell = minOf(this.size.width, this.size.height) / (size + 8)
        val left = (this.size.width - cell * size) / 2
        val top = (this.size.height - cell * size) / 2
        drawRect(androidx.compose.ui.graphics.Color.White)
        matrix.forEachIndexed { y, row ->
            row.forEachIndexed { x, dark ->
                if (dark) drawRect(
                    androidx.compose.ui.graphics.Color.Black,
                    topLeft = androidx.compose.ui.geometry.Offset(left + x * cell, top + y * cell),
                    size = androidx.compose.ui.geometry.Size(cell, cell),
                )
            }
        }
    }
}

private fun locationLabel(rule: Int) = when (rule) {
    0 -> "Off"
    1 -> "Preferred"
    2 -> "Required"
    else -> "Unknown"
}

@Composable
internal fun V1BoardGroupJoin(canChange: Boolean, onJoin: (ByteArray) -> Unit) {
    var text by remember { mutableStateOf("") }
    var reviewing by remember { mutableStateOf<Pair<ByteArray, CompanionGroupInvite>?>(null) }
    var invalid by remember { mutableStateOf(false) }
    val qr = V1InviteQrBinding.current
    fun review(code: String) {
        val bytes = V1InviteCodeText.decode(code)
        val invite = bytes?.let { CompanionGroupConfigurationCodec.decodeInvite(it) }
        invalid = invite == null
        reviewing = if (bytes != null && invite != null) bytes to invite else null
    }
    Text("Join a group", style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
    if (qr != null) {
        OutlinedButton(enabled = canChange, onClick = { qr.scan { scanned -> scanned?.let(::review) } }) {
            Text("Scan invitation")
        }
    }
    OutlinedTextField(
        value = text,
        onValueChange = { text = it; invalid = false },
        label = { Text("Invite code") },
        enabled = canChange,
        modifier = Modifier.fillMaxWidth(),
    )
    if (invalid) Text("That isn't a MeshTrail invite code.")
    Button(enabled = canChange && text.isNotBlank(), onClick = { review(text) }) { Text("Review invitation") }
    reviewing?.let { (bytes, invite) ->
        AlertDialog(
            onDismissRequest = { reviewing = null },
            title = { Text("Join ${invite.label}?") },
            text = {
                Text("Location sharing: ${locationLabel(invite.locationRule)}\n" +
                    "Your device checks the administrator's signature before it accepts.")
            },
            confirmButton = {
                TextButton(enabled = canChange, onClick = {
                    reviewing = null
                    text = ""
                    onJoin(bytes)
                }) { Text("Join") }
            },
            dismissButton = { TextButton(onClick = { reviewing = null }) { Text("Cancel") } },
        )
    }
}
