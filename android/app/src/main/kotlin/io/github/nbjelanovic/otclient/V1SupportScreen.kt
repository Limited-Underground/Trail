package io.github.nbjelanovic.otclient

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
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
import androidx.compose.material3.TextButton
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
    deviceReading: BleDeviceParameterReading? = null,
) {
    val context = LocalContext.current
    var problemNote by rememberSaveable { mutableStateOf("") }
    var readingsVisible by rememberSaveable { mutableStateOf(false) }
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
            statusMessage = if (saveV1TextDocument(context, destination, text)) {
                "Support report saved."
            } else {
                "Support report could not be saved."
            }
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
        if (deviceReading != null) {
            TextButton(onClick = { readingsVisible = !readingsVisible }) {
                Text(if (readingsVisible) "Hide device readings" else "Show device readings")
            }
            if (readingsVisible) {
                val readings = remember(deviceReading) { meshTrailParameterEntries(deviceReading.snapshot) }
                for ((kind, values) in readings) {
                    Text(kind.label + "\n" + values)
                }
            }
        }
        MeshTrailLogExportAction()
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

internal fun saveV1TextDocument(context: Context, destination: Uri, text: String): Boolean = runCatching {
    val output = requireNotNull(context.contentResolver.openOutputStream(destination, "wt"))
    output.bufferedWriter(Charsets.UTF_8).use { it.write(text) }
}.isSuccess

internal fun shareSupportReport(
    context: Context,
    report: V1SupportReport,
): String = when (shareV1TextAttachment(
    context, report, subject = "MeshTrail support report", chooserTitle = "Share support report",
)) {
    V1TextShareResult.OPENED -> "Share options opened."
    V1TextShareResult.AT_CAPACITY -> "Eight recent reports are still shareable. Save this report instead."
    V1TextShareResult.FAILED -> "Support report could not be shared."
}

internal enum class V1TextShareResult { OPENED, AT_CAPACITY, FAILED }

internal fun shareV1TextAttachment(
    context: Context,
    report: V1SupportReport,
    subject: String,
    chooserTitle: String,
    displayName: String? = null,
): V1TextShareResult = runCatching {
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
    val expiredNames = when (plan) {
        is V1SupportShareRetentionPlan.CapacityReached -> plan.expiredNames
        is V1SupportShareRetentionPlan.UnsafeDirectoryEntry -> error("Unsafe support cache state")
        is V1SupportShareRetentionPlan.Accepted -> plan.expiredNames
    }
    expiredNames.forEach { name -> check(File(directory, name).delete()) }
    if (plan is V1SupportShareRetentionPlan.CapacityReached) return@runCatching V1TextShareResult.AT_CAPACITY

    val reportFile = File.createTempFile(
        V1_SUPPORT_SHARED_REPORT_PREFIX,
        V1_SUPPORT_SHARED_REPORT_SUFFIX,
        directory,
    )
    var handoffAttempted = false
    try {
        reportFile.writeText(report.text, Charsets.UTF_8)
        val authority = "${context.packageName}.support-files"
        val uri = if (displayName == null) {
            FileProvider.getUriForFile(context, authority, reportFile)
        } else {
            FileProvider.getUriForFile(context, authority, reportFile, displayName)
        }
        val sendIntent = Intent(Intent.ACTION_SEND).apply {
            type = report.mimeType
            putExtra(Intent.EXTRA_SUBJECT, subject)
            putExtra(Intent.EXTRA_STREAM, uri)
            clipData = ClipData.newUri(context.contentResolver, report.fileName, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val chooser = Intent.createChooser(sendIntent, chooserTitle)
        // Dispatch may succeed before startActivity throws; retain that report on uncertainty.
        handoffAttempted = true
        context.startActivity(chooser)
        V1TextShareResult.OPENED
    } finally {
        // Only this newly created, never-offered report is eligible for failure cleanup.
        if (!handoffAttempted) runCatching { reportFile.delete() }
    }
}.getOrDefault(V1TextShareResult.FAILED)
