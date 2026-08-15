package io.github.nbjelanovic.otclient

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot

open class MainActivity : ComponentActivity() {
    private lateinit var bluetoothFacade: AndroidBluetoothFacade
    private lateinit var bluetoothRuntime: BleCompanionRuntime
    private lateinit var lifecycleBinding: TrailAppLifecycleBinding
    private lateinit var appController: TrailAppController

    protected open fun createBluetoothSecurityAuthority(): AndroidBleSecurityAuthority =
        DenyAllAndroidBleSecurityAuthority()

    protected open fun createBluetoothFacade(authority: AndroidBleSecurityAuthority): AndroidBluetoothFacade =
        AndroidBluetoothGattFacade(applicationContext, authority)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bluetoothFacade = createBluetoothFacade(createBluetoothSecurityAuthority())
        bluetoothRuntime = BleCompanionRuntime(
            facade = bluetoothFacade,
            scheduler = AndroidMainThreadBleRuntimeScheduler(),
            threadVerifier = AndroidMainThreadBleRuntimeVerifier(),
        )
        appController = TrailAppController(
            localController = CompanionAppController(FakeCompanionTransport()),
            bluetoothRuntime = bluetoothRuntime,
            permissionReader = AndroidNearbyDevicesPermissionReader(applicationContext),
            bluetoothFacadeCloseable = bluetoothFacade as? AutoCloseable,
        )
        lifecycleBinding = TrailAppLifecycleBinding(lifecycle, appController)

        setContent { TrailTheme { TrailApp(appController) } }
    }

    override fun onResume() {
        super.onResume()
        if (::appController.isInitialized) appController.refreshPermissionState()
    }

    override fun onDestroy() {
        if (::lifecycleBinding.isInitialized) lifecycleBinding.close()
        super.onDestroy()
    }
}

@Composable
fun TrailApp(controller: TrailAppController) {
    var state by remember { mutableStateOf(controller.state) }
    val context = LocalContext.current
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { controller.onNearbyDevicesPermissionResult() }
    DisposableEffect(controller) {
        controller.observe { state = it }
        onDispose { controller.observe(null) }
    }

    val requestNearbyPermissions = {
        if (controller.beginNearbyDevicesPermissionRequest()) {
            permissionLauncher.launch(AndroidBlePermissionContract.runtimePermissions.toTypedArray())
        }
    }
    val openAppSettings = {
        context.startActivity(
            Intent(
                Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.fromParts("package", context.packageName, null),
            ),
        )
    }

    Surface(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text("Limited Underground", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            Text("Trail", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
            when (val current = state) {
                TrailAppUiState.ChooseMode -> ModeChoicePanel(controller)
                is TrailAppUiState.LocalTest -> LocalTestPanel(current.companionState, controller)
                is TrailAppUiState.BluetoothDevice -> BluetoothDevicePanel(
                    state = current,
                    controller = controller,
                    requestNearbyPermissions = requestNearbyPermissions,
                    openAppSettings = openAppSettings,
                )
            }
        }
    }
}

@Composable
private fun ModeChoicePanel(controller: TrailAppController) {
    Text("Choose connection mode", style = MaterialTheme.typography.titleLarge)
    Text("The modes are separate. Bluetooth never falls back to local test data.")
    ModeCard(
        title = "Local test",
        body = "Use two deterministic fake companions. No Bluetooth device or radio is used.",
        button = "Use local test mode",
        select = controller::chooseLocalTestMode,
    )
    ModeCard(
        title = "Bluetooth device",
        body = "Scan for one compatible nearby LoRa companion using the real Android BLE path.",
        button = "Use Bluetooth device mode",
        select = controller::chooseBluetoothDeviceMode,
    )
}

@Composable
private fun ModeCard(title: String, body: String, button: String, select: () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Text(body)
            Button(onClick = select, modifier = Modifier.fillMaxWidth()) { Text(button) }
        }
    }
}

@Composable
private fun LocalTestPanel(state: CompanionUiState, controller: TrailAppController) {
    Text("Local test mode · deterministic fake transport", style = MaterialTheme.typography.bodySmall)
    when (state) {
        is CompanionUiState.Disconnected -> {
            StatusCard(
                "Disconnected",
                "No fake session is active. ${state.candidates.size} deterministic test choices are available.",
            )
            Button(onClick = controller::chooseLocalDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Choose test device")
            }
        }
        is CompanionUiState.Selecting -> {
            Text("Choose one fake device", style = MaterialTheme.typography.titleLarge)
            Text("These entries are local test fixtures and do not scan Bluetooth.")
            CandidateList(
                candidates = state.candidates.map { it.endpointToken to it.publicLabel },
                connect = controller::connectLocalDevice,
            )
            OutlinedButton(onClick = controller::cancelLocalSelection, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel")
            }
        }
        is CompanionUiState.Connecting -> StatusCard("Connecting fake", "Opening ${state.candidate.publicLabel}…")
        is CompanionUiState.Connected -> LocalConnectedPanel(state, controller)
        is CompanionUiState.Failed -> {
            StatusCard("Fake connection unavailable", state.publicReason)
            Button(onClick = controller::retryLocalSelection, modifier = Modifier.fillMaxWidth()) {
                Text("Choose again")
            }
        }
    }
    OutlinedButton(onClick = controller::returnToModeChoice, modifier = Modifier.fillMaxWidth()) {
        Text("Leave local test mode")
    }
}

@Composable
private fun LocalConnectedPanel(state: CompanionUiState.Connected, controller: TrailAppController) {
    StatusCard("Connected to fake ${state.connection.publicLabel}", state.connection.status)
    val snapshot = state.connection.snapshot
    SnapshotSummary("Fake device status", snapshot)
    ActionControls(controller::submitLocalAction)
    if (snapshot.pendingCriticalAlertId != 0uL) {
        AcknowledgeAlertButton(snapshot.pendingCriticalAlertId, controller::submitLocalAction)
    }
    state.lastActionResult?.let { StatusCard("Last fake action", it.fakePublicOutcome()) }
    state.publicNotice?.let { StatusCard("Fake action unavailable", it) }
    OutlinedButton(onClick = controller::disconnectLocalDevice, modifier = Modifier.fillMaxWidth()) {
        Text("Disconnect fake device")
    }
}

@Composable
private fun BluetoothDevicePanel(
    state: TrailAppUiState.BluetoothDevice,
    controller: TrailAppController,
    requestNearbyPermissions: () -> Unit,
    openAppSettings: () -> Unit,
) {
    Text("Bluetooth device mode · no local fallback", style = MaterialTheme.typography.bodySmall)
    when {
        state.permissionState == NearbyDevicesPermissionState.UNSUPPORTED -> StatusCard(
            "Bluetooth mode unavailable",
            "The real companion path requires Android 12 or newer.",
        )
        state.permissionRequestInFlight -> StatusCard(
            "Nearby Devices permission",
            "Waiting for Android's permission decision. No scan has started.",
        )
        state.permissionState == NearbyDevicesPermissionState.MISSING -> {
            StatusCard(
                if (state.permissionWasDenied) "Nearby Devices permission denied" else "Nearby Devices permission required",
                "Bluetooth scan and connection stay disabled until both Nearby Devices permissions are granted.",
            )
            Button(onClick = requestNearbyPermissions, modifier = Modifier.fillMaxWidth()) {
                Text(if (state.permissionWasDenied) "Request permission again" else "Allow Nearby Devices")
            }
            if (state.permissionWasDenied) {
                OutlinedButton(onClick = openAppSettings, modifier = Modifier.fillMaxWidth()) {
                    Text("Open app permission settings")
                }
            }
        }
        else -> BluetoothRuntimePanel(state.runtimeState, controller, requestNearbyPermissions)
    }
    OutlinedButton(onClick = controller::returnToModeChoice, modifier = Modifier.fillMaxWidth()) {
        Text("Disconnect and change mode")
    }
}

@Composable
private fun BluetoothRuntimePanel(
    state: BleRuntimeState,
    controller: TrailAppController,
    requestNearbyPermissions: () -> Unit,
) {
    when (state) {
        BleRuntimeState.Inactive -> StatusCard(
            "Bluetooth paused",
            "The app lifecycle is stopped. No scan or connection lease is retained.",
        )
        BleRuntimeState.Idle -> {
            StatusCard("Bluetooth disconnected", "No Bluetooth companion session is active.")
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Scan for compatible devices")
            }
        }
        is BleRuntimeState.Blocked -> {
            StatusCard("Bluetooth unavailable", state.reason.publicText())
            if (
                state.reason == BleRuntimeBlock.SCAN_PERMISSION_MISSING ||
                state.reason == BleRuntimeBlock.CONNECT_PERMISSION_MISSING
            ) {
                Button(onClick = requestNearbyPermissions, modifier = Modifier.fillMaxWidth()) {
                    Text("Allow Nearby Devices")
                }
            }
        }
        is BleRuntimeState.Scanning -> {
            StatusCard("Scanning", "Only devices advertising the accepted companion service are listed.")
            if (state.candidates.isEmpty()) Text("No compatible device found yet.")
            CandidateList(
                candidates = state.candidates.map { it.endpointToken to it.publicLabel },
                connect = controller::selectBluetoothDevice,
            )
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop scan")
            }
        }
        is BleRuntimeState.Connecting -> {
            StatusCard("Connecting", "Opening ${state.companion.publicLabel}.")
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel connection")
            }
        }
        is BleRuntimeState.Negotiating -> {
            StatusCard("Securing connection", state.phase.publicText())
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel connection")
            }
        }
        is BleRuntimeState.Ready -> BluetoothReadyPanel(state.session, controller)
        is BleRuntimeState.Reconnecting -> {
            StatusCard(
                "Reconnecting",
                "Attempt ${state.attempt} of ${state.maximumAttempts} for ${state.companion.publicLabel}.",
            )
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop reconnecting")
            }
        }
        is BleRuntimeState.Failed -> {
            StatusCard("Bluetooth connection unavailable", state.reason.publicText())
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Scan again")
            }
        }
        BleRuntimeState.Closed -> StatusCard("Bluetooth closed", "This app session cannot open another Bluetooth lease.")
    }
}

@Composable
private fun BluetoothReadyPanel(session: BleActiveSession, controller: TrailAppController) {
    StatusCard(
        "Connected to ${session.companion.publicLabel}",
        "Authenticated companion session. Device state remains authoritative.",
    )
    SnapshotSummary("Device status", session.snapshot)
    ActionControls { controller.submitBluetoothAction(it) }
    if (session.snapshot.pendingCriticalAlertId != 0uL) {
        AcknowledgeAlertButton(session.snapshot.pendingCriticalAlertId) { controller.submitBluetoothAction(it) }
    }
    session.lastActionResult?.let { StatusCard("Last device result", it.bluetoothPublicOutcome()) }
    OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
        Text("Disconnect Bluetooth device")
    }
}

@Composable
private fun CandidateList(candidates: List<Pair<String, String>>, connect: (String) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
        candidates.forEach { (endpointToken, publicLabel) ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(modifier = Modifier.fillMaxWidth().padding(16.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text(publicLabel, modifier = Modifier.weight(1f))
                    Button(onClick = { connect(endpointToken) }) { Text("Connect") }
                }
            }
        }
    }
}

@Composable
private fun SnapshotSummary(title: String, snapshot: CompanionStatusSnapshot) {
    Text(
        "$title · radio ${snapshot.radio.publicLabel()} · GNSS ${snapshot.gnss.publicLabel()} · " +
            "power ${snapshot.power.publicLabel()} · position ${snapshot.positionSharing.publicLabel()} · " +
            "queued ${snapshot.queuedActionCount}",
        style = MaterialTheme.typography.bodyMedium,
    )
}

@Composable
private fun ActionControls(submit: (CompanionActionRequest) -> Unit) {
    Text("Quick status", style = MaterialTheme.typography.titleMedium)
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        ActionButton("OK", Modifier.weight(1f)) {
            submit(CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.OK))
        }
        ActionButton("Need help", Modifier.weight(1f)) {
            submit(CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.NEED_ASSISTANCE))
        }
    }
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        ActionButton("Anyone online?", Modifier.weight(1f)) {
            submit(CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.ANYONE_ONLINE))
        }
        ActionButton("Available", Modifier.weight(1f)) {
            submit(CompanionActionRequest(CompanionActionKind.QUICK_STATUS, CompanionQuickStatus.AVAILABLE_TO_HELP))
        }
    }
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(
            onClick = { submit(CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING)) },
            modifier = Modifier.weight(1f),
        ) { Text("Start position") }
        OutlinedButton(
            onClick = { submit(CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING)) },
            modifier = Modifier.weight(1f),
        ) { Text("Stop position") }
    }
}

@Composable
private fun AcknowledgeAlertButton(alertId: ULong, submit: (CompanionActionRequest) -> Unit) {
    Button(
        onClick = {
            submit(
                CompanionActionRequest(
                    kind = CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT,
                    criticalAlertId = alertId,
                ),
            )
        },
        modifier = Modifier.fillMaxWidth(),
    ) { Text("Acknowledge pending critical alert") }
}

@Composable
private fun ActionButton(label: String, modifier: Modifier, action: () -> Unit) {
    Button(onClick = action, modifier = modifier) { Text(label) }
}

private fun CompanionActionResult.fakePublicOutcome(): String = when (disposition) {
    CompanionActionDisposition.QUEUED -> "Queued in the fake device. This is not sent or delivered evidence."
    CompanionActionDisposition.ADMITTED -> "Applied in deterministic test state."
    CompanionActionDisposition.REJECTED -> "The fake device rejected the request: ${rejectReason.publicLabel()}."
}

private fun CompanionActionResult.bluetoothPublicOutcome(): String = when (disposition) {
    CompanionActionDisposition.QUEUED -> "Accepted into the device queue. This is not sent or delivered evidence."
    CompanionActionDisposition.ADMITTED -> "Applied locally by the device."
    CompanionActionDisposition.REJECTED -> "The device rejected the request: ${rejectReason.publicLabel()}."
}

private fun BleRuntimeBlock.publicText(): String = when (this) {
    BleRuntimeBlock.ADAPTER_IMPLEMENTATION_MISSING -> "The Bluetooth adapter is unavailable in this build."
    BleRuntimeBlock.PLATFORM_UNSUPPORTED -> "The real Bluetooth path requires Android 12 or newer."
    BleRuntimeBlock.BLUETOOTH_UNAVAILABLE -> "This phone does not expose the required Bluetooth Low Energy adapter."
    BleRuntimeBlock.BLUETOOTH_DISABLED -> "Turn on Bluetooth, then scan again."
    BleRuntimeBlock.SCAN_PERMISSION_MISSING,
    BleRuntimeBlock.CONNECT_PERMISSION_MISSING,
    -> "Nearby Devices permission is required before Bluetooth can continue."
}

private fun BleRuntimeFailure.publicText(): String = when (this) {
    BleRuntimeFailure.SCAN_START_FAILED -> "The Bluetooth scan could not start. No local test data was substituted."
    BleRuntimeFailure.CONNECTION_START_FAILED -> "The selected companion connection ended or could not start."
    BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED -> "The companion did not meet the required security checks."
    BleRuntimeFailure.MTU_NEGOTIATION_FAILED -> "The companion could not negotiate the required message size."
    BleRuntimeFailure.PROTOCOL_INFO_FAILED -> "The companion protocol is incompatible or unavailable."
    BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED -> "The authoritative device update stream could not be enabled."
    BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED -> "The initial authoritative device status was not accepted."
    BleRuntimeFailure.NEGOTIATION_TIMEOUT -> "The companion did not complete setup in time."
    BleRuntimeFailure.PROTOCOL_VIOLATION -> "The companion sent an invalid or unexpected protocol message."
    BleRuntimeFailure.RECONNECT_EXHAUSTED -> "The bounded reconnect attempts were exhausted."
    BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED -> "The session counter was exhausted; reconnect before retrying."
    BleRuntimeFailure.ACTION_WRITE_FAILED -> "The action was not accepted for Bluetooth write."
    BleRuntimeFailure.ACTION_RESULT_TIMEOUT -> "The device did not return an action result in time."
}

private fun BleNegotiationPhase.publicText(): String = when (this) {
    BleNegotiationPhase.LINK_SECURITY -> "Checking the encrypted, authenticated, application-authorized link."
    BleNegotiationPhase.ATT_MTU -> "Negotiating the required Bluetooth message size."
    BleNegotiationPhase.PROTOCOL_INFO -> "Checking the companion protocol and capabilities."
    BleNegotiationPhase.STREAM_SUBSCRIPTION -> "Enabling authoritative device indications."
    BleNegotiationPhase.INITIAL_SNAPSHOT -> "Waiting for the initial authoritative device status."
}

private fun Enum<*>.publicLabel(): String = name.lowercase().replace('_', ' ')

@Composable
private fun StatusCard(title: String, body: String) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
            Text(body, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@Composable
private fun TrailTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = androidx.compose.material3.darkColorScheme(
            primary = Color(0xFF9DFF8B),
            background = Color(0xFF07100B),
            surface = Color(0xFF102019),
        ),
        content = content,
    )
}
