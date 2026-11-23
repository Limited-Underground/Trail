package io.github.nbjelanovic.otclient

import android.content.Context

private const val MESH_TRAIL_LOG_ATTACHMENT_MAX_BYTES = 128 * 1024
private const val MESH_TRAIL_LOG_ATTACHMENT_NAME = "MeshTrail-operation-log.txt"

/** Prepare only text rendered from the closed operation-log schema. */
internal fun meshTrailLogAttachment(text: String): V1SupportReport? {
    if (text.isBlank() || text.length > MESH_TRAIL_LOG_ATTACHMENT_MAX_BYTES ||
        text.toByteArray(Charsets.UTF_8).size > MESH_TRAIL_LOG_ATTACHMENT_MAX_BYTES
    ) return null

    return V1SupportReport(MESH_TRAIL_LOG_ATTACHMENT_NAME, V1_SUPPORT_REPORT_MIME_TYPE, text)
}

internal fun shareMeshTrailLog(context: Context, text: String): Boolean {
    val report = meshTrailLogAttachment(text) ?: return false
    return shareV1TextAttachment(
        context,
        report,
        subject = "MeshTrail operation log",
        chooserTitle = "Share operation log",
        displayName = report.fileName,
    ) == V1TextShareResult.OPENED
}
