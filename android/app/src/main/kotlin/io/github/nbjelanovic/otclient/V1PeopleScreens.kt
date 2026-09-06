@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package io.github.nbjelanovic.otclient

import android.content.res.Configuration
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp

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
) {
    /** Fail closed even if a caller accidentally supplies message content before consent. */
    val visibleMessages: List<V1DirectMessageUi>
        get() = if (contactState == V1DirectContactState.ACCEPTED) messages else emptyList()
    val effectiveLocationSharing: Boolean
        get() = contactState == V1DirectContactState.ACCEPTED && locationSharingEnabled
}

sealed interface V1PeopleUiAction {
    data object Back : V1PeopleUiAction
    data object Refresh : V1PeopleUiAction
    data class OpenPerson(val key: String) : V1PeopleUiAction
    data class SetVisibility(val enabled: Boolean) : V1PeopleUiAction
    /** firstMessage stays local until this request is accepted; it is not a request payload. */
    data class RequestChat(val key: String, val firstMessage: String) : V1PeopleUiAction
    data class Accept(val key: String) : V1PeopleUiAction
    data class Decline(val key: String) : V1PeopleUiAction
    data class Block(val key: String) : V1PeopleUiAction
    data class SendMessage(val key: String, val text: String) : V1PeopleUiAction
    data class SetLocationSharing(val key: String, val enabled: Boolean) : V1PeopleUiAction
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
                    Text("Nearby discovery and chat requests require the upcoming device update.")
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
            when (state.contactState) {
                V1DirectContactState.AVAILABLE, V1DirectContactState.DECLINED -> {
                    PeopleCard("Start a conversation") {
                        Text("Write your first message. They receive only a chat request until they accept; your message stays on this phone.")
                        PeopleComposer(person.key, state.canSubmit, "First message", "Request chat") {
                            onAction(V1PeopleUiAction.RequestChat(person.key, it))
                        }
                    }
                }
                V1DirectContactState.INCOMING_REQUEST_PENDING -> PeopleCard("Chat request") {
                    Text("${person.name} is requesting to chat with you")
                    Text("Message content is hidden until you accept. Accepting does not share your location.")
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { onAction(V1PeopleUiAction.Accept(person.key)) }, enabled = state.canSubmit) { Text("Accept") }
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
                    Row(Modifier.fillMaxWidth()) {
                        Column(Modifier.weight(1f)) {
                            Text("Share location in this chat", style = MaterialTheme.typography.titleMedium)
                            Text(if (state.effectiveLocationSharing) "Location sharing ON" else "Location sharing OFF")
                        }
                        Switch(state.effectiveLocationSharing,
                            { onAction(V1PeopleUiAction.SetLocationSharing(person.key, it)) }, enabled = state.canSubmit)
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
