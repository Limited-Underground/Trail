package io.github.nbjelanovic.otclient

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            TrailTheme {
                CompanionApp(remember { CompanionAppController(FakeCompanionTransport()) })
            }
        }
    }
}

@Composable
fun CompanionApp(controller: CompanionAppController) {
    var state by remember { mutableStateOf(controller.state) }
    DisposableEffect(controller) {
        controller.observe { state = it }
        onDispose { controller.observe(null) }
    }

    Surface(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text("Limited Underground", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            Text("Trail", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
            Text("Android foundation · fake transport only", style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(8.dp))
            when (val current = state) {
                is CompanionUiState.Disconnected -> DisconnectedPanel(current, controller)
                is CompanionUiState.Selecting -> SelectionPanel(current, controller)
                is CompanionUiState.Connecting -> StatusCard("Connecting", "Opening ${current.candidate.publicLabel}…")
                is CompanionUiState.Connected -> ConnectedPanel(current, controller)
                is CompanionUiState.Failed -> FailedPanel(current, controller)
            }
        }
    }
}

@Composable
private fun DisconnectedPanel(state: CompanionUiState.Disconnected, controller: CompanionAppController) {
    StatusCard("Disconnected", "No companion session is active. ${state.candidates.size} deterministic test choices available.")
    Button(onClick = controller::chooseDevice, modifier = Modifier.fillMaxWidth()) {
        Text("Choose test device")
    }
}

@Composable
private fun SelectionPanel(state: CompanionUiState.Selecting, controller: CompanionAppController) {
    Text("Choose one device", style = MaterialTheme.typography.titleLarge)
    Text("A production phone will connect to one LoRa device. These entries are local fakes and do not scan Bluetooth.")
    LazyColumn(verticalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth().heightIn(max = 320.dp)) {
        items(state.candidates, key = { it.endpointToken }) { candidate ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(modifier = Modifier.fillMaxWidth().padding(16.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text(candidate.publicLabel, modifier = Modifier.weight(1f))
                    Button(onClick = { controller.connect(candidate.endpointToken) }) { Text("Connect") }
                }
            }
        }
    }
    OutlinedButton(onClick = controller::cancelSelection, modifier = Modifier.fillMaxWidth()) { Text("Cancel") }
}

@Composable
private fun ConnectedPanel(state: CompanionUiState.Connected, controller: CompanionAppController) {
    StatusCard("Connected to ${state.connection.publicLabel}", state.connection.status)
    Text("Android action and device-state binding arrives in a later increment; this build still uses only the fake transport.")
    OutlinedButton(onClick = controller::disconnect, modifier = Modifier.fillMaxWidth()) { Text("Disconnect") }
}

@Composable
private fun FailedPanel(state: CompanionUiState.Failed, controller: CompanionAppController) {
    StatusCard("Connection unavailable", state.publicReason)
    Text("${state.candidates.size} deterministic test choices currently available.")
    Button(onClick = controller::retrySelection, modifier = Modifier.fillMaxWidth()) { Text("Choose again") }
}

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
