package io.github.nbjelanovic.otclient

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class V1TestLogActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent { TrailTheme { LogReview() } }
    }

    @OptIn(ExperimentalLayoutApi::class)
    @Composable
    private fun LogReview() {
        val runtime = remember { V1TestLogRuntime.get(applicationContext) }
        var report by remember { mutableStateOf<String?>(null) }
        var pending by rememberSaveable { mutableStateOf<String?>(null) }
        var status by rememberSaveable { mutableStateOf<String?>(null) }
        var confirmClear by rememberSaveable { mutableStateOf(false) }
        val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
            val text = pending
            pending = null
            if (uri != null && text != null) {
                status = runCatching {
                    requireNotNull(contentResolver.openOutputStream(uri, "wt"))
                        .bufferedWriter(Charsets.UTF_8).use { it.write(text) }
                    "Connection log saved."
                }.getOrElse { "Connection log could not be saved." }
            }
        }
        LaunchedEffect(runtime) { runtime.read { report = it } }
        Surface(Modifier.fillMaxSize()) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                TextButton(onClick = { finish() }) { Text("Back") }
                Text("V1-Test connection log", style = MaterialTheme.typography.headlineSmall)
                Text("Records observed connection changes and main-screen lifecycle only. Background gaps are not continuous monitoring. Nothing is uploaded automatically.")
                Text("GPS, radio traffic, signal and battery measurements are not included yet.")
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { runtime.read { report = it } }) { Text("Refresh") }
                    Button(enabled = report != null, onClick = {
                        pending = report; save.launch("trail-field-test-log.txt")
                    }) { Text("Save text file") }
                    Button(enabled = report != null, onClick = {
                        report?.let { status = shareV1TestConnectionLog(this@V1TestLogActivity,
                            V1SupportReport("trail-field-test-log.txt", "text/plain", it)) }
                    }) { Text("Share / Email") }
                    OutlinedButton(onClick = { confirmClear = true }) { Text("Clear log") }
                }
                status?.let { Text(it) }
                Text(report ?: "Loading recorded events…")
            }
        }
        if (confirmClear) AlertDialog(onDismissRequest = { confirmClear = false },
            title = { Text("Clear recorded events?") },
            text = { Text("This deletes this app's saved connection history. Files already exported are not deleted.") },
            confirmButton = { TextButton(onClick = {
                confirmClear = false
                runtime.clear { cleared ->
                    status = if (cleared) "Connection log cleared." else "Could not clear connection log."
                    runtime.read { report = it }
                }
            }) { Text("Clear") } },
            dismissButton = { TextButton(onClick = { confirmClear = false }) { Text("Cancel") } })
    }
}
