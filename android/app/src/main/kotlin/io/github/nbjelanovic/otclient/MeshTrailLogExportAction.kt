package io.github.nbjelanovic.otclient

import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.launch

@Composable
internal fun MeshTrailLogExportAction() {
    val context = LocalContext.current
    val logger = remember(context) { MeshTrailOperationLogging.from(context) }
    val scope = rememberCoroutineScope()
    var preparing by remember { mutableStateOf(false) }
    var failed by remember { mutableStateOf(false) }
    Text("Share recent app and board events as a text file. Choose an email or storage app to send or save it.")
    Button(enabled = !preparing, onClick = {
        preparing = true
        failed = false
        scope.launch {
            try {
                val text = logger.exportText()
                failed = text == null || !shareMeshTrailLog(context, text)
            } finally {
                preparing = false
            }
        }
    }) {
        Text("Share operation log (.txt)")
    }
    if (failed) Text("Couldn't share the operation log. Try again later.")
}
