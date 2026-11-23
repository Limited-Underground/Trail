package io.github.nbjelanovic.otclient

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.provider.Settings
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivity
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
import androidx.compose.foundation.layout.imePadding
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
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionActionResult
import io.github.nbjelanovic.otprotocol.CompanionQuickStatus
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import io.github.nbjelanovic.otprotocol.COMPANION_FACTORY_RESET_CAPABILITY
import java.util.Locale

open class MainActivity : ComponentActivity() {
    private lateinit var lifecycleBinding: TrailAppLifecycleBinding
    private lateinit var appController: TrailActivityController
    private var inviteQr: ZxingInviteQr? = null
    private val inviteScan = registerForActivityResult(com.journeyapps.barcodescanner.ScanContract()) { result ->
        inviteQr?.delivered(result.contents)
    }

    protected open fun createConnectedDeviceServiceConnector(): ConnectedDeviceServiceConnector =
        AndroidConnectedDeviceServiceConnector(applicationContext)

    @Composable
    protected open fun AdditionalTools() = Unit

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        configureTrailFullscreen(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val operationLog = MeshTrailOperationLogging.from(applicationContext)
        appController = TrailActivityController(
            localController = createActivityLocalController(),
            permissionReader = AndroidNearbyDevicesPermissionReader(applicationContext),
            notificationPermissionReader = AndroidNotificationPermissionReader(applicationContext),
            serviceConnector = createConnectedDeviceServiceConnector(),
            serviceStartupScheduler = AndroidMainThreadBleRuntimeScheduler(),
            bluetoothRetryRefusalObserver = operationLog::recordBluetoothRetryRefusal,
        )
        lifecycleBinding = TrailAppLifecycleBinding(lifecycle, appController)

        operationLog.appOpened()
        inviteQr = ZxingInviteQr { inviteScan.launch(it) }.also { V1InviteQrBinding.current = it }
        setContent { TrailTheme { TrailApp(appController) { AdditionalTools() } } }
    }

    override fun onResume() {
        super.onResume()
        if (::appController.isInitialized) appController.refreshPermissionState()
    }

    override fun onDestroy() {
        if (::lifecycleBinding.isInitialized) lifecycleBinding.close()
        if (V1InviteQrBinding.current === inviteQr) V1InviteQrBinding.current = null
        inviteQr = null
        super.onDestroy()
    }
}

private data class DeviceSettingsAuthority(
    val endpointToken: String,
    val sessionNonce: Long,
)

internal enum class NearbyPermissionDenial {
    RETRYABLE,
    SETTINGS_ONLY,
}

internal fun nearbyPermissionDenial(
    result: Map<String, Boolean>,
    isGranted: (String) -> Boolean,
    shouldShowRationale: (String) -> Boolean?,
): NearbyPermissionDenial? {
    // A false rationale alone also describes a permission that has never been requested.
    val denied = AndroidBlePermissionContract.runtimePermissions.filter {
        result[it] == false && !isGranted(it)
    }
    if (denied.isEmpty()) return null
    return if (denied.any { shouldShowRationale(it) == false }) {
        NearbyPermissionDenial.SETTINGS_ONLY
    } else {
        NearbyPermissionDenial.RETRYABLE
    }
}

@Composable
fun TrailApp(controller: TrailUiController, additionalTools: @Composable () -> Unit = {}) {
    var state by remember { mutableStateOf(controller.state) }
    val homeStateHolder = androidx.compose.runtime.saveable.rememberSaveableStateHolder()
    var deviceSettingsAuthority by remember { mutableStateOf<DeviceSettingsAuthority?>(null) }
    val context = LocalContext.current
    val activity = LocalActivity.current
    val pairingCodes = rememberBoardPairingCodeRetention()
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val displayPreferences = remember(context) { AppDisplayPreferences(context) }
    var showBuildLabel by remember { mutableStateOf(displayPreferences.showBuildLabel()) }
    var displaySettingSaveFailed by remember { mutableStateOf(false) }
    var nearbyDenial by rememberSaveable { mutableStateOf<NearbyPermissionDenial?>(null) }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { result ->
        nearbyDenial = nearbyPermissionDenial(
            result,
            isGranted = { context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED },
            shouldShowRationale = { activity?.shouldShowRequestPermissionRationale(it) },
        )
        controller.onNearbyDevicesPermissionResult()
    }
    val notificationPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { controller.refreshPermissionState() }
    DisposableEffect(controller, pairingCodes) {
        val logObservation = MeshTrailOperationLogging.from(context).appObservation()
        controller.observe { next ->
            logObservation.observeApp(next)
            pairingCodes.observe(next, SystemClock.elapsedRealtime())
            state = next
        }
        onDispose { controller.observe(null) }
    }
    ExpireBoardPairingCode(pairingCodes)

    val requestNearbyPermissions = {
        if (controller.beginNearbyDevicesPermissionRequest()) {
            permissionLauncher.launch(AndroidBlePermissionContract.runtimePermissions.toTypedArray())
        }
    }
    val findBluetoothDevice = {
        controller.chooseBluetoothDeviceMode()
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
    LaunchedEffect(bluetooth?.permissionState) {
        if (bluetooth?.permissionState == NearbyDevicesPermissionState.GRANTED) nearbyDenial = null
    }
    val readySession = (bluetooth?.runtimeState as? BleRuntimeState.Ready)?.session
    val settingsSession = readySession?.takeIf { session ->
        deviceSettingsAuthority?.let { authority ->
            authority.endpointToken == session.companion.endpointToken &&
                authority.sessionNonce == session.sessionNonce
        } == true
    }

    Column(modifier = Modifier.fillMaxSize().imePadding()) {
      V1StatusStrip(state, showBuildLabel = showBuildLabel)
      if (!landscape) additionalTools()
      androidx.compose.foundation.layout.Box(modifier = Modifier.weight(1f)) {
      Surface(modifier = Modifier.fillMaxSize()) {
        if (bluetooth?.factoryResetConfirmationVisible == true) {
            FactoryResetConfirmationScreen(readySession, controller)
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
        homeStateHolder.SaveableStateProvider("home") {
        val appSettings: @Composable () -> Unit = {
            AppDisplaySettings(showBuildLabel, { show ->
                val saved = displayPreferences.setShowBuildLabel(show)
                displaySettingSaveFailed = !saved
                if (saved) showBuildLabel = show
            }, displaySettingSaveFailed)
        }
        V1HomeScreen(
            state = state,
            liveRuntime = { (controller.state as? TrailAppUiState.BluetoothDevice)?.runtimeState },
            onSubmitQuickStatus = controller::submitQuickStatus,
            onSubmitText = controller::submitText,
            onBoardGroupAction = controller::submitBoardGroupAction,
            onRefreshGroupConfirmation = controller::refreshGroupConfirmation,
            onConfirmGroupConfirmation = controller::confirmGroupConfirmation,
            onCancelGroupConfirmation = controller::cancelGroupConfirmation,
            additionalDeviceTools = { if (landscape) additionalTools() },
        ) { onSetupComplete, onSetUpLater ->
          if (state == TrailAppUiState.ChooseMode) {
            V1OnboardingScreen(
                state = V1OnboardingScreenState(statusMessage =
                    "Connect to see your device name and radio region."),
                onAction = { action ->
                    if (action == V1OnboardingUiAction.Back) onSetUpLater()
                },
                footer = appSettings,
                showStepNumber = false,
                connectionContent = {
                    Button(onClick = findBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                        Text("Find device")
                    }
                },
            )
          } else {
          Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
          ) {
            when (val current = state) {
                TrailAppUiState.ChooseMode -> {
                    Text("Connect your MeshTrail device", style = MaterialTheme.typography.titleLarge)
                    Text("Turn on your device and keep it nearby.\nAllow Nearby devices access when Android asks.")
                    Button(onClick = findBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                        Text("Connect device")
                    }
                }
                is TrailAppUiState.LocalTest -> VariantLocalConnection(current.companionState, controller)
                is TrailAppUiState.BluetoothDevice -> BluetoothDevicePanel(
                    state = current,
                    controller = controller,
                    requestNearbyPermissions = requestNearbyPermissions,
                    nearbyDenial = nearbyDenial,
                    requestNotificationPermission = requestNotificationPermission,
                    openAppSettings = openAppSettings,
                    openBluetoothSettings = openBluetoothSettings,
                    onSetupComplete = onSetupComplete,
                    openDeviceSettings = { session ->
                        deviceSettingsAuthority = DeviceSettingsAuthority(
                            session.companion.endpointToken,
                            session.sessionNonce,
                        )
                    },
                )
            }
            appSettings()
          }
          }
        }
    }
    }
}

    }
}

@Composable
private fun BluetoothDevicePanel(
    state: TrailAppUiState.BluetoothDevice,
    controller: TrailUiController,
    requestNearbyPermissions: () -> Unit,
    nearbyDenial: NearbyPermissionDenial?,
    requestNotificationPermission: () -> Unit,
    openAppSettings: () -> Unit,
    openBluetoothSettings: () -> Unit,
    openDeviceSettings: (BleActiveSession) -> Unit,
    onSetupComplete: (V1OnboardingScope) -> Unit,
) {
    Text("Bluetooth", style = MaterialTheme.typography.bodySmall)
    when {
        state.permissionState == NearbyDevicesPermissionState.UNSUPPORTED -> StatusCard(
            "Bluetooth unavailable",
            "Bluetooth needs Android 12 or newer.",
        )
        state.permissionRequestInFlight -> StatusCard(
            "Nearby Devices permission",
            "Allow Nearby devices access in the Android prompt.",
        )
        state.permissionState == NearbyDevicesPermissionState.MISSING -> {
            val settingsOnly = nearbyDenial == NearbyPermissionDenial.SETTINGS_ONLY
            val denied = nearbyDenial != null || state.permissionWasDenied
            StatusCard(
                if (denied) "Nearby devices access is off" else "Allow nearby devices",
                if (settingsOnly) "Open app settings, tap Permissions, and allow Nearby devices."
                else "MeshTrail needs Nearby devices access to find your device.",
            )
            if (!settingsOnly) {
                Button(onClick = requestNearbyPermissions, modifier = Modifier.fillMaxWidth()) {
                    Text(if (denied) "Ask again" else "Allow Nearby Devices")
                }
            }
            OutlinedButton(onClick = openAppSettings, modifier = Modifier.fillMaxWidth()) {
                Text("Open app settings")
            }
        }
        state.serviceState == ConnectedDeviceServiceUiState.START_REQUIRED -> {
            StatusCard(
                "Stay connected",
                "Let MeshTrail stay connected while you use other apps.",
            )
            if (state.notificationPermissionState == NotificationPermissionState.DENIED) {
                StatusCard(
                    "Notifications are off",
                    "Allow notifications to see when MeshTrail is running.",
                )
                OutlinedButton(onClick = requestNotificationPermission, modifier = Modifier.fillMaxWidth()) {
                    Text("Allow notifications")
                }
            }
            Button(onClick = controller::startBluetoothService, modifier = Modifier.fillMaxWidth()) {
                Text("Turn on background connection")
            }
        }
        state.serviceState == ConnectedDeviceServiceUiState.STARTING -> StatusCard(
            "Turning on Bluetooth connection",
            "MeshTrail is getting Bluetooth ready.",
        )
        state.serviceState == ConnectedDeviceServiceUiState.START_FAILED -> {
            StatusCard("Couldn't start Bluetooth connection", state.serviceFailure.publicText())
            Button(onClick = controller::startBluetoothService, modifier = Modifier.fillMaxWidth()) {
                Text("Try again")
            }
        }
        else -> {
            if (state.serviceState == ConnectedDeviceServiceUiState.RUNNING &&
                state.notificationPermissionState == NotificationPermissionState.DENIED
            ) {
                StatusCard(
                    "Bluetooth notifications are off",
                    "Allow notifications to see when MeshTrail is running.",
                )
                OutlinedButton(onClick = requestNotificationPermission, modifier = Modifier.fillMaxWidth()) {
                    Text("Allow notifications")
                }
            }
            BluetoothAuthorizedRuntimePanel(
                state,
                controller,
                requestNearbyPermissions,
                openAppSettings,
                openBluetoothSettings,
                openDeviceSettings,
                onSetupComplete,
            )
        }
    }
}

@Composable
private fun BluetoothAuthorizedRuntimePanel(
    state: TrailAppUiState.BluetoothDevice,
    controller: TrailUiController,
    requestNearbyPermissions: () -> Unit,
    openAppSettings: () -> Unit,
    openBluetoothSettings: () -> Unit,
    openDeviceSettings: (BleActiveSession) -> Unit,
    onSetupComplete: (V1OnboardingScope) -> Unit,
) {
    when (val authorization = state.authorizationState) {
        DeviceAuthorizationUiState.None -> BluetoothRuntimePanel(
            state.runtimeState,
            controller,
            requestNearbyPermissions,
            openAppSettings,
            openBluetoothSettings,
            openDeviceSettings,
            onSetupComplete,
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
                "Phone approved",
                "Your device approved this phone.",
            )
            BluetoothRuntimePanel(
                state = state.runtimeState,
                controller = controller,
                requestNearbyPermissions = requestNearbyPermissions,
                openAppSettings = openAppSettings,
                openBluetoothSettings = openBluetoothSettings,
                openDeviceSettings = openDeviceSettings,
                onSetupComplete = onSetupComplete,
            )
        }
        is DeviceAuthorizationUiState.Denied -> AuthorizationEndedPanel(
            "Phone refused",
            "Your device refused to connect to this phone.",
            controller,
        )
        is DeviceAuthorizationUiState.InvalidResult -> AuthorizationEndedPanel(
            "Device reply not accepted",
            INVALID_AUTHORIZATION_RESULT_PUBLIC_TEXT,
            controller,
        )
        is DeviceAuthorizationUiState.Expired -> AuthorizationEndedPanel(
            "Phone request timed out",
            EXPIRED_AUTHORIZATION_PUBLIC_TEXT,
            controller,
        )
        is DeviceAuthorizationUiState.Unavailable -> AuthorizationEndedPanel(
            "Couldn't request phone approval",
            "MeshTrail couldn't ask your device to approve this phone.",
            controller,
        )
        is DeviceAuthorizationUiState.Unsupported -> AuthorizationEndedPanel(
            "Phone setup unavailable",
            "Your device's software doesn't support this phone setup.",
            controller,
        )
        is DeviceAuthorizationUiState.AuthorityUnknown -> AuthorizationEndedPanel(
            "Phone approval not confirmed",
            "The connection dropped before your device answered.\nCheck your device before trying again.",
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
    openAppSettings: () -> Unit,
    openBluetoothSettings: () -> Unit,
    openDeviceSettings: (BleActiveSession) -> Unit,
    onSetupComplete: (V1OnboardingScope) -> Unit,
) {
    when (state) {
        BleRuntimeState.Inactive -> StatusCard(
            "Bluetooth paused",
            "MeshTrail isn't using Bluetooth right now.",
        )
        BleRuntimeState.Idle -> {
            StatusCard("Bluetooth disconnected", "No device is connected.")
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Scan for compatible devices")
            }
            PairingHelp()
        }
        is BleRuntimeState.Blocked -> {
            StatusCard("Bluetooth unavailable", state.reason.publicText())
            val disabled = state.reason == BleRuntimeBlock.BLUETOOTH_DISABLED
            val stateReadDenied = state.reason == BleRuntimeBlock.BLUETOOTH_STATE_READ_DENIED
            if (disabled || stateReadDenied) {
                var refusal by remember(controller, state.reason) { mutableStateOf<BluetoothRecoveryRefusal?>(null) }
                if (disabled) {
                    Button(onClick = openBluetoothSettings, modifier = Modifier.fillMaxWidth()) {
                        Text("Open Bluetooth settings")
                    }
                }
                OutlinedButton(
                    onClick = { refusal = controller.retryBluetoothDiscovery() },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Try again")
                }
                refusal?.let { problem ->
                    Text(
                        problem.publicText(stringResource(R.string.app_name)),
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    )
                }
                if (stateReadDenied ||
                    refusal == BluetoothRecoveryRefusal.PERMISSION_REQUIRED ||
                    refusal == BluetoothRecoveryRefusal.OWNER_UNAVAILABLE ||
                    refusal == BluetoothRecoveryRefusal.RESET_PENDING ||
                    refusal == BluetoothRecoveryRefusal.PLATFORM_BLOCKED
                ) {
                    OutlinedButton(onClick = openAppSettings, modifier = Modifier.fillMaxWidth()) {
                        Text("Open app settings")
                    }
                }
            }
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
                "Only devices ready to pair are shown.",
            )
            if (state.candidates.isEmpty()) Text("No compatible device found yet.")
            BluetoothCandidateList(state.candidates, controller)
            PairingHelp()
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop scan")
            }
        }
        is BleRuntimeState.ScanComplete -> {
            StatusCard(
                "Scan complete",
                if (state.candidates.isEmpty()) {
                    "No MeshTrail device was found.\nRestart a device ready for setup if its code is missing, then scan again."
                } else {
                    "Choose a device or scan again."
                },
            )
            BluetoothCandidateList(state.candidates, controller)
            PairingHelp()
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Scan again")
            }
        }
        BleRuntimeState.FindingReturningOwner -> {
            StatusCard(
                "Finding your saved device",
                "Looking for your saved device nearby.",
            )
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Add a new device instead")
            }
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Stop reconnecting")
            }
        }
        is BleRuntimeState.AwaitingAuthorization -> StatusCard(
            "Connecting",
            "Waiting for your device to answer.",
        )
        is BleRuntimeState.Connecting -> {
            StatusCard("Connecting", "Connecting to ${state.companion.publicLabel}.")
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel connection")
            }
        }
        is BleRuntimeState.Negotiating -> {
            StatusCard("Connecting safely", state.phase.publicText())
            OutlinedButton(onClick = controller::disconnectBluetoothDevice, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel connection")
            }
        }
        is BleRuntimeState.Ready -> BluetoothReadyPanel(state.session, controller, openDeviceSettings, onSetupComplete)
        is BleRuntimeState.FactoryResetRequesting -> StatusCard(
            "Reset request sent",
            "Waiting for your device to accept the reset.",
        )
        is BleRuntimeState.FactoryResetErasing -> StatusCard(
            "Reset in progress",
            "Keep Bluetooth on while your device resets.",
        )
        is BleRuntimeState.FactoryResetVerifying -> StatusCard(
            "Checking the reset",
            "Waiting for your device to restart and show it was reset.",
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
            val recovery=factoryResetRecoveryPresentation(state)
            StatusCard("Device reset confirmed",recovery.detail)
            recovery.bondGuidance?.let {
                StatusCard("Old Bluetooth pairing",it)
                Button(onClick = openBluetoothSettings, modifier = Modifier.fillMaxWidth()) {
                    Text("Open Bluetooth settings")
                }
            }
            Text(recovery.windowGuidance)
            Button(onClick = {
                requestFreshSetupAfterReset(state,
                    { (controller.state as? TrailAppUiState.BluetoothDevice)?.runtimeState },
                    { controller.scanBluetoothDevices() })
            }, modifier = Modifier.fillMaxWidth()) {
                Text("Start fresh device setup")
            }
        }
        is BleRuntimeState.Reconnecting -> {
            val progress = if (state.periodic) {
                "Keep MeshTrail open as it tries ${state.companion.publicLabel} again every 15 seconds."
            } else {
                "Attempt ${state.attempt} of ${state.maximumAttempts} for ${state.companion.publicLabel}."
            }
            val body = if (state.connectionDiagnostic == null) {
                progress
            } else {
                progress + "\n" +
                    "Support code: ${state.connectionDiagnostic.supportCode()}"
            }
            StatusCard(
                if (state.periodic) "Waiting for device" else "Reconnecting",
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
            StatusCard("Couldn't connect to the device", body)
            Button(onClick = controller::scanBluetoothDevices, modifier = Modifier.fillMaxWidth()) {
                Text("Scan again")
            }
        }
        BleRuntimeState.Closed -> StatusCard("Bluetooth closed", "Reopen MeshTrail to use Bluetooth again.")
    }
}

@Composable
private fun LostPhoneGuidance() {
    StatusCard(
        "Lost your paired phone?",
        LOST_PHONE_RECOVERY_PUBLIC_TEXT,
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
                    Text("Step 1 of 5 · Choose the name shown on your device.",
                        style = MaterialTheme.typography.bodySmall)
                    Button(
                        onClick = { controller.selectBluetoothDevice(candidate.endpointToken) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Pair this phone") }
                    Text("Enter the six digits from your device only in Android's pairing prompt.",
                        style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
private fun PairingHelp() {
    var expanded by remember { mutableStateOf(false) }
    androidx.compose.material3.TextButton(onClick = { expanded = !expanded }) {
        Text(if (expanded) "Hide pairing help" else "Pairing help")
    }
    if (expanded) {
        val instructions = androidSystemPairingInstructions(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE)
        Text(instructions.beforeActionTitle, style = MaterialTheme.typography.titleSmall)
        Text(instructions.beforeActionBody, style = MaterialTheme.typography.bodySmall)
        LostPhoneGuidance()
    }
}

internal const val LOST_PHONE_RECOVERY_PUBLIC_TEXT =
    "A new phone needs you to reset your device first.\nThis erases all MeshTrail user data on the device.\nHold the device button for at least 10 seconds.\nRelease it when prompted, then press it briefly within 10 seconds to confirm.\nAfter the reset restarts the device, use its six-digit code to pair within 60 seconds."

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
            "Restart a device ready for setup if the code is missing.\nThe device shows the code for 60 seconds after it starts.\nTap Pair this phone and enter the six digits only in Android's pairing prompt.\nMeshTrail never sees your code.",
        startingTitle = "Pairing with Android",
        startingBody =
            "You have 60 seconds after the device starts.\nEnter the six digits from your device only in Android's pairing prompt.\nIf the code disappears, cancel and restart the device, then try again.",
        pendingTitle = "Bluetooth paired",
        pendingBody =
            "Wait for the device to approve this phone.\nYou don't need to press the device button again.",
    )
}

@Composable
private fun BluetoothReadyPanel(
    session: BleActiveSession,
    controller: TrailUiController,
    openDeviceSettings: (BleActiveSession) -> Unit,
    onSetupComplete: (V1OnboardingScope) -> Unit,
) {
    val device = connectedDevicePresentation(session.configuration)
    StatusCard(device.title, device.detail)
    SnapshotSummary("Device status", session.snapshot)
    if (session.boardPairing != null) {
        androidx.compose.runtime.key(session.companion.endpointToken, session.sessionNonce, session.configuration.editorSessionId) {
            val pairingCodes = rememberBoardPairingCodeRetention()
            val scope = BoardPairingCodeScope(session.companion.endpointToken, session.sessionNonce)
            V1BoardPairingPanel(
                session.boardPairing,
                retainedInitiatorCode = pairingCodes.current(scope, session.boardPairing, SystemClock.elapsedRealtime()),
                onInitiatorCodeSubmitted = { code ->
                    if (code == null) pairingCodes.clear()
                    else pairingCodes.retain(scope, code, SystemClock.elapsedRealtime())
                },
            ) { request ->
                val current = ((controller.state as? TrailAppUiState.BluetoothDevice)
                    ?.runtimeState as? BleRuntimeState.Ready)?.session
                current != null && current.sessionNonce == session.sessionNonce &&
                    current.companion.endpointToken == session.companion.endpointToken &&
                    current.configuration.editorSessionId == session.configuration.editorSessionId &&
                    controller.submitBluetoothAction(request)
            }
        }
    }
    var editConfiguration by remember(session.companion.endpointToken, session.sessionNonce, session.configuration.editorSessionId) {
        mutableStateOf(false)
    }
    if(editConfiguration || V1OnboardingProjection.nameVerified(session.configuration)) {
        androidx.compose.material3.TextButton(onClick={ editConfiguration=!editConfiguration }) {
            Text(if(editConfiguration) "Resume setup" else "Edit saved name or region")
        }
    }
    BleConfigurationPanel(session,controller,onboarding=!editConfiguration,onSetupComplete=onSetupComplete)
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
            Text("Device settings", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
            StatusCard(
                "Connected device",
                connectedDevicePresentation(session.configuration).title,
            )
            StatusCard(
                "Factory reset",
                if (supportsFactoryReset) {
                    APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT
                } else {
                    "This device can't be reset from the app."
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
private fun FactoryResetConfirmationScreen(session: BleActiveSession?, controller: TrailUiController) {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Erase ${factoryResetBoardName(session)}?", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
            Text(APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT)
            StatusCard(
                "Keep the device nearby",
                "MeshTrail checks that your device resets and restarts.",
            )
        }
        Column(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
            Button(onClick = { controller.confirmFactoryReset() }, enabled = session != null, modifier = Modifier.fillMaxWidth()) {
                Text("Erase this device")
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
        "Couldn't confirm that your device received the reset request."
    FactoryResetNotVerifiedReason.REQUEST_REJECTED ->
        "Your device refused the reset."
    FactoryResetNotVerifiedReason.RESPONSE_TIMEOUT ->
        "Your device took too long to answer.\nThe reset is unconfirmed."
    FactoryResetNotVerifiedReason.CONNECTION_LOST_BEFORE_ACCEPTANCE ->
        "The connection dropped before MeshTrail could confirm the reset request."
    FactoryResetNotVerifiedReason.MALFORMED_RESPONSE ->
        "Couldn't read your device's reset reply."
    FactoryResetNotVerifiedReason.VERIFICATION_UNAVAILABLE ->
        "Couldn't check whether your device reset."
    FactoryResetNotVerifiedReason.VERIFICATION_TIMEOUT ->
        "Your device didn't confirm the reset in time."
    FactoryResetNotVerifiedReason.WRONG_DEVICE_OBSERVED ->
        "A different device answered, so your reset is unconfirmed."
    FactoryResetNotVerifiedReason.LOCAL_RECORD_CLEAR_FAILED ->
        "MeshTrail couldn't remove the old saved device after checking the reset."
}

@Composable
internal fun SnapshotSummary(title: String, snapshot: CompanionStatusSnapshot) {
    Text(
        "$title · ${snapshot.radio.deviceStatusLabel()} · GPS ${snapshot.gnss.publicLabel()} · " +
            "power ${snapshot.power.publicLabel()} · location sharing ${snapshot.positionSharing.publicLabel()} · " +
            "waiting requests: ${snapshot.queuedActionCount}",
        style = MaterialTheme.typography.bodyMedium,
    )
}

@Composable
internal fun GroupLocationSection(
    snapshot: GroupLocationSnapshot,
    sourceLabel: String = "Device",
    sourceDescription: String = "These locations come from your device.",
    namePrefix: String = "",
) {
    var expanded by remember { mutableStateOf(false) }
    OutlinedButton(
        onClick = { expanded = !expanded },
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(if (expanded) "Hide group locations" else "Show group locations")
    }
    if (!expanded) return

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                "Group locations",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.semantics { heading() },
            )
            Text(sourceDescription)
            Text("$sourceLabel position sharing: ${snapshot.sharingState.publicLabel()}.")
            GroupPositionCard(namePrefix + "This device", snapshot.selfPosition, sourceLabel)
            Text(
                "Group members (${snapshot.peers.size})",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() },
            )
            if (snapshot.peers.isEmpty()) {
                Text("No group locations yet.")
            } else {
                snapshot.peers.forEach { peer ->
                    GroupPositionCard(namePrefix + peer.displayAlias.value, peer.position, sourceLabel)
                }
            }
            Text("Maps aren't available yet.")
        }
    }
}

@Composable
private fun GroupPositionCard(
    label: String,
    position: GroupLocationPosition,
    sourceLabel: String,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(label, style = MaterialTheme.typography.titleMedium)
            when (position.state) {
                GroupPositionState.UNAVAILABLE -> Text(
                    "$sourceLabel location unavailable.",
                )
                GroupPositionState.CURRENT,
                GroupPositionState.STALE,
                -> {
                    val coordinate = requireNotNull(position.coordinate)
                    Text("$sourceLabel location is ${position.state.publicLabel()}.")
                    Text(
                        "$sourceLabel latitude ${coordinate.latitudeDegrees().fixedCoordinate()}, " +
                            "longitude ${coordinate.longitudeDegrees().fixedCoordinate()}.",
                    )
                    Text("$sourceLabel age: ${position.ageSeconds} seconds.")
                    Text(
                        position.accuracyMeters?.let { "$sourceLabel accuracy: $it meters." }
                            ?: "$sourceLabel accuracy: unknown.",
                    )
                }
            }
        }
    }
}

private fun Double.fixedCoordinate(): String = String.format(Locale.US, "%.5f", this)

@Composable
internal fun ActionControls(submit: (CompanionActionRequest) -> Unit) {
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
        ) { Text("Share location") }
        OutlinedButton(
            onClick = { submit(CompanionActionRequest(CompanionActionKind.STOP_POSITION_SHARING)) },
            modifier = Modifier.weight(1f),
        ) { Text("Stop sharing") }
    }
}

@Composable
internal fun AcknowledgeAlertButton(alertId: ULong, submit: (CompanionActionRequest) -> Unit) {
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
    ) { Text("Mark alert as seen") }
}

@Composable
private fun ActionButton(label: String, modifier: Modifier, action: () -> Unit) {
    Button(onClick = action, modifier = modifier) { Text(label) }
}

private fun CompanionActionResult.bluetoothPublicOutcome(): String = when (disposition) {
    CompanionActionDisposition.QUEUED -> "Your device queued the request."
    CompanionActionDisposition.ADMITTED -> "Your device applied the request."
    CompanionActionDisposition.REJECTED -> when (rejectReason) {
        CompanionActionRejectReason.NONE -> "Your device refused the request."
        CompanionActionRejectReason.UNSUPPORTED_ACTION -> "Your device can't do that."
        CompanionActionRejectReason.STALE_ALERT -> "That alert is no longer current."
        CompanionActionRejectReason.UNAVAILABLE -> "That feature isn't available right now."
        CompanionActionRejectReason.QUEUE_FULL -> "Your device has too many requests waiting."
        CompanionActionRejectReason.POLICY_DENIED -> "Your device didn't allow this request."
        CompanionActionRejectReason.INTERNAL_FAILURE -> "Your device hit an error while handling the request."
        CompanionActionRejectReason.BUSY -> "Your device is busy, so try again in a moment."
    }
}

private fun BleRuntimeBlock.publicText(): String = when (this) {
    BleRuntimeBlock.ADAPTER_IMPLEMENTATION_MISSING -> "This version of MeshTrail can't use Bluetooth."
    BleRuntimeBlock.PLATFORM_UNSUPPORTED -> "Bluetooth needs Android 12 or newer."
    BleRuntimeBlock.BLUETOOTH_UNAVAILABLE -> "This phone doesn't support this kind of Bluetooth."
    BleRuntimeBlock.BLUETOOTH_DISABLED ->
        "Turn on Bluetooth and MeshTrail will check again."
    BleRuntimeBlock.BLUETOOTH_STATE_READ_DENIED ->
        "Android didn't let MeshTrail check Bluetooth.\nAllow Nearby devices access in app settings and try again."
    BleRuntimeBlock.SCAN_PERMISSION_MISSING,
    BleRuntimeBlock.CONNECT_PERMISSION_MISSING,
    -> "Allow Nearby devices access to use Bluetooth."
}

private fun BluetoothRecoveryRefusal.publicText(appName: String): String = when (this) {
    BluetoothRecoveryRefusal.BLUETOOTH_OFF ->
        "Bluetooth is still off or starting.\nCheck Bluetooth settings and wait."
    BluetoothRecoveryRefusal.PERMISSION_REQUIRED ->
        "Allow Nearby devices under Permissions in app settings."
    BluetoothRecoveryRefusal.PERMISSION_PENDING ->
        "Answer the Nearby devices prompt before trying again."
    BluetoothRecoveryRefusal.OWNER_UNAVAILABLE ->
        "Force stop MeshTrail in app settings, then reopen it with Bluetooth on."
    BluetoothRecoveryRefusal.AUTHORIZATION_PENDING ->
        "Finish the prompt on your phone or device, or wait for it to end."
    BluetoothRecoveryRefusal.RESET_PENDING ->
        "The earlier device reset still needs checking.\nForce stop $appName in app settings, then reopen it with Bluetooth on.\nIf prompted, tap Turn on background connection."
    BluetoothRecoveryRefusal.PLATFORM_BLOCKED ->
        "Android still can't use Bluetooth.\nCheck Bluetooth and Nearby devices access in Settings."
    BluetoothRecoveryRefusal.STATE_CHANGED ->
        "The connection changed, so follow the instructions on Device."
}

private fun BleRuntimeFailure.publicText(): String = when (this) {
    BleRuntimeFailure.SCAN_START_FAILED -> "Couldn't start looking for your device."
    BleRuntimeFailure.SETUP_LABEL_AMBIGUOUS -> "Two devices showed the same setup name.\nRestart one and check its screen before you scan again."
    BleRuntimeFailure.SETUP_LABEL_CHANGED -> "A device's setup name changed or couldn't be read.\nCheck its screen and scan again."
    BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS ->
        "More than one saved device answered.\nMove away from the others and try again."
    BleRuntimeFailure.CONNECTION_START_FAILED -> "Couldn't connect to the device.\nMove closer and try again."
    BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED -> "Couldn't make a secure connection to your device."
    BleRuntimeFailure.MTU_NEGOTIATION_FAILED -> "Couldn't set up messaging over Bluetooth."
    BleRuntimeFailure.PROTOCOL_INFO_FAILED -> "Couldn't check that this device works with MeshTrail."
    BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED -> "Couldn't turn on updates from your device."
    BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED -> "Couldn't read your device's starting status."
    BleRuntimeFailure.NEGOTIATION_TIMEOUT -> "Your device took too long to connect."
    BleRuntimeFailure.PROTOCOL_VIOLATION -> "Your device sent a reply MeshTrail couldn't use."
    BleRuntimeFailure.RECONNECT_EXHAUSTED -> "Couldn't reconnect after several tries."
    BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED -> "This connection reached its limit, so reconnect and try again."
    BleRuntimeFailure.ACTION_WRITE_FAILED -> "Couldn't send the request to your device."
    BleRuntimeFailure.ACTION_RESULT_TIMEOUT -> "Your device took too long to answer the request."
    BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST ->
        "The connection dropped during phone approval.\nCheck your device before trying again."
    BleRuntimeFailure.AUTHORIZATION_UNSUPPORTED ->
        "This device doesn't support the phone setup MeshTrail needs."
    BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE ->
        "Couldn't ask your device to approve this phone."
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
        "Keep MeshTrail open and tap Try again."
    ConnectedDeviceServiceStartFailure.NEARBY_PERMISSION_MISSING ->
        "Allow Nearby devices in app settings."
    ConnectedDeviceServiceStartFailure.PLATFORM_REJECTED ->
        "Android didn't let MeshTrail start the background connection."
    ConnectedDeviceServiceStartFailure.SERVICE_UNAVAILABLE,
    null,
    -> "MeshTrail couldn't get the background connection ready."
}

private fun BleNegotiationPhase.publicText(): String = when (this) {
    BleNegotiationPhase.LINK_SECURITY -> "Checking that your phone can connect safely."
    BleNegotiationPhase.ATT_MTU -> "Getting Bluetooth messages ready."
    BleNegotiationPhase.PROTOCOL_INFO -> "Checking your device's software."
    BleNegotiationPhase.STREAM_SUBSCRIPTION -> "Turning on updates from your device."
    BleNegotiationPhase.AUTHORIZATION_CLAIM -> "Waiting for your device to approve this phone."
    BleNegotiationPhase.INITIAL_SNAPSHOT -> "Reading your device's current status."
}

internal fun CompanionRadioState.deviceStatusLabel(): String =
    when (this) {
        CompanionRadioState.UNAVAILABLE -> "radio status not reported"
        CompanionRadioState.UNKNOWN -> "radio status unknown"
        else -> "radio ${publicLabel()}"
    }

internal fun Enum<*>.publicLabel(): String = name.lowercase().replace('_', ' ')

@Composable
internal fun StatusCard(title: String, body: String) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
            Text(body, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@Composable
internal fun TrailTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = androidx.compose.material3.darkColorScheme(
            primary = Color(0xFF65E3BC),
            onPrimary = Color(0xFF00261D),
            primaryContainer = Color(0xFF164A3B),
            onPrimaryContainer = Color(0xFFC7F9E7),
            secondary = Color(0xFFA5CEC0),
            onSecondary = Color(0xFF0A3025),
            secondaryContainer = Color(0xFF214C3F),
            onSecondaryContainer = Color(0xFFD2F9E9),
            background = Color(0xFF05110D),
            onBackground = Color(0xFFE6F5EE),
            surface = Color(0xFF071711),
            onSurface = Color(0xFFE6F5EE),
            surfaceVariant = Color(0xFF18362B),
            onSurfaceVariant = Color(0xFFB2CABF),
            surfaceContainer = Color(0xFF0D251B),
            surfaceContainerHighest = Color(0xFF143125),
            surfaceContainerHigh = Color(0xFF112B20),
            surfaceContainerLow = Color(0xFF0A2017),
            outline = Color(0xFF587E6B),
            outlineVariant = Color(0xFF294C3D),
        ),
        content = content,
    )
}
