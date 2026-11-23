package io.github.nbjelanovic.otclient

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
internal fun MeshTrailLogExportAction() {
    val context = LocalContext.current
    val logger = remember(context) { MeshTrailOperationLogging.from(context) }
    val scope = rememberCoroutineScope()
    var preparing by remember { mutableStateOf(false) }
    var pendingSaveText by rememberSaveable { mutableStateOf<String?>(null) }
    var status by rememberSaveable { mutableStateOf<String?>(null) }
    val saveLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument(V1_SUPPORT_REPORT_MIME_TYPE),
    ) { destination ->
        val text = pendingSaveText
        pendingSaveText = null
        if (destination != null && text != null) {
            preparing = true
            scope.launch {
                try {
                    val saved = withContext(Dispatchers.IO) { saveV1TextDocument(context, destination, text) }
                    status = if (saved) "Operation log saved." else "Couldn't save the operation log."
                } finally {
                    preparing = false
                }
            }
        }
    }

    fun prepareLog(onReady: (String) -> Unit) {
        if (preparing || pendingSaveText != null) return
        preparing = true
        status = null
        scope.launch {
            try {
                val text = logger.exportText()
                if (text == null) {
                    status = "Couldn't prepare the operation log."
                } else {
                    onReady(text)
                }
            } finally {
                preparing = false
            }
        }
    }

    val canExport = !preparing && pendingSaveText == null
    Text("Share the log by email, or save it to upload to your cloud storage and share a link.")
    Button(enabled = canExport, onClick = {
        prepareLog { text ->
            if (!shareMeshTrailLog(context, text)) {
                status = "Couldn't share the operation log. Try saving it instead."
            }
        }
    }) {
        Text("Share operation log (.txt)")
    }
    Button(enabled = canExport, onClick = {
        prepareLog { text ->
            val report = meshTrailLogAttachment(text)
            if (report == null) {
                status = "Couldn't prepare the operation log."
            } else {
                pendingSaveText = report.text
                if (runCatching { saveLauncher.launch(report.fileName) }.isFailure) {
                    pendingSaveText = null
                    status = "Couldn't open the file picker."
                }
            }
        }
    }) {
        Text("Save operation log (.txt)")
    }
    status?.let { Text(it) }
}
