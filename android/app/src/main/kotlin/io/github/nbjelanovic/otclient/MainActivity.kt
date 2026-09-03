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
import io.github.nbjelanovic.otprotocol.COMPANION_FACTORY_RESET_CAPABILITY
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

private data class DeviceSettingsAuthority(
    val endpointToken: String,
    val sessionNonce: Long,
)

@Composable
fun TrailApp(controller: TrailUiController) {
    var state by remember { mutableStateOf(controller.state) }
    var deviceSettingsAuthority by remember { mutableStateOf<DeviceSettingsAuthority?>(null) }
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
    val openBluetoothSettings = {
        context.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
    }
    val bluetooth = state as? TrailAppUiState.BluetoothDevice
    val readySession = (bluetooth?.runtimeState as? BleRuntimeState.Ready)?.session
    val settingsSession = readySession?.takeIf { session ->
        deviceSettingsAuthority?.let { authority ->
            authority.endpointToken == session.companion.endpointToken &&
                authority.sessionNonce == session.sessionNonce
        } == true
    }

    Surface(modifier = Modifier.fillMaxSize()) {
        if (bluetooth?.factoryResetConfirmationVisible == true) {
            FactoryResetConfirmationScreen(controller)
            return@Surface
        }
        if (settingsSession != null) {
            DeviceSettingsScreen(
                session = settingsSession,
                controller = controller,
                onBack = { deviceSettingsAuthority = null },
            )
            return@Surface
        }
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
                    openBluetoothSettings = openBluetoothSettings,
                    openDeviceSettings = { session ->
                        deviceSettingsAuthority = DeviceSettingsAuthority(
                            session.companion.endpointToken,
                            session.sessionNonce,
                        )
                    },
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
    openBluetoothSettings: () -> Unit,
    openDeviceSettings: (BleActiveSession) -> Unit,
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
            BluetoothAuthorizedRuntimePanel(
                state,
                controller,
                requestNearbyPermissions,
                openBluetoothSettings,
                openDeviceSettings,
            )
        }
    }
    if (!state.runtimeState.isFactoryResetResolutionState()) {
        OutlinedButton(onClick = controller::returnToModeChoice, modifier = Modifier.fillMaxWidth()) {
            Text("Disconnect and change mode")
        }
    }
}

@Composable
private fun BluetoothAuthorizedRuntimePanel(
    state: TrailAppUiState.BluetoothDevice,
    controller: TrailUiController,
    requestNearbyPermissions: () -> Unit,
    openBluetoothSettings: () -> Unit,
    openDeviceSettings: (BleActiveSession) -> Unit,
) {
    when (val authorization = state.authorizationState) {
        DeviceAuthorizationUiState.None -> BluetoothRuntimePanel(
            state.runtimeState,
            controller,
            requestNearbyPermissions,
            openBluetoothSettings,
            openDeviceSettings,
        )
        is DeviceAuthorizationUiState.Starting -> {
            val purpose = authorization.purpose
            val instructions = androidSystemPairingInstructions(purpose)
            StatusCard(
                instructions.startingTitle,
                instructions.startingBody,
            )
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel request")
            }
        }
        is DeviceAuthorizationUiState.Pending -> {
            val purpose = authorization.purpose
            val instructions = androidSystemPairingInstructions(purpose)
            StatusCard(
                instructions.pendingTitle,
                instructions.pendingBody,
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
            BluetoothRuntimePanel(
                state.runtimeState,
                controller,
                requestNearbyPermissions,
                openBluetoothSettings,
                openDeviceSettings,
            )
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
    openBluetoothSettings: () -> Unit,
    openDeviceSettings: (BleActiveSession) -> Unit,
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
            StatusCard(
                "Scanning",
                "Only devices in their active unowned pairing window are listed.",
            )
            LostPhoneGuidance()
            if (state.candidates.isEmpty()) Text("No compatible device found yet.")
            BluetoothCandidateList(state.candidates, controller)
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop scan")
            }
        }
        is BleRuntimeState.ScanComplete -> {
            StatusCard(
                "Scan complete",
                if (state.candidates.isEmpty()) {
                    "No compatible Trail device was found. Restart an unowned or reset device if its PIN is not showing, then scan again."
                } else {
                    "The scan ended. Choose a compatible device found during this scan, or scan again."
                },
            )
            LostPhoneGuidance()
            BluetoothCandidateList(state.candidates, controller)
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Scan again")
            }
        }
        BleRuntimeState.FindingReturningOwner -> {
            StatusCard(
                "Checking for authorized device",
                "Looking for exactly one nearby device already authorized by this phone. No PIN or new pairing is requested.",
            )
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Add a new device instead")
            }
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop reconnecting")
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
        is BleRuntimeState.Ready -> BluetoothReadyPanel(state.session, controller, openDeviceSettings)
        is BleRuntimeState.FactoryResetRequesting -> StatusCard(
            "Reset request sent",
            "Waiting for the exact protected device response. Nothing has been reported as erased or complete.",
        )
        is BleRuntimeState.FactoryResetErasing -> StatusCard(
            "Reset accepted · completion not verified",
            "The device accepted the durable reset intent. Keep Bluetooth on while it erases data and restarts.",
        )
        is BleRuntimeState.FactoryResetVerifying -> StatusCard(
            "Verifying reset",
            "Waiting for this exact device to restart and advertise its verified unowned state. Other nearby devices do not count.",
        )
        is BleRuntimeState.FactoryResetNotVerified -> {
            StatusCard("Reset not verified", state.reason.factoryResetPublicText())
            if (state.canRetryVerification) {
                Button(
                    onClick = { controller.retryFactoryResetVerification() },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Check this device again") }
            } else {
                OutlinedButton(
                    onClick = controller::disconnectBluetoothDevice,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Return to Bluetooth") }
            }
        }
        is BleRuntimeState.FactoryResetComplete -> {
            StatusCard(
                "Factory reset verified",
                "Local session state cleared. Please navigate to Android Bluetooth Settings to manually remove the system device bond.",
            )
            if (state.systemBondRemovalRequired) {
                StatusCard(
                    "Remove old Android pairing",
                    "Android still lists the old Bluetooth bond. Open Bluetooth settings and forget the old Trail pairing before pairing this reset device again.",
                )
                Button(onClick = openBluetoothSettings, modifier = Modifier.fillMaxWidth()) {
                    Text("Open Bluetooth settings")
                }
            } else {
                Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                    Text("Add a Trail device")
                }
            }
        }
        is BleRuntimeState.Reconnecting -> {
            val body = if (state.connectionDiagnostic == null) {
                "Attempt ${state.attempt} of ${state.maximumAttempts} for ${state.companion.publicLabel}."
            } else {
                "Attempt ${state.attempt} of ${state.maximumAttempts} for ${state.companion.publicLabel}.\n" +
                    "Support code: ${state.connectionDiagnostic.supportCode()}"
            }
            StatusCard(
                "Reconnecting",
                body,
            )
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop reconnecting")
            }
        }
        is BleRuntimeState.Failed -> {
            val body = if (state.connectionDiagnostic == null) {
                state.reason.publicText()
            } else {
                "${state.reason.publicText()}\nSupport code: ${state.connectionDiagnostic.supportCode()}"
            }
            StatusCard("Bluetooth connection unavailable", body)
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
        "Lost authorized phone",
        LOST_PHONE_RECOVERY_PUBLIC_TEXT,
    )
}

@Composable
private fun BluetoothCandidateList(
    candidates: List<BleDiscoveredCompanion>,
    controller: TrailUiController,
) {
    val initialInstructions = androidSystemPairingInstructions(
        DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE,
    )
    Column(verticalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
        candidates.forEach { candidate ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(candidate.publicLabel)
                    Text(initialInstructions.beforeActionTitle, style = MaterialTheme.typography.titleSmall)
                    Text(initialInstructions.beforeActionBody, style = MaterialTheme.typography.bodySmall)
                    Button(
                        onClick = { controller.selectBluetoothDevice(candidate.endpointToken) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Authorize this phone") }
                }
            }
        }
    }
}

internal const val LOST_PHONE_RECOVERY_PUBLIC_TEXT =
    "A replacement phone cannot take over an owned device. On the device, hold the physical control for at " +
        "least 10 seconds, release when prompted, then short-press within 10 seconds to confirm. The physical " +
        "factory reset erases all Trail user data. After the verified reset reboots the device unowned, its " +
        "six-digit PIN and pairable window open automatically for 60 seconds; authorize the new phone before " +
        "the window closes."

internal const val APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT =
    "This permanently erases the paired phone, device name, settings, groups, messages, saved locations, and offline maps. " +
        "Firmware and factory hardware data remain. This cannot be undone."

internal data class AndroidSystemPairingInstructions(
    val beforeActionTitle: String,
    val beforeActionBody: String,
    val startingTitle: String,
    val startingBody: String,
    val pendingTitle: String,
    val pendingBody: String,
)

internal fun androidSystemPairingInstructions(
    purpose: DeviceAuthorizationPurpose,
): AndroidSystemPairingInstructions = when (purpose) {
    DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE -> AndroidSystemPairingInstructions(
        beforeActionTitle = "Pair a new phone",
        beforeActionBody =
            "Restart the unowned or just-reset device if its six-digit PIN is not showing. The PIN and pairable " +
                "window open automatically for 60 seconds after unowned boot. Then tap Authorize this phone and " +
                "enter the displayed six digits only in Android's system pairing dialog. Trail never receives " +
                "or stores the code.",
        startingTitle = "Android system pairing starting",
        startingBody =
            "Keep the automatically opened 60-second PIN window active. If Android asks for a code, enter the " +
                "six digits shown on the device only in Android's system pairing dialog. If the PIN is no " +
                "longer showing, cancel, restart the unowned device, and retry. Trail never receives or stores " +
                "the code.",
        pendingTitle = "Secure pairing completed",
        pendingBody =
            "The device is completing initial phone authorization. Initial authorization does not require a " +
                "second physical confirmation.",
    )
}

@Composable
private fun BluetoothReadyPanel(
    session: BleActiveSession,
    controller: TrailUiController,
    openDeviceSettings: (BleActiveSession) -> Unit,
) {
    StatusCard(
        "Connected to ${session.companion.publicLabel}",
        "Authenticated companion session. Device state remains authoritative.",
    )
    SnapshotSummary("Device status", session.snapshot)
    GroupLocationSection(session.groupLocation)
    ActionControls { controller.submitBluetoothAction(it) }
    OutlinedButton(
        onClick = { openDeviceSettings(session) },
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text("Device settings")
    }
    if (session.snapshot.pendingCriticalAlertId != 0uL) {
        AcknowledgeAlertButton(session.snapshot.pendingCriticalAlertId) { controller.submitBluetoothAction(it) }
    }
    session.lastActionResult?.let { StatusCard("Last device result", it.bluetoothPublicOutcome()) }
    OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
        Text("Disconnect Bluetooth device")
    }
}

@Composable
private fun DeviceSettingsScreen(
    session: BleActiveSession,
    controller: TrailUiController,
    onBack: () -> Unit,
) {
    val supportsFactoryReset =
        (session.protocolInfo.capabilities and COMPANION_FACTORY_RESET_CAPABILITY) != 0
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Limited Underground", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            Text("Device settings", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
            StatusCard(
                "Connected device",
                "Settings apply only to ${session.companion.publicLabel} in this authenticated session.",
            )
            StatusCard(
                "Factory reset",
                if (supportsFactoryReset) {
                    APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT
                } else {
                    "This connected firmware does not expose the protected factory-reset capability."
                },
            )
        }
        Column(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
            if (supportsFactoryReset) {
                Button(
                    onClick = { controller.requestFactoryResetConfirmation() },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Review factory reset")
                }
            }
            OutlinedButton(onClick = onBack, modifier = Modifier.fillMaxWidth()) {
                Text("Back to device")
            }
        }
    }
}

@Composable
private fun FactoryResetConfirmationScreen(controller: TrailUiController) {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Limited Underground", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            Text("Erase all Trail data?", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
            Text(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT)
            StatusCard(
                "Keep the device nearby",
                "After you confirm, Trail waits for the protected acceptance and then verifies this exact device restarted unowned. A disconnect or timeout is not shown as success.",
            )
        }
        Column(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
            Button(onClick = { controller.confirmFactoryReset() }, modifier = Modifier.fillMaxWidth()) {
                Text("Erase all Trail data")
            }
            OutlinedButton(onClick = controller::cancelFactoryResetConfirmation, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel")
            }
        }
    }
}

private fun BleRuntimeState.isFactoryResetResolutionState(): Boolean =
    this is BleRuntimeState.FactoryResetRequesting ||
        this is BleRuntimeState.FactoryResetErasing ||
        this is BleRuntimeState.FactoryResetVerifying ||
        this is BleRuntimeState.FactoryResetNotVerified

private fun FactoryResetNotVerifiedReason.factoryResetPublicText(): String = when (this) {
    FactoryResetNotVerifiedReason.REQUEST_WRITE_UNCERTAIN ->
        "Android could not verify that the protected reset command was accepted. The saved device record was kept."
    FactoryResetNotVerifiedReason.REQUEST_REJECTED ->
        "The device rejected the reset request. No reset completion is claimed, and the saved device record was kept."
    FactoryResetNotVerifiedReason.RESPONSE_TIMEOUT ->
        "The protected reset response timed out. The result is unknown, not successful, and the saved device record was kept."
    FactoryResetNotVerifiedReason.CONNECTION_LOST_BEFORE_ACCEPTANCE ->
        "The connection ended before reset acceptance was proven. The result is unknown, not successful, and the saved device record was kept."
    FactoryResetNotVerifiedReason.MALFORMED_RESPONSE ->
        "The reset response was invalid. No success is claimed, and the saved device record was kept."
    FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE ->
        "Trail could not verify this exact device after restart. No success is claimed, and the saved device record was kept."
    FactoryResetNotVerifiedReason.VERIFICATION_TIMEOUT ->
        "This exact device did not show its verified unowned state before the check ended. No success is claimed, and the saved device record was kept."
    FactoryResetNotVerifiedReason.WRONG_DEVICE_OBSERVED ->
        "The observed device did not match the reset target. It was ignored, no success is claimed, and the saved device record was kept."
    FactoryResetNotVerifiedReason.LOCAL_RECORD_CLEAR_FAILED ->
        "The reset device was observed unowned, but Trail could not safely remove its saved record. Completion is not claimed."
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
    BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS ->
        "More than one bonded Trail device answered. Reconnect stopped without choosing or storing an identity."
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

internal fun BleConnectionDiagnostic.supportCode(): String = when (this) {
    BleConnectionDiagnostic.LEASE_UNAVAILABLE -> "BLE-LEASE-UNAVAILABLE"
    BleConnectionDiagnostic.START_REJECTED -> "BLE-OPEN-REJECTED"
    BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE -> "BLE-DISCONNECTED-BEFORE-PROFILE"
    BleConnectionDiagnostic.GATT_TRANSIENT_LINK -> "BLE-GATT-TRANSIENT-LINK"
    BleConnectionDiagnostic.GATT_PERMISSION_REVOKED -> "BLE-GATT-PERMISSION-REVOKED"
    BleConnectionDiagnostic.GATT_SECURITY_REJECTED -> "BLE-GATT-SECURITY-REJECTED"
    BleConnectionDiagnostic.GATT_BOND_REQUIRED -> "BLE-GATT-BOND-REQUIRED"
    BleConnectionDiagnostic.GATT_AUTHORIZATION_REJECTED -> "BLE-GATT-AUTHORIZATION-REJECTED"
    BleConnectionDiagnostic.GATT_PLATFORM_FAILURE -> "BLE-GATT-PLATFORM-FAILURE"
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
