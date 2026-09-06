package io.github.nbjelanovic.otclient

import android.content.ClipData
import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import java.io.File

internal fun shareV1TestConnectionLog(
    context: Context,
    report: V1SupportReport,
): String {
    val result = runCatching {
        val directory = File(context.cacheDir, "support-reports").apply {
            check(isDirectory || mkdirs())
        }
        val existingFiles = requireNotNull(directory.listFiles())
        val plan = V1TestLogShareRetentionPolicy.plan(
            entries = existingFiles.map {
                V1TestLogCachedReport(it.name, it.lastModified(), it.isFile)
            },
            nowEpochMillis = System.currentTimeMillis(),
        )
        when (plan) {
            is V1TestLogShareRetentionPlan.CapacityReached -> {
                plan.expiredNames.forEach { name -> check(File(directory, name).delete()) }
                return "Eight recent reports are still shareable. Save this report instead."
            }
            V1TestLogShareRetentionPlan.UnsafeDirectoryEntry -> error("Unsafe support cache state")
            is V1TestLogShareRetentionPlan.Accepted -> plan.expiredNames.forEach { name ->
                check(File(directory, name).delete())
            }
        }
        val reportFile = File.createTempFile(
            V1_TEST_LOG_SHARED_REPORT_PREFIX,
            V1_TEST_LOG_SHARED_REPORT_SUFFIX,
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
