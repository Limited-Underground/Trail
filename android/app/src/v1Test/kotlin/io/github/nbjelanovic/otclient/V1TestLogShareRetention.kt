package io.github.nbjelanovic.otclient

internal const val V1_TEST_LOG_SHARED_REPORT_MAX_FILES = 8
internal const val V1_TEST_LOG_SHARED_REPORT_RETENTION_MILLIS = 24L * 60L * 60L * 1_000L
internal const val V1_TEST_LOG_SHARED_REPORT_PREFIX = "trail-support-"
internal const val V1_TEST_LOG_SHARED_REPORT_SUFFIX = ".txt"

internal data class V1TestLogCachedReport(
    val name: String,
    val lastModifiedMillis: Long,
    val isRegularFile: Boolean,
)

internal sealed interface V1TestLogShareRetentionPlan {
    data class Accepted(val expiredNames: Set<String>) : V1TestLogShareRetentionPlan
    data class CapacityReached(val expiredNames: Set<String>) : V1TestLogShareRetentionPlan
    data object UnsafeDirectoryEntry : V1TestLogShareRetentionPlan
}

internal object V1TestLogShareRetentionPolicy {
    fun plan(
        entries: List<V1TestLogCachedReport>,
        nowEpochMillis: Long,
    ): V1TestLogShareRetentionPlan {
        if (nowEpochMillis < 0) return V1TestLogShareRetentionPlan.UnsafeDirectoryEntry
        val expired = mutableSetOf<String>()
        var recentCount = 0
        entries.forEach { entry ->
            if (!entry.isRegularFile ||
                !entry.name.startsWith(V1_TEST_LOG_SHARED_REPORT_PREFIX) ||
                !entry.name.endsWith(V1_TEST_LOG_SHARED_REPORT_SUFFIX) ||
                entry.lastModifiedMillis < 0
            ) {
                return V1TestLogShareRetentionPlan.UnsafeDirectoryEntry
            }
            val age = nowEpochMillis - entry.lastModifiedMillis
            if (age >= V1_TEST_LOG_SHARED_REPORT_RETENTION_MILLIS && age >= 0) {
                expired += entry.name
            } else {
                recentCount += 1
            }
        }
        return if (recentCount >= V1_TEST_LOG_SHARED_REPORT_MAX_FILES) {
            V1TestLogShareRetentionPlan.CapacityReached(expired)
        } else {
            V1TestLogShareRetentionPlan.Accepted(expired)
        }
    }
}
