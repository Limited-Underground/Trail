package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.Saver
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.RegionSelectionCatalog

// UI restoration epoch only: never an identity, protocol field, or persisted preference.
private val nameEditorProcessEpoch=java.util.UUID.randomUUID().toString()

@Composable
internal fun BleConfigurationPanel(session: BleActiveSession, controller: TrailUiController, onboarding: Boolean = false) {
    val state=session.configuration
    if(!state.available) {
        if(onboarding) Text("Step 3 of 5 · Device name. Waiting for protected device configuration support.")
        return
    }
    val scope=remember(session.companion.endpointToken,session.sessionNonce,state.editorSessionId) {
        V1OnboardingScope.from(session)
    }
    val binding=remember(controller) { V1OnboardingBinding(
        runtime={ (controller.state as? TrailAppUiState.BluetoothDevice)?.runtimeState },
        send={ command -> when(command) {
            V1OnboardingCommand.ReadName -> controller.readDeviceName()
            is V1OnboardingCommand.WriteName -> controller.writeDeviceName(command.value)
            V1OnboardingCommand.ReadRegion -> controller.readRadioRegion()
            is V1OnboardingCommand.WriteRegion -> controller.writeRadioRegion(command.selectionId)
        } },
    ) }
    val stage=V1OnboardingProjection.stage(BleRuntimeState.Ready(session))
    fun submit(command: V1OnboardingCommand): Boolean = binding.submit(scope,command,onboarding)
    var attemptedRegionRead by remember(scope) { mutableStateOf(false) }
    LaunchedEffect(scope,stage,state.busy) {
        if(onboarding && stage==V1SetupStage.CONFIRM_RADIO_REGION && state.regionAvailable &&
            state.regionRevision==null && !state.busy && !attemptedRegionRead) {
            attemptedRegionRead=true
            submit(V1OnboardingCommand.ReadRegion)
        }
    }
    val context=LocalContext.current
    val draft=remember { V1SetupDraftRepository(AndroidV1SetupDraftStorage(context)).load() }
    val editorScope="$nameEditorProcessEpoch:${state.editorSessionId}"
    val editorSaver=remember(editorScope) {
        Saver<BleDeviceNameEditor,Any>(
            save={ listOf(editorScope,it.text,it.edited,it.suggestion.orEmpty()) },
            restore={ BleDeviceNameEditor.restore(it,editorScope,draft?.deviceName?.value.orEmpty()) })
    }
    var editor by rememberSaveable(state.editorSessionId,stateSaver=editorSaver) {
        mutableStateOf(BleDeviceNameEditor(draft?.deviceName?.value.orEmpty()))
    }
    LaunchedEffect(state.editorSessionId, state.suggestedDeviceName, state.deviceName) {
        editor=editor.suggest(state.suggestedDeviceName, state.deviceName)
    }
    val name=editor.text
    var regionChoice by rememberSaveable(session.sessionNonce) { mutableStateOf<Int?>(null) }
    var regionMenu by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp),verticalArrangement=Arrangement.spacedBy(8.dp)) {
            Text(if(onboarding) "Step ${stage.ordinal+1} of 5 · ${when(stage) {
                V1SetupStage.NAME_DEVICE -> "Name your device"
                V1SetupStage.CONFIRM_RADIO_REGION -> "Choose your radio region"
                else -> "Public name and visibility"
            }}" else "Device configuration",style=MaterialTheme.typography.titleMedium)
            Text(state.notice)
            if(!onboarding || stage==V1SetupStage.NAME_DEVICE) {
            state.deviceName?.let { Text("Last device readback: $it") }
            state.suggestedDeviceName?.let {
                Text("Suggested name from the device you matched: $it. Edit it or tap Apply name to save it.",
                    style=MaterialTheme.typography.bodySmall)
            }
            OutlinedTextField(value=name,onValueChange={ editor=editor.edit(it) },label={ Text("Device name") },
                enabled=!state.busy,singleLine=true,modifier=Modifier.fillMaxWidth())
            Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick={ submit(V1OnboardingCommand.ReadName) },enabled=!state.busy) { Text("Read device") }
                Button(onClick={ submit(V1OnboardingCommand.WriteName(name)) },enabled=!state.busy && state.nameRevision!=null && V1DeviceName.create(name)!=null) {
                    Text("Apply name")
                }
            }
            }
            OutlinedButton(onClick={ controller.synchronizeDisplayTime() },enabled=!state.busy) { Text("Sync display clock") }
            if(state.regionAvailable && V1OnboardingProjection.nameVerified(state) && (!onboarding || stage==V1SetupStage.CONFIRM_RADIO_REGION)) {
                HorizontalDivider()
                Text("Radio region choice",style=MaterialTheme.typography.titleMedium)
                Text("Saved choice: ${state.regionSelectionId?.let { RegionSelectionCatalog.find(it)?.code } ?: if(state.regionRevision == 0uL) "Not configured" else "Not read back"}")
                Box {
                    OutlinedButton(onClick={ regionMenu=true },enabled=!state.busy) {
                        Text(regionChoice?.let { RegionSelectionCatalog.find(it)?.code } ?: "Choose region")
                    }
                    DropdownMenu(expanded=regionMenu,onDismissRequest={ regionMenu=false }) {
                        RegionSelectionCatalog.entries.forEach { selection ->
                            DropdownMenuItem(text={ Text(selection.code) },onClick={ regionChoice=selection.id;regionMenu=false })
                        }
                    }
                }
                OutlinedButton(onClick={ submit(V1OnboardingCommand.ReadRegion) },enabled=!state.busy) { Text(if(onboarding) "Read region to continue" else "Read region") }
                Button(onClick={ regionChoice?.let { submit(V1OnboardingCommand.WriteRegion(it)) } },
                    enabled=!state.busy && regionChoice!=null && state.regionRevision!=null && state.regionRevision!=ULong.MAX_VALUE) {
                    Text("Apply region choice")
                }
                Text("Choose where you will use the device. Saving a choice does not configure the radio or authorize transmission. Radio TX remains disabled.",style=MaterialTheme.typography.bodySmall)
            }
            if(onboarding && stage==V1SetupStage.CONFIRM_RADIO_REGION && !state.regionAvailable) {
                Text("This firmware cannot read back a saved radio region. Setup is paused until compatible device support is available.")
            }
            if(onboarding && stage==V1SetupStage.PUBLIC_PROFILE) {
                Text("Device name and saved region were read back from this connected device.")
                Text("Public name and visibility need the upcoming device update. Setup cannot be completed yet. Preferences saved under Prepare setup choices remain drafts; no public discovery is enabled.")
                Text("A saved region does not configure the radio or authorize transmission. Radio TX remains disabled.")
            }
            Text("The clock syncs automatically on connection and when your phone's time changes, using its local time and 12/24-hour preference. You can also sync it here.",style=MaterialTheme.typography.bodySmall)
        }
    }
}
