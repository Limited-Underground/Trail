package io.github.nbjelanovic.otclient

import android.content.Context

private const val MESH_TRAIL_LOG_ATTACHMENT_MAX_BYTES = 128 * 1024
private const val MESH_TRAIL_LOG_ATTACHMENT_NAME = "MeshTrail-operation-log.txt"

/** Call from the share action with the text rendered from the closed operation-log schema. */
internal fun shareMeshTrailLog(context: Context, text: String): Boolean {
    if (text.isBlank() || text.length > MESH_TRAIL_LOG_ATTACHMENT_MAX_BYTES ||
        text.toByteArray(Charsets.UTF_8).size > MESH_TRAIL_LOG_ATTACHMENT_MAX_BYTES
    ) return false

    return shareV1TextAttachment(
        context,
        V1SupportReport(MESH_TRAIL_LOG_ATTACHMENT_NAME, V1_SUPPORT_REPORT_MIME_TYPE, text),
        subject = "MeshTrail operation log",
        chooserTitle = "Share operation log",
        displayName = MESH_TRAIL_LOG_ATTACHMENT_NAME,
    ) == V1TextShareResult.OPENED
}
