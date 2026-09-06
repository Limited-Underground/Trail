package io.github.nbjelanovic.otclient

import android.content.Context
import android.util.Base64
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/** Pending phone-local preferences, never evidence of a device configuration. */
internal class AndroidV1SetupDraftStorage(context: Context) : V1SetupDraftStorage {
    private val preferences = context.getSharedPreferences("v1-pending-setup", Context.MODE_PRIVATE)
    override fun read(): ByteArray? {
        val encoded = preferences.getString("draft", null) ?: return null
        if (encoded.length > 1024) return null
        return runCatching { Base64.decode(encoded, Base64.NO_WRAP) }.getOrNull()
    }
    override fun write(encoded: ByteArray): Boolean = encoded.size <= V1SetupDraftCodec.MAX_BYTES &&
        preferences.edit().putString("draft", Base64.encodeToString(encoded, Base64.NO_WRAP)).commit()
    override fun clear(): Boolean = preferences.edit().remove("draft").commit()
}

@Composable
internal fun V1SetupDraftScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val repository = remember { V1SetupDraftRepository(AndroidV1SetupDraftStorage(context)) }
    val initial = remember { repository.load() }
    var deviceName by rememberSaveable { mutableStateOf(initial?.deviceName?.value ?: "") }
    var publicName by rememberSaveable { mutableStateOf(initial?.publicName?.value ?: "") }
    var regionSelected by rememberSaveable { mutableStateOf(initial?.radioRegion == V1RadioRegion.US915) }
    var visible by rememberSaveable { mutableStateOf(initial?.publiclyDiscoverable ?: true) }
    var status by rememberSaveable { mutableStateOf<String?>(null) }
    val draft = V1SetupDraft.create(deviceName.trim().ifEmpty { null }, publicName.trim().ifEmpty { null },
        if (regionSelected) V1RadioRegion.US915 else null, visible)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        TextButton(onClick = onBack) { Text("Back to Device") }
        Text("Prepare device setup", style = MaterialTheme.typography.headlineMedium)
        Text("Save these choices on this phone. They are not applied to a device and do not complete setup. Device confirmation requires the upcoming firmware update.")
        OutlinedTextField(deviceName, { if (it.length <= V1_DEVICE_NAME_MAX_CHARS) { deviceName = it; status = null } },
            label = { Text("Device name") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
        OutlinedTextField(publicName, { if (it.length <= V1_PUBLIC_NAME_MAX_CHARS) { publicName = it; status = null } },
            label = { Text("Your display name") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
        Text("Planned radio region", style = MaterialTheme.typography.titleMedium)
        Row(Modifier.fillMaxWidth()) {
            Checkbox(regionSelected, { regionSelected = it; status = null })
            Text("United States · 915 MHz", modifier = Modifier.weight(1f))
        }
        Text("Leave unselected if you will use another region. This draft does not authorize transmission or verify hardware compatibility.",
            style = MaterialTheme.typography.bodySmall)
        Row(Modifier.fillMaxWidth()) {
            Text("Visible to nearby people", modifier = Modifier.weight(1f))
            Switch(visible, { visible = it; status = null })
        }
        Text("Visibility takes effect only after device setup is confirmed. Direct-chat location sharing starts OFF.",
            style = MaterialTheme.typography.bodySmall)
        Button(onClick = { status = if (draft != null && repository.save(draft))
            "Draft saved on this phone. Not applied to device." else "Draft could not be saved." }, enabled = draft != null) {
            Text("Save setup draft")
        }
        status?.let { Text(it) }
    }
}
