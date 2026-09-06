package io.github.nbjelanovic.otclient

import android.content.ClipData
import android.content.Context
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import java.io.File

@Composable
@OptIn(ExperimentalLayoutApi::class)
internal fun V1SupportScreen(
    snapshot: V1SafeDiagnosticSnapshot,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var problemNote by rememberSaveable { mutableStateOf("") }
    var pendingSaveText by rememberSaveable { mutableStateOf<String?>(null) }
    var statusMessage by rememberSaveable { mutableStateOf<String?>(null) }
    val report = remember(snapshot, problemNote) {
        V1SupportReportGenerator.create(snapshot, problemNote)
    }
    val saveLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument(V1_SUPPORT_REPORT_MIME_TYPE),
    ) { destination ->
        val text = pendingSaveText
        pendingSaveText = null
        if (destination != null && text != null) {
            statusMessage = runCatching {
                val output = requireNotNull(context.contentResolver.openOutputStream(destination, "wt"))
                output.bufferedWriter(Charsets.UTF_8).use { it.write(text) }
                "Support report saved."
            }.getOrElse { "Support report could not be saved." }
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Help and support")
        Text("Nothing is sent automatically. Review the report before saving or sharing it.")
        OutlinedTextField(
            value = problemNote,
            onValueChange = { value ->
                if (value.length <= V1_SUPPORT_NOTE_MAX_CHARS &&
                    value.none { it.isISOControl() && it != '\n' && it != '\t' }
                ) {
                    problemNote = value
                    statusMessage = null
                }
            },
            modifier = Modifier.fillMaxWidth(),
            label = { Text("Problem description (optional)") },
            supportingText = { Text("${problemNote.length} / $V1_SUPPORT_NOTE_MAX_CHARS") },
            minLines = 3,
            maxLines = 8,
        )
        Text("Preview")
        SelectionContainer {
            Text(report?.text ?: "The report cannot be generated from the current note.")
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                enabled = report != null,
                onClick = {
                    pendingSaveText = report?.text
                    statusMessage = null
                    saveLauncher.launch(report?.fileName ?: return@Button)
                },
            ) {
                Text("Save text file")
            }
            Button(
                enabled = report != null,
                onClick = {
                    val currentReport = report ?: return@Button
                    statusMessage = shareSupportReport(context, currentReport)
                },
            ) {
                Text("Share / Email")
            }
        }
        statusMessage?.let { Text(it) }
    }
}

internal fun shareSupportReport(
    context: Context,
    report: V1SupportReport,
): String {
    val result = runCatching {
        val directory = File(context.cacheDir, "support-reports").apply {
            check(isDirectory || mkdirs())
        }
        val existingFiles = requireNotNull(directory.listFiles())
        val plan = V1SupportShareRetentionPolicy.plan(
            entries = existingFiles.map {
                V1SupportCachedReport(it.name, it.lastModified(), it.isFile)
            },
            nowEpochMillis = System.currentTimeMillis(),
        )
        when (plan) {
            is V1SupportShareRetentionPlan.CapacityReached -> {
                plan.expiredNames.forEach { name -> check(File(directory, name).delete()) }
                return "Eight recent reports are still shareable. Save this report instead."
            }
            V1SupportShareRetentionPlan.UnsafeDirectoryEntry -> error("Unsafe support cache state")
            is V1SupportShareRetentionPlan.Accepted -> plan.expiredNames.forEach { name ->
                check(File(directory, name).delete())
            }
        }
        val reportFile = File.createTempFile(
            V1_SUPPORT_SHARED_REPORT_PREFIX,
            V1_SUPPORT_SHARED_REPORT_SUFFIX,
            directory,
        )
        reportFile.writeText(report.text, Charsets.UTF_8)
        val uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.support-files",
            reportFile,
        )
        val sendIntent = Intent(Intent.ACTION_SEND).apply {
            type = report.mimeType
            putExtra(Intent.EXTRA_SUBJECT, "Trail support report")
            putExtra(Intent.EXTRA_STREAM, uri)
            clipData = ClipData.newUri(context.contentResolver, report.fileName, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        context.startActivity(Intent.createChooser(sendIntent, "Share support report"))
        "Share options opened."
    }
    return result.getOrElse { "Support report could not be shared." }
}
