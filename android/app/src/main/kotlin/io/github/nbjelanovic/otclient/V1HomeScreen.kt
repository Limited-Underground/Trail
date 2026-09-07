package io.github.nbjelanovic.otclient

import android.os.Build
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

/** The real entry shell. Missing transport capabilities remain visibly unavailable. */
@Composable
internal fun V1HomeScreen(
    state: TrailAppUiState,
    deviceContent: @Composable () -> Unit,
) {
    // Freeze the launch choice before the splash; runtime changes must not navigate for the user.
    var selected by rememberSaveable {
        mutableStateOf(v1InitialHomeDestination(state).name)
    }
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
    val destination = V1AppDestination.entries.firstOrNull { it.name == selected } ?: V1AppDestination.MESSAGES
    val retainedPages = rememberSaveableStateHolder()
    val runtime = (state as? TrailAppUiState.BluetoothDevice)?.runtimeState
    val connected = runtime is BleRuntimeState.Ready
    val status = when (runtime) {
        is BleRuntimeState.Ready -> "Device connected"
        is BleRuntimeState.Reconnecting, BleRuntimeState.FindingReturningOwner -> "Reconnecting to device"
        is BleRuntimeState.Connecting, is BleRuntimeState.Negotiating -> "Connecting to device"
        is BleRuntimeState.AwaitingAuthorization -> "Authorize your phone"
        else -> "No device connected"
    }
    V1AppShell(
        state = V1AppNavigationState.empty().select(destination),
        onDestinationSelected = { selected = it.name },
        messages = { retainedPages.SaveableStateProvider("messages") {
            V1MessagesScreen(status, onConnect = { selected = V1AppDestination.DEVICE.name })
        } },
        group = { retainedPages.SaveableStateProvider("group") {
            V1GroupScreen(V1GroupScreenState(statusMessage = if (connected)
                "This device's current firmware does not yet support V1 group setup."
                else "Connect your device to manage a group. V1 group setup requires the upcoming device update."), onAction = {})
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
                    }
                    Box(Modifier.weight(1f)) { deviceContent() }
                    TextButton(onClick = { setupDraft = true }) { Text("Prepare setup choices") }
                }
            }
        } },
    )
}

@Composable
internal fun V1MessagesScreen(connectionStatus: String, onConnect: () -> Unit,
    initiallyDirect: Boolean = false, initiallyEditing: Boolean = false) {
    val context = LocalContext.current
    val preferences = remember { context.getSharedPreferences("v1-local-templates", 0) }
    var templates by remember {
        mutableStateOf((0 until V1QuickMessages.MAX_CUSTOM).mapNotNull { index ->
            preferences.getString("template-$index", null)?.let(V1QuickMessages::normalize)
        }.distinct())
    }
    fun persist(value: List<String>) {
        // Only this dedicated preferences file is replaced; no connection/group state is touched.
        preferences.edit().clear().also { editor ->
            value.forEachIndexed { index, text -> editor.putString("template-$index", text) }
        }.apply()
        templates = value
    }
    var direct by rememberSaveable { mutableStateOf(initiallyDirect) }
    var editing by rememberSaveable { mutableStateOf(initiallyEditing) }
    var text by rememberSaveable { mutableStateOf("") }
    var nearby by rememberSaveable { mutableStateOf(false) }
    androidx.activity.compose.BackHandler(enabled = nearby) { nearby = false }
    if (nearby) {
        V1PeopleScreen(V1PeopleScreenState(
            statusMessage = "Nearby people requires the upcoming device update. No live discovery is running."),
            onAction = { if (it == V1PeopleUiAction.Back) nearby = false })
        return
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
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
            TextButton(onClick = { editing = !editing }) { Text(if (editing) "Done" else "Customize") }
        }
        Text("Prepare your messages here. Nothing is sent from this screen.", style = MaterialTheme.typography.bodySmall)
        V1QuickMessages.builtIn.forEach { Text(it) }
        templates.forEachIndexed { index, template ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Text(template)
                    if (editing) {
                        Row {
                            TextButton(onClick = { persist(V1QuickMessages.move(templates, index, -1)) }, enabled = index > 0) { Text("Up") }
                            TextButton(onClick = { persist(V1QuickMessages.move(templates, index, 1)) }, enabled = index < templates.lastIndex) { Text("Down") }
                            TextButton(onClick = { persist(templates.filterIndexed { i, _ -> i != index }) }) { Text("Remove") }
                        }
                    }
                }
            }
        }
        if (editing) {
            OutlinedTextField(value = text, onValueChange = { if (it.length <= V1QuickMessages.MAX_CHARS) text = it },
                label = { Text("Your quick message") }, modifier = Modifier.fillMaxWidth(),
                supportingText = { Text("${text.length}/160 · ${templates.size}/12 custom messages") })
            Button(onClick = { V1QuickMessages.add(templates, text)?.let { persist(it); text = "" } },
                enabled = V1QuickMessages.add(templates, text) != null) { Text("Save quick message") }
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
        label(Build.VERSION.RELEASE), label("Not available"), null, health.connection,
        health.radio, health.gps, health.power, null, health.lastCode)
}
