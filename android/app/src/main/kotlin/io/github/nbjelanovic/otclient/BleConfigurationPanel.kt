package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@Composable
internal fun BleConfigurationPanel(session: BleActiveSession, controller: TrailUiController) {
    val state=session.configuration
    if(!state.available) return
    val context=LocalContext.current
    val draft=remember { V1SetupDraftRepository(AndroidV1SetupDraftStorage(context)).load() }
    var name by rememberSaveable(session.sessionNonce) { mutableStateOf(draft?.deviceName?.value.orEmpty()) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp),verticalArrangement=Arrangement.spacedBy(8.dp)) {
            Text("Device name and clock",style=MaterialTheme.typography.titleMedium)
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
            Text("Clock uses this phone's local time and 12/24-hour preference. Region and group setup remain unavailable.",style=MaterialTheme.typography.bodySmall)
        }
    }
}
