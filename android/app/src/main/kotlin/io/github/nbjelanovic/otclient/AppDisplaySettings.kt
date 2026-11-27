package io.github.nbjelanovic.otclient

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

/** Owner-approved local section on the existing Device tab; no new destination. */
@Composable
internal fun AppDisplaySettings(showBuildLabel: Boolean, onShowBuildLabelChanged: (Boolean) -> Unit,
    saveFailed: Boolean, languageChoiceAvailable: Boolean = AppLanguageSetting.isAvailable()) {
    val context = LocalContext.current
    var language by remember(context) { mutableStateOf(AppLanguageSetting.current(context)) }
    var choosingLanguage by remember { mutableStateOf(false) }
    var languageSaveFailed by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text(stringResource(R.string.settings_app_title), style = MaterialTheme.typography.titleMedium)
        val buildLabel = stringResource(R.string.settings_show_build_label)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(buildLabel, modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium)
            Switch(checked = showBuildLabel, onCheckedChange = onShowBuildLabelChanged,
                modifier = Modifier.semantics { contentDescription = buildLabel })
        }
        // Android 13+ only: the system keeps the choice; older phones follow the phone's language.
        if (languageChoiceAvailable) Row(
            Modifier.fillMaxWidth().clickable(role = Role.Button) { choosingLanguage = true }
                .padding(vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(stringResource(R.string.settings_language), modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium)
            Text(language?.nativeName ?: stringResource(R.string.settings_language_system_default),
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.primary)
        }
        if (saveFailed || languageSaveFailed) Text(stringResource(R.string.settings_save_failed),
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
    if (choosingLanguage) {
        AppLanguageDialog(
            selected = language,
            onDismiss = { choosingLanguage = false },
            onSelect = { choice ->
                choosingLanguage = false
                if (choice != language) {
                    val saved = AppLanguageSetting.select(context, choice)
                    languageSaveFailed = !saved
                    if (saved) language = choice
                }
            },
        )
    }
}

@Composable
private fun AppLanguageDialog(
    selected: AppLanguage?,
    onDismiss: () -> Unit,
    onSelect: (AppLanguage?) -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.settings_language_choose)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                AppLanguageOption(stringResource(R.string.settings_language_system_default),
                    selected == null) { onSelect(null) }
                AppLanguage.entries.forEach { language ->
                    AppLanguageOption(language.nativeName, selected == language) { onSelect(language) }
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

@Composable
private fun AppLanguageOption(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().selectable(selected = selected, role = Role.RadioButton, onClick = onClick)
            .padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(selected = selected, onClick = null)
        Spacer(Modifier.width(12.dp))
        Text(label, style = MaterialTheme.typography.bodyLarge)
    }
}
