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
import io.github.nbjelanovic.otprotocol.CompanionPublicProfileCodec

// UI restoration epoch only: never an identity, protocol field, or persisted preference.
private val nameEditorProcessEpoch=java.util.UUID.randomUUID().toString()

@Composable
internal fun BleConfigurationPanel(session: BleActiveSession, controller: TrailUiController, onboarding: Boolean = false,
    onSetupComplete: (V1OnboardingScope) -> Unit = {}) {
    val state=session.configuration
    if(!state.available) {
        if(onboarding) Text("Step 3 of 5 · Your device can't share its settings yet.")
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
            V1OnboardingCommand.ReadPublicProfile -> controller.readPublicProfile()
            is V1OnboardingCommand.WritePublicProfile -> controller.writePublicProfile(command.name,command.visible)
        } },
    ) }
    val stage=V1OnboardingProjection.stage(BleRuntimeState.Ready(session))
    val completeSetup by rememberUpdatedState(onSetupComplete)
    LaunchedEffect(scope,onboarding,stage,state.busy) {
        if(onboarding && stage==V1SetupStage.COMPLETE && !state.busy) completeSetup(scope)
    }
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
    var publicText by rememberSaveable(editorScope) { mutableStateOf(draft?.publicName?.value.orEmpty()) }
    var publicVisible by rememberSaveable(editorScope) { mutableStateOf(draft?.publiclyDiscoverable ?: true) }
    var publicEdited by rememberSaveable(editorScope) { mutableStateOf(false) }
    var attemptedPublicRead by remember(scope) { mutableStateOf(false) }
    LaunchedEffect(scope,stage,state.busy,state.publicRevision) {
        if(onboarding && stage==V1SetupStage.PUBLIC_PROFILE && state.publicProfileAvailable &&
            state.publicRevision==null && !state.busy && !attemptedPublicRead) {
            attemptedPublicRead=true
            submit(V1OnboardingCommand.ReadPublicProfile)
        }
    }
    LaunchedEffect(editorScope,state.publicRevision,state.publicName,state.publiclyDiscoverable) {
        if(!publicEdited && state.publicRevision!=null) {
            if(state.publicName!=null) publicText=state.publicName
            state.publiclyDiscoverable?.let { publicVisible=it }
        }
    }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp),verticalArrangement=Arrangement.spacedBy(8.dp)) {
            Text(if(onboarding && stage==V1SetupStage.COMPLETE) "Device setup complete" else if(onboarding) "Step ${stage.ordinal+1} of 5 · ${when(stage) {
                V1SetupStage.NAME_DEVICE -> "Name your device"
                V1SetupStage.CONFIRM_RADIO_REGION -> "Choose your radio region"
                else -> "Public name and visibility"
            }}" else "Name, region and profile",style=MaterialTheme.typography.titleMedium)
            Text(state.notice)
            if(!onboarding || stage==V1SetupStage.NAME_DEVICE) {
            state.deviceName?.let { Text("Saved device name: $it") }
            state.suggestedDeviceName?.let {
                Text("Suggested device name: $it.",
                    style=MaterialTheme.typography.bodySmall)
            }
            OutlinedTextField(value=name,onValueChange={ editor=editor.edit(it) },label={ Text("Device name") },
                enabled=!state.busy,singleLine=true,modifier=Modifier.fillMaxWidth())
            Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick={ submit(V1OnboardingCommand.ReadName) },enabled=!state.busy) { Text("Check saved name") }
                Button(onClick={ submit(V1OnboardingCommand.WriteName(name)) },enabled=!state.busy && state.nameRevision!=null && V1DeviceName.create(name)!=null) {
                    Text("Apply name")
                }
            }
            }
            OutlinedButton(onClick={ controller.synchronizeDisplayTime() },enabled=!state.busy) { Text("Set clock from phone") }
            if(state.regionAvailable && V1OnboardingProjection.nameVerified(state) && (!onboarding || stage==V1SetupStage.CONFIRM_RADIO_REGION)) {
                HorizontalDivider()
                Text("Radio region",style=MaterialTheme.typography.titleMedium)
                Text("Saved choice: ${state.regionSelectionId?.let { RegionSelectionCatalog.find(it)?.code } ?: if(state.regionRevision == 0uL) "Not configured" else "Not checked yet"}")
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
                OutlinedButton(onClick={ submit(V1OnboardingCommand.ReadRegion) },enabled=!state.busy) { Text(if(onboarding) "Check region to continue" else "Check saved region") }
                Button(onClick={ regionChoice?.let { submit(V1OnboardingCommand.WriteRegion(it)) } },
                    enabled=!state.busy && regionChoice!=null && state.regionRevision!=null && state.regionRevision!=ULong.MAX_VALUE) {
                    Text("Save region")
                }
                Text("Choose the region where you use your device.",style=MaterialTheme.typography.bodySmall)
            }
            if(onboarding && stage==V1SetupStage.CONFIRM_RADIO_REGION && !state.regionAvailable) {
                Text("Your device needs an update before you can check its saved radio region.")
            }
            if(stage in listOf(V1SetupStage.PUBLIC_PROFILE,V1SetupStage.COMPLETE)) {
                HorizontalDivider()
                Text("Public name and visibility",style=MaterialTheme.typography.titleMedium)
                if(state.publicProfileAvailable) {
                    Text("Saved public name: ${state.publicName ?: if(state.publicRevision==0uL) "Not configured" else "Not confirmed"}")
                    Text("Saved visibility: ${state.publiclyDiscoverable?.let { if(it) "On" else "Off" } ?: "Not confirmed"}")
                    OutlinedTextField(value=publicText,onValueChange={ publicText=it;publicEdited=true },
                        label={ Text("Public name") },enabled=!state.busy,singleLine=true,modifier=Modifier.fillMaxWidth())
                    Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                        Text("Public visibility")
                        Switch(checked=publicVisible,onCheckedChange={ publicVisible=it;publicEdited=true },enabled=!state.busy)
                    }
                    val validPublic=V1PublicName.create(publicText)!=null && CompanionPublicProfileCodec.validName(publicText)
                    if(publicText.isNotEmpty() && !validPublic) Text("Use a shorter public name with letters, numbers and ordinary name punctuation.")
                    Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick={ submit(V1OnboardingCommand.ReadPublicProfile) },enabled=!state.busy) { Text("Check saved profile") }
                        Button(onClick={ submit(V1OnboardingCommand.WritePublicProfile(publicText,publicVisible)) },
                            enabled=!state.busy && validPublic && state.publicRevision!=null && state.publicRevision!=ULong.MAX_VALUE) { Text("Save profile") }
                    }
                    Text(if(stage==V1SetupStage.COMPLETE) "Your device confirmed your setup.\nSaving these settings doesn't make you visible yet."
                        else "Setup continues when your device confirms these settings.\nSaving them doesn't make you visible yet.")
                } else Text("Your device needs an update to check your profile.\nYour choices stay on this phone for now.")
                Text("Your device follows the radio rules for its saved region.")
            }
            Text("Your device clock follows your phone's time and format.",style=MaterialTheme.typography.bodySmall)
        }
    }
}
