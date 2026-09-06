package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

class V1TestLogShareRetentionTest {
    @Test
    fun keepsRecentAttachmentsAndRemovesOnlyExpiredReports() {
        val now = V1_TEST_LOG_SHARED_REPORT_RETENTION_MILLIS * 2
        val plan = assertIs<V1TestLogShareRetentionPlan.Accepted>(
            V1TestLogShareRetentionPolicy.plan(
                listOf(
                    report("old", now - V1_TEST_LOG_SHARED_REPORT_RETENTION_MILLIS),
                    report("recent", now - V1_TEST_LOG_SHARED_REPORT_RETENTION_MILLIS + 1),
                    report("future", now + 1),
                ),
                now,
            ),
        )
        assertEquals(setOf("${V1_TEST_LOG_SHARED_REPORT_PREFIX}old.txt"), plan.expiredNames)
    }

    @Test
    fun refusesNinthRecentAttachmentWithoutDeletingTheLiveEight() {
        val entries = List(V1_TEST_LOG_SHARED_REPORT_MAX_FILES) { report(it.toString(), 100) }
        val plan = assertIs<V1TestLogShareRetentionPlan.CapacityReached>(
            V1TestLogShareRetentionPolicy.plan(entries, 101),
        )
        assertTrue(plan.expiredNames.isEmpty())
    }

    @Test
    fun expiredReportsDoNotConsumeCapacity() {
        val now = V1_TEST_LOG_SHARED_REPORT_RETENTION_MILLIS
        val entries = List(V1_TEST_LOG_SHARED_REPORT_MAX_FILES) { report(it.toString(), 0) }
        val plan = assertIs<V1TestLogShareRetentionPlan.Accepted>(
            V1TestLogShareRetentionPolicy.plan(entries, now),
        )
        assertEquals(V1_TEST_LOG_SHARED_REPORT_MAX_FILES, plan.expiredNames.size)
    }

    @Test
    fun refusesUnknownFilesDirectoriesAndInvalidTime() {
        listOf(
            V1TestLogCachedReport("other.txt", 0, true),
            V1TestLogCachedReport("${V1_TEST_LOG_SHARED_REPORT_PREFIX}nested.txt", 0, false),
            V1TestLogCachedReport("${V1_TEST_LOG_SHARED_REPORT_PREFIX}bad.txt", -1, true),
        ).forEach { entry ->
            assertIs<V1TestLogShareRetentionPlan.UnsafeDirectoryEntry>(
                V1TestLogShareRetentionPolicy.plan(listOf(entry), 1),
            )
        }
        assertIs<V1TestLogShareRetentionPlan.UnsafeDirectoryEntry>(
            V1TestLogShareRetentionPolicy.plan(emptyList(), -1),
        )
    }

    @Test
    fun generatedNameBoundaryIsExact() {
        val accepted = V1TestLogShareRetentionPolicy.plan(listOf(report("123", 0)), 1)
        assertTrue(accepted is V1TestLogShareRetentionPlan.Accepted)
    }

    private fun report(marker: String, modified: Long) = V1TestLogCachedReport(
        name = "$V1_TEST_LOG_SHARED_REPORT_PREFIX$marker$V1_TEST_LOG_SHARED_REPORT_SUFFIX",
        lastModifiedMillis = modified,
        isRegularFile = true,
    )
}
