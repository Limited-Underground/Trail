package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.RegionSelectionCatalog

@Composable
internal fun BleConfigurationPanel(session: BleActiveSession, controller: TrailUiController) {
    val state=session.configuration
    if(!state.available) return
    val context=LocalContext.current
    val draft=remember { V1SetupDraftRepository(AndroidV1SetupDraftStorage(context)).load() }
    var name by rememberSaveable(session.sessionNonce) { mutableStateOf(draft?.deviceName?.value.orEmpty()) }
    var regionChoice by rememberSaveable(session.sessionNonce) { mutableStateOf<Int?>(null) }
    var regionMenu by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp),verticalArrangement=Arrangement.spacedBy(8.dp)) {
            Text("Device configuration",style=MaterialTheme.typography.titleMedium)
            Text(state.notice)
            state.deviceName?.let { Text("Last device readback: $it") }
            OutlinedTextField(value=name,onValueChange={ name=it },label={ Text("Device name") },
                enabled=!state.busy,singleLine=true,modifier=Modifier.fillMaxWidth())
            Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick={ controller.readDeviceName() },enabled=!state.busy) { Text("Read device") }
                Button(onClick={ controller.writeDeviceName(name) },enabled=!state.busy && state.nameRevision!=null && V1DeviceName.create(name)!=null) {
                    Text("Apply name")
                }
            }
            OutlinedButton(onClick={ controller.synchronizeDisplayTime() },enabled=!state.busy) { Text("Sync display clock") }
            if(state.regionAvailable) {
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
                OutlinedButton(onClick={ controller.readRadioRegion() },enabled=!state.busy) { Text("Read region") }
                Button(onClick={ regionChoice?.let { controller.writeRadioRegion(it) } },
                    enabled=!state.busy && regionChoice!=null && state.regionRevision!=null && state.regionRevision!=ULong.MAX_VALUE) {
                    Text("Apply region choice")
                }
                Text("Choose where you will use the device. Saving a choice does not configure the radio or authorize transmission. Radio TX remains disabled.",style=MaterialTheme.typography.bodySmall)
            }
            Text("The clock syncs automatically on connection and when your phone's time changes, using its local time and 12/24-hour preference. You can also sync it here.",style=MaterialTheme.typography.bodySmall)
        }
    }
}
