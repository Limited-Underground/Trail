package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
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

data class V1OnboardingScreenState(
    val stage: V1SetupStage = V1SetupStage.MATCH_DEVICE,
    val setupLabel: String? = null,
    val deviceName: String = "",
    val publicName: String = "",
    val publiclyDiscoverable: Boolean = true,
    val availableRegions: List<V1RadioRegion> = emptyList(),
    val canSubmit: Boolean = false,
    val statusMessage: String? = null,
    val isBusy: Boolean = false,
)

sealed interface V1OnboardingUiAction {
    data class MatchDevice(val label: String) : V1OnboardingUiAction
    data class SaveDeviceName(val name: String) : V1OnboardingUiAction
    data class VerifyRegion(val region: V1RadioRegion) : V1OnboardingUiAction
    data class SavePublicProfile(val name: String, val visible: Boolean) : V1OnboardingUiAction
    data object Finish : V1OnboardingUiAction
    data object Back : V1OnboardingUiAction
}

/**
 * Renders the earliest caller-verified incomplete step. Inputs emit requests;
 * a button press cannot create authorization or device configuration receipts.
 */
@Composable
fun V1OnboardingScreen(
    state: V1OnboardingScreenState,
    onAction: (V1OnboardingUiAction) -> Unit,
    modifier: Modifier = Modifier,
    connectionContent: @Composable () -> Unit = {},
) {
    val currentState by rememberUpdatedState(state)
    val currentAction by rememberUpdatedState(onAction)
    val currentConnectionContent by rememberUpdatedState(connectionContent)
    // Keep field composition identity stable when the responsive layout moves it.
    val retainedFields = remember {
        movableContentOf { V1OnboardingFields(currentState, currentAction, currentConnectionContent) }
    }
    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Set up your device", style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.semantics { heading() })
        if (state.stage != V1SetupStage.COMPLETE) {
            Text("Step ${state.stage.ordinal + 1} of 5")
        }
        BoxWithConstraints(Modifier.fillMaxWidth()) {
            if (maxWidth >= 720.dp) {
                Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                    Column(Modifier.weight(0.8f), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        V1OnboardingExplanation(state)
                    }
                    Column(Modifier.weight(1.2f), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        retainedFields()
                    }
                }
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    V1OnboardingExplanation(state)
                    retainedFields()
                }
            }
        }
    }
}

@Composable
private fun V1OnboardingExplanation(state: V1OnboardingScreenState) {
    val title = when (state.stage) {
        V1SetupStage.MATCH_DEVICE -> "Find your Trail device"
        V1SetupStage.SECURE_PAIR_AND_AUTHORIZE -> "Connect securely"
        V1SetupStage.NAME_DEVICE -> "Name your device"
        V1SetupStage.CONFIRM_RADIO_REGION -> "Choose your radio region"
        V1SetupStage.PUBLIC_PROFILE -> "How people see you"
        V1SetupStage.COMPLETE -> "You're ready"
    }
    val explanation = when (state.stage) {
        V1SetupStage.MATCH_DEVICE ->
            "Keep your device nearby. Match the setup name on its display so you connect to your own device."
        V1SetupStage.SECURE_PAIR_AND_AUTHORIZE ->
            "Use the PIN shown on this device when Android asks. Setup continues after your phone is authorized."
        V1SetupStage.NAME_DEVICE ->
            "Choose a name you recognize. This name will also appear on your device's display."
        V1SetupStage.CONFIRM_RADIO_REGION ->
            "Choose the region where you will use this device. Radio transmission stays off until the device confirms its compatible configuration."
        V1SetupStage.PUBLIC_PROFILE ->
            "Choose the name nearby people can discover. They must request a chat before sending you messages."
        V1SetupStage.COMPLETE ->
            "Device setup is complete. You can start from Messages and create or join a group whenever you need one."
    }
    Text(title, style = MaterialTheme.typography.titleLarge,
        modifier = Modifier.semantics { heading() })
    Text(explanation, color = MaterialTheme.colorScheme.onSurfaceVariant)
    state.statusMessage?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }
    if (state.isBusy) LinearProgressIndicator(Modifier.fillMaxWidth())
}

@Composable
private fun V1OnboardingFields(
    state: V1OnboardingScreenState,
    onAction: (V1OnboardingUiAction) -> Unit,
    connectionContent: @Composable () -> Unit,
) {
    val enabled = state.canSubmit && !state.isBusy
    when (state.stage) {
        V1SetupStage.MATCH_DEVICE -> {
            connectionContent()
            state.setupLabel?.let { label ->
                V1SetupCard {
                    Text(label, style = MaterialTheme.typography.headlineSmall)
                    Text("Does this match the name on your device?")
                    Button(
                        onClick = { onAction(V1OnboardingUiAction.MatchDevice(label)) },
                        enabled = enabled && V1SetupLabel.create(label) != null,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("This is my device") }
                }
            }
        }
        V1SetupStage.SECURE_PAIR_AND_AUTHORIZE -> {
            state.setupLabel?.let { Text(it, style = MaterialTheme.typography.titleMedium) }
            connectionContent()
            Text("Waiting for secure device confirmation", color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        V1SetupStage.NAME_DEVICE -> {
            var name by rememberSaveable(state.setupLabel) { mutableStateOf(state.deviceName) }
            V1SetupCard {
                OutlinedTextField(
                    value = name,
                    onValueChange = { if (it.length <= V1_DEVICE_NAME_MAX_CHARS) name = it },
                    label = { Text("Device name") },
                    supportingText = { Text("${name.length}/$V1_DEVICE_NAME_MAX_CHARS") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Button(
                    onClick = { onAction(V1OnboardingUiAction.SaveDeviceName(name.trim())) },
                    enabled = enabled && V1DeviceName.create(name.trim()) != null,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Save to device") }
            }
        }
        V1SetupStage.CONFIRM_RADIO_REGION -> {
            // A region must be deliberately selected; locale never selects it.
            var selectedCode by rememberSaveable(state.setupLabel) { mutableStateOf<String?>(null) }
            V1SetupCard {
                if (state.availableRegions.isEmpty()) {
                    Text("Waiting for this device's supported regions. A compatible firmware update may be needed.")
                }
                state.availableRegions.forEach { region ->
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Column(Modifier.weight(1f)) {
                            Text(region.publicCode, style = MaterialTheme.typography.titleMedium)
                            Text(when (region) { V1RadioRegion.US915 -> "United States · 915 MHz" })
                        }
                        RadioButton(selected = selectedCode == region.publicCode,
                            onClick = { selectedCode = region.publicCode }, enabled = !state.isBusy)
                    }
                }
                val selected = state.availableRegions.firstOrNull { it.publicCode == selectedCode }
                Button(
                    onClick = { selected?.let { onAction(V1OnboardingUiAction.VerifyRegion(it)) } },
                    enabled = enabled && selected != null,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Apply & verify region") }
            }
        }
        V1SetupStage.PUBLIC_PROFILE -> {
            var name by rememberSaveable(state.setupLabel) { mutableStateOf(state.publicName) }
            var visible by rememberSaveable(state.setupLabel) { mutableStateOf(state.publiclyDiscoverable) }
            V1SetupCard {
                OutlinedTextField(name, { if (it.length <= V1_PUBLIC_NAME_MAX_CHARS) name = it },
                    label = { Text("Your display name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Column(Modifier.weight(1f)) {
                        Text("Visible to nearby people", style = MaterialTheme.typography.titleMedium)
                        Text("Your location and group are not shared through discovery.")
                    }
                    Switch(checked = visible, onCheckedChange = { visible = it }, enabled = !state.isBusy)
                }
                Button(
                    onClick = { onAction(V1OnboardingUiAction.SavePublicProfile(name.trim(), visible)) },
                    enabled = enabled && V1PublicName.create(name.trim()) != null,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Save profile") }
            }
        }
        V1SetupStage.COMPLETE -> Button(
            onClick = { onAction(V1OnboardingUiAction.Finish) }, enabled = enabled,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Open Messages") }
    }
    if (state.stage != V1SetupStage.COMPLETE) {
        TextButton(onClick = { onAction(V1OnboardingUiAction.Back) }, enabled = !state.isBusy) {
            Text("Set up later")
        }
    }
}

@Composable
private fun V1SetupCard(content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp), content = content)
    }
}
