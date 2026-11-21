package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.github.nbjelanovic.otprotocol.CompanionActionDisposition
import io.github.nbjelanovic.otprotocol.CompanionActionResult

internal fun createActivityLocalController(): CompanionAppController? =
    CompanionAppController(FakeCompanionTransport())

@Composable
internal fun VariantLocalConnection(state: CompanionUiState, controller: TrailUiController) {
    LocalTestPanel(state, controller)
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
    GroupLocationSection(
        state.connection.groupLocation,
        sourceLabel = "Fake fixture",
        sourceDescription = "Deterministic fake location fixture. This is not phone GPS, device, radio, or map evidence.",
        namePrefix = "Fake ",
    )
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

private fun CompanionActionResult.fakePublicOutcome(): String = when (disposition) {
    CompanionActionDisposition.QUEUED -> "Queued in the fake device. This is not sent or delivered evidence."
    CompanionActionDisposition.ADMITTED -> "Applied in deterministic test state."
    CompanionActionDisposition.REJECTED -> "The fake device rejected the request: ${rejectReason.publicLabel()}."
}
