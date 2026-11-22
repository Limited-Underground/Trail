@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package io.github.nbjelanovic.otclient

import android.content.res.Configuration
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner

/** Public discovery presents only a chosen name and a caller-owned opaque UI key. */
data class V1NearbyPersonUi(val key: String, val name: String)

enum class V1DirectMessageStatus { RECEIVED, QUEUED, SENT, DELIVERED }
data class V1DirectMessageUi(val text: String, val status: V1DirectMessageStatus)

data class V1PeopleScreenState(
    val people: List<V1NearbyPersonUi> = emptyList(),
    val selectedPerson: V1NearbyPersonUi? = null,
    val contactState: V1DirectContactState = V1DirectContactState.AVAILABLE,
    val canSubmit: Boolean = false,
    val discoveryAvailable: Boolean = false,
    val publiclyVisible: Boolean = true,
    val locationSharingEnabled: Boolean = false,
    val messages: List<V1DirectMessageUi> = emptyList(),
    val statusMessage: String? = null,
    val locationConsentAvailable: Boolean = false,
    val incomingInvitation: V1ChatRequestToken? = null,
    val locationOptedIn: Boolean = false,
    val phoneLocationState: PhoneLocationState = PhoneLocationState.OFF,
) {
    /** Fail closed even if a caller accidentally supplies message content before consent. */
    val visibleMessages: List<V1DirectMessageUi>
        get() = if (contactState == V1DirectContactState.ACCEPTED) messages else emptyList()
    /** Only the live foreground controller can report Sharing. */
    val effectiveLocationSharing: Boolean
        get() = contactState == V1DirectContactState.ACCEPTED && phoneLocationState == PhoneLocationState.SHARING
    val mayChangeLocationSharing: Boolean
        get() = contactState == V1DirectContactState.ACCEPTED && canSubmit && locationConsentAvailable
    val mayChooseInvitationLocation: Boolean
        get() = canSubmit && locationConsentAvailable && when (contactState) {
            V1DirectContactState.AVAILABLE, V1DirectContactState.DECLINED -> true
            V1DirectContactState.INCOMING_REQUEST_PENDING -> incomingInvitation != null
            else -> false
        }
}

sealed interface V1PeopleUiAction {
    data object Back : V1PeopleUiAction
    data object Refresh : V1PeopleUiAction
    data class OpenPerson(val key: String) : V1PeopleUiAction
    data class SetVisibility(val enabled: Boolean) : V1PeopleUiAction
    /** firstMessage stays local until this request is accepted; it is not a request payload. */
    data class RequestChat(
        val key: String,
        val firstMessage: String,
        val shareWhenAccepted: Boolean = false,
    ) : V1PeopleUiAction
    data class Accept(
        val key: String,
        val shareBack: Boolean = false,
        val invitation: V1ChatRequestToken? = null,
    ) : V1PeopleUiAction
    data class Decline(val key: String) : V1PeopleUiAction
    data class Block(val key: String) : V1PeopleUiAction
    data class SendMessage(val key: String, val text: String) : V1PeopleUiAction
    data class SetLocationSharing(val key: String, val enabled: Boolean) : V1PeopleUiAction
    data class ResumeLocationSharing(val key: String) : V1PeopleUiAction
    data class RequestLocationPermission(val key: String) : V1PeopleUiAction
    data class OpenLocationSettings(val key: String) : V1PeopleUiAction
}

internal object V1DirectDraftPolicy {
    const val MAX_CHARS = 500
    fun valid(value: String): Boolean = value.isNotBlank() && value.length <= MAX_CHARS &&
        value.none { it.isISOControl() && it != '\n' }
}

/** UI requests only; the caller must supply transport-confirmed contact and delivery state. */
@Composable
fun V1PeopleScreen(state: V1PeopleScreenState, onAction: (V1PeopleUiAction) -> Unit,
    modifier: Modifier = Modifier) {
    val currentState by rememberUpdatedState(state)
    val currentAction by rememberUpdatedState(onAction)
    val retainedBody = remember {
        movableContentOf {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                PeopleBody(currentState, currentAction)
            }
        }
    }
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    BoxWithConstraints(modifier.fillMaxSize()) {
    // The navigation rail and split-window mode can leave a landscape pane compact.
    if (landscape && maxWidth >= 600.dp) {
        Row(Modifier.fillMaxSize().padding(16.dp), horizontalArrangement = Arrangement.spacedBy(20.dp)) {
            Column(Modifier.weight(0.32f).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                PeopleHeader(state, onAction)
            }
            Column(Modifier.weight(0.68f).verticalScroll(rememberScrollState())) {
                retainedBody()
            }
        }
    } else {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            PeopleHeader(state, onAction)
            retainedBody()
        }
    }
    }
}

@Composable
private fun PeopleHeader(state: V1PeopleScreenState, onAction: (V1PeopleUiAction) -> Unit) {
        val person = state.selectedPerson
        TextButton(onClick = { onAction(V1PeopleUiAction.Back) }) {
            Text(if (person == null) "Back to Messages" else "Back to Nearby people")
        }
        Text(person?.name ?: "Nearby people", style = MaterialTheme.typography.headlineMedium)
        state.statusMessage?.let { Text(it, color = MaterialTheme.colorScheme.onSurfaceVariant) }
}

@Composable
private fun PeopleBody(state: V1PeopleScreenState, onAction: (V1PeopleUiAction) -> Unit) {
        val person = state.selectedPerson
        if (person == null) {
            Row(Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text("Visible to nearby people", style = MaterialTheme.typography.titleMedium)
                    Text("Only your display name is visible. Your group and location are not shared.")
                }
                Switch(state.publiclyVisible, { onAction(V1PeopleUiAction.SetVisibility(it)) },
                    enabled = state.canSubmit)
            }
            if (!state.discoveryAvailable) {
                PeopleCard("Discovery not available yet") {
                    Text("Your device needs an update to find nearby people and start chats.")
                }
            } else if (state.people.isEmpty()) {
                PeopleCard("No people found") { Text("People who have enabled visibility can appear here.") }
            } else {
                state.people.forEach { nearby ->
                    PeopleCard(nearby.name) {
                        OutlinedButton(onClick = { onAction(V1PeopleUiAction.OpenPerson(nearby.key)) }) {
                            Text("Request a chat")
                        }
                    }
                }
            }
            OutlinedButton(onClick = { onAction(V1PeopleUiAction.Refresh) },
                enabled = state.discoveryAvailable && state.canSubmit) { Text("Refresh nearby people") }
        } else {
            val locationChoice = remember(
                person.key, state.contactState, state.incomingInvitation, state.mayChooseInvitationLocation,
            ) { mutableStateOf(false) }
            val lifecycle = LocalLifecycleOwner.current.lifecycle
            DisposableEffect(lifecycle, locationChoice) {
                val observer = LifecycleEventObserver { _, event ->
                    if (event == Lifecycle.Event.ON_PAUSE) locationChoice.value = false
                }
                lifecycle.addObserver(observer)
                onDispose { lifecycle.removeObserver(observer) }
            }
            when (state.contactState) {
                V1DirectContactState.AVAILABLE, V1DirectContactState.DECLINED -> {
                    PeopleCard("Start a conversation") {
                        Text("Write your first message. They receive only a chat request until they accept; your message stays on this phone.")
                        if (state.mayChooseInvitationLocation) {
                            DirectLocationChoice("Share when accepted", locationChoice.value) { locationChoice.value = it }
                        }
                        PeopleComposer(person.key, state.canSubmit, "First message", "Request chat") {
                            val share = state.mayChooseInvitationLocation && locationChoice.value
                            locationChoice.value = false
                            onAction(V1PeopleUiAction.RequestChat(person.key, it, share))
                        }
                    }
                }
                V1DirectContactState.INCOMING_REQUEST_PENDING -> PeopleCard("Chat request") {
                    Text("${person.name} is requesting to chat with you")
                    Text("Message content is hidden until you accept.")
                    if (state.mayChooseInvitationLocation) {
                        DirectLocationChoice("Share back", locationChoice.value) { locationChoice.value = it }
                    }
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = {
                            val share = state.mayChooseInvitationLocation && locationChoice.value
                            locationChoice.value = false
                            onAction(V1PeopleUiAction.Accept(person.key, share, state.incomingInvitation))
                        }, enabled = state.canSubmit) { Text("Accept") }
                        OutlinedButton(onClick = { onAction(V1PeopleUiAction.Decline(person.key)) }, enabled = state.canSubmit) { Text("Decline") }
                    }
                    OutlinedButton(onClick = { onAction(V1PeopleUiAction.Block(person.key)) }, enabled = state.canSubmit) { Text("Block") }
                }
                V1DirectContactState.OUTGOING_REQUEST_PENDING -> PeopleCard("Waiting for acceptance") {
                    Text("${person.name} has not accepted your chat request. Your first message has not been delivered.")
                    Text("Location sharing is OFF.")
                }
                V1DirectContactState.BLOCKED -> PeopleCard("Contact blocked") {
                    Text("New chat requests from this person are blocked. Location sharing is OFF.")
                }
                V1DirectContactState.ACCEPTED -> {
                    val locationAction = if (state.mayChangeLocationSharing) {
                        when (state.phoneLocationState) {
                            PhoneLocationState.ALLOW_PHONE_LOCATION ->
                                V1PeopleUiAction.RequestLocationPermission(person.key)
                            PhoneLocationState.TURN_ON_PHONE_LOCATION ->
                                V1PeopleUiAction.OpenLocationSettings(person.key)
                            PhoneLocationState.PAUSED -> V1PeopleUiAction.ResumeLocationSharing(person.key)
                            else -> null
                        }
                    } else null
                    Row(Modifier.fillMaxWidth()) {
                        Column(Modifier.weight(1f)) {
                            Text("Share location in this chat", style = MaterialTheme.typography.titleMedium)
                            if (locationAction == null || state.phoneLocationState == PhoneLocationState.PAUSED) {
                                Text(when {
                                    state.phoneLocationState != PhoneLocationState.OFF -> state.phoneLocationState.label
                                    state.locationOptedIn -> "Location sharing requested"
                                    else -> PhoneLocationState.OFF.label
                                })
                            }
                        }
                        Switch(state.locationOptedIn || state.effectiveLocationSharing,
                            { onAction(V1PeopleUiAction.SetLocationSharing(person.key, it)) },
                            enabled = state.mayChangeLocationSharing)
                    }
                    if (locationAction != null) {
                        OutlinedButton(onClick = { onAction(locationAction) }) {
                            Text(if (state.phoneLocationState == PhoneLocationState.PAUSED) {
                                "Resume"
                            } else state.phoneLocationState.label)
                        }
                    }
                    if (state.visibleMessages.isEmpty()) Text("Chat accepted. No messages yet.")
                    state.visibleMessages.forEach { message ->
                        PeopleCard(message.text) { Text(message.status.name.lowercase().replaceFirstChar { it.uppercase() }) }
                    }
                    PeopleComposer(person.key, state.canSubmit, "Message", "Send") {
                        onAction(V1PeopleUiAction.SendMessage(person.key, it))
                    }
                }
            }
        }
}

@Composable
private fun DirectLocationChoice(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().toggleable(value = checked, role = Role.Checkbox, onValueChange = onChange),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Checkbox(checked = checked, onCheckedChange = null)
        Text(label)
    }
}

@Composable
private fun PeopleComposer(key: String, enabled: Boolean, label: String, action: String,
    onSubmit: (String) -> Unit) {
    var draft by rememberSaveable(key) { mutableStateOf("") }
    OutlinedTextField(draft, { if (it.length <= V1DirectDraftPolicy.MAX_CHARS) draft = it },
        label = { Text(label) }, supportingText = { Text("${draft.length}/${V1DirectDraftPolicy.MAX_CHARS}") },
        modifier = Modifier.fillMaxWidth(), minLines = 2, maxLines = 5)
    Button(onClick = { onSubmit(draft) }, enabled = enabled && V1DirectDraftPolicy.valid(draft)) { Text(action) }
}

@Composable
private fun PeopleCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}
