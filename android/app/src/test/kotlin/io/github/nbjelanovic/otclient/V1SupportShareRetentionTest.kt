package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

class V1SupportShareRetentionTest {
    @Test
    fun keepsRecentAttachmentsAndRemovesOnlyExpiredReports() {
        val now = V1_SUPPORT_SHARED_REPORT_RETENTION_MILLIS * 2
        val plan = assertIs<V1SupportShareRetentionPlan.Accepted>(
            V1SupportShareRetentionPolicy.plan(
                listOf(
                    report("old", now - V1_SUPPORT_SHARED_REPORT_RETENTION_MILLIS),
                    report("recent", now - V1_SUPPORT_SHARED_REPORT_RETENTION_MILLIS + 1),
                    report("future", now + 1),
                ),
                now,
            ),
        )
        assertEquals(setOf("${V1_SUPPORT_SHARED_REPORT_PREFIX}old.txt"), plan.expiredNames)
    }

    @Test
    fun refusesNinthRecentAttachmentWithoutDeletingTheLiveEight() {
        val entries = List(V1_SUPPORT_SHARED_REPORT_MAX_FILES) { report(it.toString(), 100) }
        val plan = assertIs<V1SupportShareRetentionPlan.CapacityReached>(
            V1SupportShareRetentionPolicy.plan(entries, 101),
        )
        assertTrue(plan.expiredNames.isEmpty())
    }

    @Test
    fun expiredReportsDoNotConsumeCapacity() {
        val now = V1_SUPPORT_SHARED_REPORT_RETENTION_MILLIS
        val entries = List(V1_SUPPORT_SHARED_REPORT_MAX_FILES) { report(it.toString(), 0) }
        val plan = assertIs<V1SupportShareRetentionPlan.Accepted>(
            V1SupportShareRetentionPolicy.plan(entries, now),
        )
        assertEquals(V1_SUPPORT_SHARED_REPORT_MAX_FILES, plan.expiredNames.size)
    }

    @Test
    fun refusesUnknownFilesDirectoriesAndInvalidTime() {
        listOf(
            V1SupportCachedReport("other.txt", 0, true),
            V1SupportCachedReport("${V1_SUPPORT_SHARED_REPORT_PREFIX}nested.txt", 0, false),
            V1SupportCachedReport("${V1_SUPPORT_SHARED_REPORT_PREFIX}bad.txt", -1, true),
        ).forEach { entry ->
            assertIs<V1SupportShareRetentionPlan.UnsafeDirectoryEntry>(
                V1SupportShareRetentionPolicy.plan(listOf(entry), 1),
            )
        }
        assertIs<V1SupportShareRetentionPlan.UnsafeDirectoryEntry>(
            V1SupportShareRetentionPolicy.plan(emptyList(), -1),
        )
    }

    @Test
    fun generatedNameBoundaryIsExact() {
        val accepted = V1SupportShareRetentionPolicy.plan(listOf(report("123", 0)), 1)
        assertTrue(accepted is V1SupportShareRetentionPlan.Accepted)
    }

    private fun report(marker: String, modified: Long) = V1SupportCachedReport(
        name = "$V1_SUPPORT_SHARED_REPORT_PREFIX$marker$V1_SUPPORT_SHARED_REPORT_SUFFIX",
        lastModifiedMillis = modified,
        isRegularFile = true,
    )
}
