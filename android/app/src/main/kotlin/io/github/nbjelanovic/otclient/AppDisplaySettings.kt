package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

/** Owner-approved local section on the existing Device tab; no new destination. */
@Composable
internal fun AppDisplaySettings(showBuildLabel: Boolean, onShowBuildLabelChanged: (Boolean) -> Unit,
    saveFailed: Boolean) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text("App settings", style = MaterialTheme.typography.titleMedium)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("Show app build in top bar", modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium)
            Switch(checked = showBuildLabel, onCheckedChange = onShowBuildLabelChanged,
                modifier = Modifier.semantics { contentDescription = "Show app build in top bar" })
        }
        if (saveFailed) Text("Could not save this setting. Please try again.",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
}
