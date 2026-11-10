package io.github.nbjelanovic.otclient

import android.os.Build
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.focus.FocusDirection
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.*
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

/** The real entry shell. Missing transport capabilities remain visibly unavailable. */
@Composable
internal fun V1HomeScreen(
    state: TrailAppUiState,
    liveRuntime: () -> BleRuntimeState?,
    onRefreshGroupConfirmation: () -> Boolean = { false },
    onConfirmGroupConfirmation: (V1GroupConfirmationOffer) -> Boolean = { false },
    onCancelGroupConfirmation: (V1GroupConfirmationOffer) -> Boolean = { false },
    additionalDeviceTools: @Composable () -> Unit = {},
    onSubmitQuickStatus: (V1QuickStatusRecipient, io.github.nbjelanovic.otprotocol.CompanionQuickStatus) -> Boolean = { _, _ -> false },
    deviceContent: @Composable (onSetupComplete: (V1OnboardingScope) -> Unit) -> Unit,
) {
    val launchSaver=remember { Saver<V1HomeLaunchState,Any>(
        save={ it.save() },restore={ V1HomeLaunchState.restore(it) }) }
    var navigation by rememberSaveable(stateSaver=launchSaver) {
        mutableStateOf(V1HomeLaunchState.initial(state))
    }
    val currentRuntime by rememberUpdatedState(liveRuntime)
    var splashFinished by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(Unit) { delay(1_800); splashFinished = true }
    if (!splashFinished) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Image(painterResource(R.drawable.limited_underground_trail),
                contentDescription = "Limited Underground Trail",
                modifier = Modifier.fillMaxWidth().padding(24.dp))
        }
        return
    }
    val destination = navigation.destination
    val retainedPages = rememberSaveableStateHolder()
    val bluetooth = state as? TrailAppUiState.BluetoothDevice
    val runtime = bluetooth?.runtimeState
    val readySession = (runtime as? BleRuntimeState.Ready)?.session
    val connected = readySession != null
    val confirmation = if (connected) bluetooth?.groupConfirmation ?: V1GroupConfirmationState.Unsupported
        else V1GroupConfirmationState.Unsupported
    val refreshGroupConfirmation by rememberUpdatedState(onRefreshGroupConfirmation)
    LaunchedEffect(destination, readySession?.companion?.endpointToken, readySession?.sessionNonce) {
        if (destination == V1AppDestination.GROUP && readySession != null) {
            refreshGroupConfirmation()
        }
    }
    val status = when (runtime) {
        is BleRuntimeState.Ready -> "Device connected"
        is BleRuntimeState.Reconnecting, BleRuntimeState.FindingReturningOwner -> "Reconnecting to device"
        is BleRuntimeState.Connecting, is BleRuntimeState.Negotiating -> "Connecting to device"
        is BleRuntimeState.AwaitingAuthorization -> "Authorize your phone"
        else -> "No device connected"
    }
    V1AppShell(
        state = V1AppNavigationState.empty().select(destination),
        onDestinationSelected = { navigation = navigation.select(it) },
        messages = { retainedPages.SaveableStateProvider("messages") {
            V1MessagesScreen(status, onConnect = { navigation = navigation.beginSetup(currentRuntime()) },
                quickStatus = bluetooth?.quickStatus ?: V1QuickStatusState(), onSubmitQuickStatus = onSubmitQuickStatus)
        } },
        group = { retainedPages.SaveableStateProvider("group") {
            V1GroupScreen(
                V1GroupScreenState(
                    statusMessage = when {
                        !connected -> "Connect your device to manage a group. V1 group setup requires the upcoming device update."
                        confirmation == V1GroupConfirmationState.Unsupported ->
                            "This device's current firmware does not yet support V1 group setup."
                        else -> "Review the nearby peer before joining."
                    },
                    confirmation = confirmation,
                ),
                onAction = { action ->
                    when (action) {
                        V1GroupUiAction.RefreshDeviceOffer -> if (connected) onRefreshGroupConfirmation()
                        is V1GroupUiAction.ConfirmDeviceOffer -> if (connected) onConfirmGroupConfirmation(action.offer)
                        is V1GroupUiAction.CancelDeviceOffer -> if (connected) onCancelGroupConfirmation(action.offer)
                        else -> Unit
                    }
                },
            )
        } },
        device = { retainedPages.SaveableStateProvider("device") {
            var support by rememberSaveable { mutableStateOf(false) }
            var setupDraft by rememberSaveable { mutableStateOf(false) }
            // Keep the report draft while Support is temporarily replaced by Device.
            val retainedDevicePages = rememberSaveableStateHolder()
            androidx.activity.compose.BackHandler(enabled = support) { support = false }
            androidx.activity.compose.BackHandler(enabled = setupDraft) { setupDraft = false }
            if (setupDraft) {
                retainedDevicePages.SaveableStateProvider("setup-draft") {
                    V1SetupDraftScreen(onBack = { setupDraft = false })
                }
            } else if (support) {
                Column(Modifier.fillMaxSize()) {
                    TextButton(onClick = { support = false }) { Text("Back to Device") }
                    retainedDevicePages.SaveableStateProvider("support") {
                        V1SupportScreen(v1SupportSnapshot(state))
                    }
                }
            } else {
                Column(Modifier.fillMaxSize()) {
                    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Text("Device", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.weight(1f))
                        TextButton(onClick = { support = true }) { Text("Help & support") }
                        additionalDeviceTools()
                    }
                    Box(Modifier.weight(1f)) { deviceContent { scope ->
                        navigation = navigation.finish(scope,currentRuntime)
                    } }
                    TextButton(onClick = { setupDraft = true }) { Text("Prepare setup choices") }
                }
            }
        } },
    )
}

@Composable
internal fun V1MessagesScreen(connectionStatus: String, onConnect: () -> Unit,
    initiallyDirect: Boolean = false, initiallyEditing: Boolean = false,
    quickStatus: V1QuickStatusState = V1QuickStatusState(),
    onSubmitQuickStatus: (V1QuickStatusRecipient, io.github.nbjelanovic.otprotocol.CompanionQuickStatus) -> Boolean = { _, _ -> false }) {
    val context = LocalContext.current
    var submissionNotice by remember(quickStatus.recipient) { mutableStateOf<String?>(null) }
    fun chooseStatus(value: String) {
        val status = V1QuickStatusComposer.fromTemplate(value)
        val recipient = quickStatus.recipient
        submissionNotice = when {
            status == null -> V1QuickStatusComposer.TEXT_UNAVAILABLE
            recipient == null -> V1QuickStatusComposer.AUTHORITY_UNAVAILABLE
            onSubmitQuickStatus(recipient, status) -> "Request submitted; delivery is not confirmed."
            else -> "Request refused or outcome uncertain. Check device evidence; do not resend automatically."
        }
    }
    val preferences = remember { context.getSharedPreferences("v1-local-templates", 0) }
    val loadedTemplates = remember {
        V1QuickMessages.load { index ->
            check(index in (0 until V1QuickMessages.MAX_CUSTOM))
            preferences.getString("template-$index", null)?.let(V1QuickMessages::normalize)
        }
    }
    var templates by remember {
        mutableStateOf(loadedTemplates.orEmpty())
    }
    var saveFailed by rememberSaveable { mutableStateOf(false) }
    fun persist(value: List<String>): Boolean {
        if (loadedTemplates == null) return false
        // Only this dedicated preferences file is replaced; no connection/group state is touched.
        val saved = V1QuickMessages.save(value) { snapshot ->
            preferences.edit().clear().also { editor ->
                snapshot.forEachIndexed { index, text -> editor.putString("template-$index", text) }
            }.commit()
        }
        saveFailed = !saved
        if (saved) templates = value
        return saved
    }
    var direct by rememberSaveable { mutableStateOf(initiallyDirect) }
    var editing by rememberSaveable { mutableStateOf(initiallyEditing) }
    var text by rememberSaveable { mutableStateOf("") }
    var editingIndex by rememberSaveable { mutableStateOf<Int?>(null) }
    var editingOriginal by rememberSaveable { mutableStateOf<String?>(null) }
    var editText by rememberSaveable { mutableStateOf("") }
    var nearby by rememberSaveable { mutableStateOf(false) }
    androidx.activity.compose.BackHandler(enabled = nearby) { nearby = false }
    if (nearby) {
        V1PeopleScreen(V1PeopleScreenState(
            statusMessage = "Nearby people requires the upcoming device update. No live discovery is running."),
            onAction = { if (it == V1PeopleUiAction.Back) nearby = false })
        return
    }
    val focusManager = LocalFocusManager.current
    val fieldFocus = remember { FocusRequester() }
    val saveFocus = remember { FocusRequester() }
    val cancelFocus = remember { FocusRequester() }
    val index = editingIndex
    val formText = if (index == null) text else editText
    fun candidate(): List<String>? {
        if (editingIndex != index) return null
        return if (index == null) V1QuickMessages.add(templates, text)
            else editingOriginal?.let { V1QuickMessages.edit(templates, index, it, editText) }
    }
    val canSave = loadedTemplates != null && candidate() != null
    // Keep actions outside the scrolling content so the IME cannot push them below it.
    Column(Modifier.fillMaxSize()) {
    Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text("Messages", style = MaterialTheme.typography.headlineMedium)
        TextButton(onClick = onConnect) { Text(connectionStatus) }
        OutlinedButton(onClick = { nearby = true }) { Text("Nearby people") }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = !direct, onClick = { direct = false }, label = { Text("Group") })
            FilterChip(selected = direct, onClick = { direct = true }, label = { Text("Direct") })
        }
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(if (direct) "No conversations yet" else "No group conversation", style = MaterialTheme.typography.titleLarge)
                Text(if (direct) "Discoverable people will appear here. A chat request must be accepted before you can exchange messages."
                    else "Create or join a group from the Group tab when group setup is available.")
                if (direct) {
                    OutlinedButton(onClick = { nearby = true }) { Text("Find nearby people") }
                    Text("Person discovery requires the upcoming device update.", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("Quick messages", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
            TextButton(onClick = { if (editingIndex == null) editing = !editing },
                enabled = loadedTemplates != null && editingIndex == null) {
                Text(if (editing) "Done" else "Customize")
            }
        }
        Text("Fixed quick statuses use the current authenticated pair only. Templates remain local until explicitly selected.", style = MaterialTheme.typography.bodySmall)
        if (quickStatus.recipient == null) Text(V1QuickStatusComposer.AUTHORITY_UNAVAILABLE)
        submissionNotice?.let { Text(it) }
        if (quickStatus.recipient != null) {
            quickStatus.received?.let { Text("Received status: " + V1QuickStatusComposer.label(it)) }
            quickStatus.outbound?.let { Text(V1QuickStatusComposer.evidence(it)) }
        }
        if (loadedTemplates == null) {
            Text("Saved quick messages could not be read. Editing is unavailable.")
        } else if (saveFailed) {
            Text("Save could not be confirmed. Your draft is still here; saved state is uncertain.")
        }
        val choices = if (editing) V1QuickMessages.builtIn else
            (V1QuickMessages.builtIn + io.github.nbjelanovic.otprotocol.CompanionQuickStatus.entries.map(V1QuickStatusComposer::label)).distinct()
        choices.forEach { value ->
            if (editing) Text(value) else V1QuickStatusChoice(value, quickStatus.recipient != null) { chooseStatus(value) }
        }
        templates.forEachIndexed { index, template ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Text(template)
                    if (!editing) V1QuickStatusChoice(template, quickStatus.recipient != null) { chooseStatus(template) }
                    if (editing) {
                        TextButton(onClick = {
                            if (editingIndex == null && templates.getOrNull(index) == template) {
                                editingIndex = index
                                editingOriginal = template
                                editText = template
                            }
                        }, enabled = loadedTemplates != null && editingIndex == null,
                            modifier = Modifier.semantics { contentDescription = "Edit quick message ${index + 1}" }) {
                            Text("Edit")
                        }
                        Row {
                            TextButton(onClick = {
                                if (editingIndex == null && templates.getOrNull(index) == template)
                                    persist(V1QuickMessages.move(templates, index, -1))
                            },
                                enabled = loadedTemplates != null && editingIndex == null && index > 0,
                                modifier = Modifier.semantics { contentDescription = "Move quick message ${index + 1} up" }) { Text("Up") }
                            TextButton(onClick = {
                                if (editingIndex == null && templates.getOrNull(index) == template)
                                    persist(V1QuickMessages.move(templates, index, 1))
                            },
                                enabled = loadedTemplates != null && editingIndex == null && index < templates.lastIndex,
                                modifier = Modifier.semantics { contentDescription = "Move quick message ${index + 1} down" }) { Text("Down") }
                            TextButton(onClick = {
                                if (editingIndex == null && templates.getOrNull(index) == template)
                                    persist(templates.filterIndexed { i, _ -> i != index })
                            },
                                enabled = loadedTemplates != null && editingIndex == null,
                                modifier = Modifier.semantics { contentDescription = "Remove quick message ${index + 1}" }) { Text("Remove") }
                        }
                    }
                }
            }
        }
        if (editing) {
            val staleEdit = index != null && (index !in templates.indices || templates[index] != editingOriginal)
            OutlinedTextField(value = formText, onValueChange = {
                if (it.length <= V1QuickMessages.MAX_CHARS) {
                    if (index == null) text = it else editText = it
                }
            }, enabled = loadedTemplates != null,
                label = { Text(if (index == null) "Your quick message" else "Edit quick message") },
                isError = formText.isNotEmpty() && candidate() == null,
                // Templates reject control characters. Tab is form navigation here, not text,
                // while the field still wraps long messages and keeps normal editing shortcuts.
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                keyboardActions = KeyboardActions(onNext = { focusManager.moveFocus(FocusDirection.Next) }),
                modifier = Modifier.fillMaxWidth().focusRequester(fieldFocus)
                    .focusProperties {
                        next = if (canSave) saveFocus else if (index != null) cancelFocus else FocusRequester.Default
                    }
                    .onPreviewKeyEvent { event ->
                        if (event.key == Key.Tab && !event.isCtrlPressed && !event.isAltPressed && !event.isMetaPressed) {
                            if (event.type == KeyEventType.KeyDown) {
                                focusManager.moveFocus(if (event.isShiftPressed) FocusDirection.Previous else FocusDirection.Next)
                            }
                            true
                        } else false
                    }, supportingText = {
                    Text(when {
                        staleEdit -> "This quick message changed. Cancel the edit before choosing it again."
                        formText.isNotEmpty() && candidate() == null -> "Use a unique message of 1–160 characters without control characters."
                        else -> "${formText.length}/160 · ${templates.size}/12 custom messages"
                    })
                })
        }
    }
    if (editing) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(modifier = Modifier.weight(1f).heightIn(min = 48.dp).focusRequester(saveFocus)
                .focusProperties { previous = fieldFocus; next = if (index != null) cancelFocus else FocusRequester.Default },
                onClick = {
                candidate()?.let { value ->
                    if (persist(value)) {
                        if (index == null) text = "" else {
                            editingIndex = null
                            editingOriginal = null
                            editText = ""
                        }
                    }
                }
            }, enabled = canSave) {
                Text(if (index == null) "Save quick message" else "Save changes")
            }
            if (index != null) {
                OutlinedButton(modifier = Modifier.weight(1f).heightIn(min = 48.dp).focusRequester(cancelFocus)
                    .focusProperties { previous = if (canSave) saveFocus else fieldFocus },
                    onClick = {
                    focusManager.clearFocus()
                    editingIndex = null
                    editingOriginal = null
                    editText = ""
                }) { Text("Cancel edit") }
            }
        }
    }
    }
}

@Composable
private fun v1SupportSnapshot(state: TrailAppUiState): V1SafeDiagnosticSnapshot {
    val context = LocalContext.current
    fun label(value: String) = V1PublicDiagnosticLabel.create(value) ?: V1PublicDiagnosticLabel.create("Not available")!!
    val version = remember { context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: "Not available" }
    val health = V1SupportHealthProjector.from(state)
    return V1SafeDiagnosticSnapshot(label(version), label("Not available"), label(Build.MODEL),
        label(Build.VERSION.RELEASE), label("Not available"), V1SupportRegionProjector.from(state), health.connection,
        health.radio, health.gps, health.power, null, health.lastCode)
}

/** Unknown phrases stay editable but can never silently acquire fixed-status semantics. */
@Composable
private fun V1QuickStatusChoice(text: String, currentRecipient: Boolean, submit: () -> Unit) {
    val mapped = V1QuickStatusComposer.fromTemplate(text) != null
    OutlinedButton(onClick = submit, enabled = mapped && currentRecipient,
        modifier = Modifier.heightIn(min = 48.dp)) { Text("Send: $text") }
    if (!mapped) Text(V1QuickStatusComposer.TEXT_UNAVAILABLE, style = MaterialTheme.typography.bodySmall)
}
