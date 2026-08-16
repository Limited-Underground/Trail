package io.github.nbjelanovic.otclient

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
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
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import java.util.Locale

open class MainActivity : ComponentActivity() {
    private lateinit var lifecycleBinding: TrailAppLifecycleBinding
    private lateinit var appController: TrailActivityController

    protected open fun createConnectedDeviceServiceConnector(): ConnectedDeviceServiceConnector =
        AndroidConnectedDeviceServiceConnector(applicationContext)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        appController = TrailActivityController(
            localController = CompanionAppController(FakeCompanionTransport()),
            permissionReader = AndroidNearbyDevicesPermissionReader(applicationContext),
            notificationPermissionReader = AndroidNotificationPermissionReader(applicationContext),
            serviceConnector = createConnectedDeviceServiceConnector(),
            serviceStartupScheduler = AndroidMainThreadBleRuntimeScheduler(),
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
fun TrailApp(controller: TrailUiController) {
    var state by remember { mutableStateOf(controller.state) }
    val context = LocalContext.current
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { controller.onNearbyDevicesPermissionResult() }
    val notificationPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { controller.refreshPermissionState() }
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
    val requestNotificationPermission = {
        if (Build.VERSION.SDK_INT >= 33) {
            notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        } else {
            controller.refreshPermissionState()
        }
    }

    Surface(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Image(
                painter = painterResource(R.drawable.limited_underground_trail),
                contentDescription = null,
                modifier = Modifier.fillMaxWidth().aspectRatio(2f),
                contentScale = ContentScale.Fit,
            )
            Text("Limited Underground", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            Text("Trail", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
            when (val current = state) {
                TrailAppUiState.ChooseMode -> ModeChoicePanel(controller)
                is TrailAppUiState.LocalTest -> LocalTestPanel(current.companionState, controller)
                is TrailAppUiState.BluetoothDevice -> BluetoothDevicePanel(
                    state = current,
                    controller = controller,
                    requestNearbyPermissions = requestNearbyPermissions,
                    requestNotificationPermission = requestNotificationPermission,
                    openAppSettings = openAppSettings,
                )
            }
        }
    }
}

@Composable
private fun ModeChoicePanel(controller: TrailUiController) {
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
private fun LocalTestPanel(state: CompanionUiState, controller: TrailUiController) {
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
private fun LocalConnectedPanel(state: CompanionUiState.Connected, controller: TrailUiController) {
    StatusCard("Connected to fake ${state.connection.publicLabel}", state.connection.status)
    val snapshot = state.connection.snapshot
    SnapshotSummary("Fake device status", snapshot)
    GroupLocationSection(state.connection.groupLocation)
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
    controller: TrailUiController,
    requestNearbyPermissions: () -> Unit,
    requestNotificationPermission: () -> Unit,
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
        state.serviceState == ConnectedDeviceServiceUiState.START_REQUIRED -> {
            StatusCard(
                "Bluetooth service ready to start",
                "Start the connected-device service from this visible screen. It owns at most one Bluetooth session " +
                    "while running; you or Android may stop it, and no device connection or claim starts automatically.",
            )
            if (state.notificationPermissionState == NotificationPermissionState.DENIED) {
                StatusCard(
                    "Notification visibility is reduced",
                    "Android can still run the foreground service and shows its disclosure in Task Manager, but " +
                        "the service notification may not appear in the notification drawer.",
                )
                OutlinedButton(onClick = requestNotificationPermission, modifier = Modifier.fillMaxWidth()) {
                    Text("Allow service notifications")
                }
            }
            Button(onClick = controller::startBluetoothService, modifier = Modifier.fillMaxWidth()) {
                Text("Start Bluetooth device service")
            }
        }
        state.serviceState == ConnectedDeviceServiceUiState.STARTING -> StatusCard(
            "Starting Bluetooth service",
            "Android is starting the user-requested connected-device service. No device claim is retried automatically.",
        )
        state.serviceState == ConnectedDeviceServiceUiState.START_FAILED -> {
            StatusCard("Bluetooth service unavailable", state.serviceFailure.publicText())
            Button(onClick = controller::startBluetoothService, modifier = Modifier.fillMaxWidth()) {
                Text("Try starting service again")
            }
        }
        else -> {
            if (state.notificationPermissionState == NotificationPermissionState.DENIED) {
                StatusCard(
                    "Service running · reduced notification visibility",
                    "Notification permission is not granted. Android still discloses the foreground service in Task " +
                        "Manager, but its notification may be absent from the notification drawer.",
                )
                OutlinedButton(onClick = requestNotificationPermission, modifier = Modifier.fillMaxWidth()) {
                    Text("Allow service notifications")
                }
            }
            BluetoothAuthorizedRuntimePanel(state, controller, requestNearbyPermissions)
        }
    }
    OutlinedButton(onClick = controller::returnToModeChoice, modifier = Modifier.fillMaxWidth()) {
        Text("Disconnect and change mode")
    }
}

@Composable
private fun BluetoothAuthorizedRuntimePanel(
    state: TrailAppUiState.BluetoothDevice,
    controller: TrailUiController,
    requestNearbyPermissions: () -> Unit,
) {
    when (val authorization = state.authorizationState) {
        DeviceAuthorizationUiState.None -> BluetoothRuntimePanel(
            state.runtimeState,
            controller,
            requestNearbyPermissions,
        )
        is DeviceAuthorizationUiState.Starting -> {
            val purpose = authorization.purpose
            StatusCard(
                if (purpose == DeviceAuthorizationPurpose.REPLACE_LOST_PHONE) {
                    "Preparing replacement request"
                } else {
                    "Preparing authorization request"
                },
                "The app is preparing the encrypted, authenticated device path. Do not press the physical " +
                    "authorization control yet; the 30-second device window has not started.",
            )
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel request")
            }
        }
        is DeviceAuthorizationUiState.Pending -> {
            val purpose = authorization.purpose
            StatusCard(
                if (purpose == DeviceAuthorizationPurpose.REPLACE_LOST_PHONE) {
                    "Replacement request waiting for the device"
                } else {
                    "Authorization request waiting for the device"
                },
                "On the physical device, press its authorization control within 30 seconds. " +
                    "The device alone decides. This request is not proof that a control was pressed, " +
                    "a Bluetooth bond exists, or phone authority changed.",
            )
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel request")
            }
        }
        is DeviceAuthorizationUiState.Accepted -> {
            StatusCard(
                "Device reported request accepted",
                "The exact protected device response reported this one claim accepted. Normal use still awaits the " +
                    "exact initial device snapshot/session gate; this host-tested build is not physical-device evidence.",
            )
            BluetoothRuntimePanel(state.runtimeState, controller, requestNearbyPermissions)
        }
        is DeviceAuthorizationUiState.Replaced -> {
            StatusCard(
                "Device reported prior phone replaced",
                "The exact protected device response reported replacement for this claim. The phone did not grant or " +
                    "revoke authority; normal use still awaits the exact initial snapshot/session gate, and host tests " +
                    "are not physical proof.",
            )
            BluetoothRuntimePanel(state.runtimeState, controller, requestNearbyPermissions)
        }
        is DeviceAuthorizationUiState.Denied -> AuthorizationEndedPanel(
            "Device reported request denied",
            "The authoritative denial ended this request; no normal authorized companion session was established.",
            controller,
        )
        is DeviceAuthorizationUiState.InvalidResult -> AuthorizationEndedPanel(
            "Authorization result rejected",
            INVALID_AUTHORIZATION_RESULT_PUBLIC_TEXT,
            controller,
        )
        is DeviceAuthorizationUiState.Expired -> AuthorizationEndedPanel(
            "Authorization request expired",
            EXPIRED_AUTHORIZATION_PUBLIC_TEXT,
            controller,
        )
        is DeviceAuthorizationUiState.Unavailable -> AuthorizationEndedPanel(
            "Authorization request unavailable",
            "No authorization claim or result is available. No normal authorized companion session was established, " +
                "and this app did not establish device authority.",
            controller,
        )
        is DeviceAuthorizationUiState.Unsupported -> AuthorizationEndedPanel(
            "Authorization not supported",
            "This device did not expose the accepted protected authorization contract. The app did not " +
                "fall back to local test mode or establish device authority.",
            controller,
        )
        is DeviceAuthorizationUiState.AuthorityUnknown -> AuthorizationEndedPanel(
            "Authorization result unknown",
            "The protected connection ended after the device reported the request pending. Device authority " +
                "may have changed; reconnect and check the physical device before retrying.",
            controller,
        )
    }
}

@Composable
private fun AuthorizationEndedPanel(
    title: String,
    body: String,
    controller: TrailUiController,
) {
    StatusCard(title, body)
    Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
        Text("Scan again")
    }
}

@Composable
private fun BluetoothRuntimePanel(
    state: BleRuntimeState,
    controller: TrailUiController,
    requestNearbyPermissions: () -> Unit,
) {
    when (state) {
        BleRuntimeState.Inactive -> StatusCard(
            "Bluetooth paused",
            "The app lifecycle is stopped. No scan or connection lease is retained.",
        )
        BleRuntimeState.Idle -> {
            StatusCard("Bluetooth disconnected", "No Bluetooth companion session is active.")
            LostPhoneGuidance()
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
            LostPhoneGuidance()
            if (state.candidates.isEmpty()) Text("No compatible device found yet.")
            BluetoothCandidateList(state.candidates, controller)
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop scan")
            }
        }
        is BleRuntimeState.AwaitingAuthorization -> StatusCard(
            "Waiting for device authority",
            "The selected device has not yet returned an authoritative claim result.",
        )
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
private fun LostPhoneGuidance() {
    StatusCard(
        "Lost or replaced phone",
        "A lost phone cannot be removed by this phone alone. Choose Replace lost phone, then use the " +
            "physical device authorization or reset control. The device decides. Replacement is designed not " +
            "to erase radio configuration, but this host build does not verify radio continuity.",
    )
}

@Composable
private fun BluetoothCandidateList(
    candidates: List<BleDiscoveredCompanion>,
    controller: TrailUiController,
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
        candidates.forEach { candidate ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(candidate.publicLabel)
                    Button(
                        onClick = { controller.selectBluetoothDevice(candidate.endpointToken) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Authorize this phone") }
                    OutlinedButton(
                        onClick = { controller.replaceLostPhoneWithBluetoothDevice(candidate.endpointToken) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Replace lost phone") }
                }
            }
        }
    }
}

@Composable
private fun BluetoothReadyPanel(session: BleActiveSession, controller: TrailUiController) {
    StatusCard(
        "Connected to ${session.companion.publicLabel}",
        "Authenticated companion session. Device state remains authoritative.",
    )
    SnapshotSummary("Device status", session.snapshot)
    GroupLocationSection(session.groupLocation)
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
private fun GroupLocationSection(snapshot: GroupLocationSnapshot) {
    var expanded by remember { mutableStateOf(false) }
    val isFake = snapshot.provenance == GroupLocationProvenance.LOCAL_TEST_FIXTURE
    val sourceLabel = if (isFake) "Fake fixture" else "Device-reported"
    OutlinedButton(
        onClick = { expanded = !expanded },
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(if (expanded) "Close Group / Location" else "Open Group / Location")
    }
    if (!expanded) return

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                "Group / Location",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                when (snapshot.provenance) {
                    GroupLocationProvenance.LOCAL_TEST_FIXTURE ->
                        "Deterministic fake location fixture. This is not phone GPS, device, radio, or map evidence."
                    GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT ->
                        "Device-authoritative snapshot only. This app does not substitute phone GPS."
                },
            )
            Text("$sourceLabel position sharing: ${snapshot.sharingState.publicLabel()}.")
            GroupPositionCard("This device", snapshot.selfPosition, snapshot.provenance)
            Text(
                "Group peers (${snapshot.peers.size})",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() },
            )
            if (snapshot.peers.isEmpty()) {
                Text("No peer positions or public display aliases were supplied in this snapshot.")
            } else {
                snapshot.peers.forEach { peer ->
                    GroupPositionCard(peer.displayAlias.value, peer.position, snapshot.provenance)
                }
            }
            Text("Map rendering and offline map packages are deferred; this screen uses no network tiles.")
        }
    }
}

@Composable
private fun GroupPositionCard(
    label: String,
    position: GroupLocationPosition,
    provenance: GroupLocationProvenance,
) {
    val isFake = provenance == GroupLocationProvenance.LOCAL_TEST_FIXTURE
    val sourceLabel = if (isFake) "Fake fixture" else "Device-reported"
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(if (isFake) "Fake $label" else label, style = MaterialTheme.typography.titleMedium)
            when (position.state) {
                GroupPositionState.UNAVAILABLE -> Text(
                    "$sourceLabel position unavailable. No coordinates, age, or accuracy were supplied.",
                )
                GroupPositionState.CURRENT,
                GroupPositionState.STALE,
                -> {
                    val coordinate = requireNotNull(position.coordinate)
                    Text("$sourceLabel position ${position.state.publicLabel()}.")
                    Text(
                        "$sourceLabel latitude ${coordinate.latitudeDegrees().fixedCoordinate()}, " +
                            "longitude ${coordinate.longitudeDegrees().fixedCoordinate()}.",
                    )
                    Text("$sourceLabel age: ${position.ageSeconds} seconds.")
                    Text(
                        position.accuracyMeters?.let { "$sourceLabel accuracy: $it meters." }
                            ?: "$sourceLabel accuracy: not supplied.",
                    )
                }
            }
        }
    }
}

private fun Double.fixedCoordinate(): String = String.format(Locale.US, "%.5f", this)

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
    BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST ->
        "The authorization connection ended. Device authority may have changed; reconnect and check the device."
    BleRuntimeFailure.AUTHORIZATION_UNSUPPORTED ->
        "The selected device does not expose the accepted protected authorization contract."
    BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE ->
        "The protected authorization path is unavailable. This app did not establish device authority."
}

private fun ConnectedDeviceServiceStartFailure?.publicText(): String = when (this) {
    ConnectedDeviceServiceStartFailure.NOT_VISIBLE_USER_ACTION ->
        "Start the Bluetooth service again from the visible app screen."
    ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING ->
        "Nearby Devices permission was not available when the service start was requested."
    ConnectedDeviceServiceStartFailure.PLATFORM_REJECTED ->
        "Android did not allow the foreground service to start from the current app state."
    ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE,
    null,
    -> "The connected-device service did not become available. No local test data was substituted."
}

private fun BleNegotiationPhase.publicText(): String = when (this) {
    BleNegotiationPhase.LINK_SECURITY -> "Checking the encrypted, authenticated, application-authorized link."
    BleNegotiationPhase.ATT_MTU -> "Negotiating the required Bluetooth message size."
    BleNegotiationPhase.PROTOCOL_INFO -> "Checking the companion protocol and capabilities."
    BleNegotiationPhase.STREAM_SUBSCRIPTION -> "Enabling authoritative device indications."
    BleNegotiationPhase.AUTHORIZATION_CLAIM -> "Waiting for an exact device authorization result."
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
