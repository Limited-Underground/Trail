package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class V1QuickMessagesTest {
    @Test fun normalizesAndRejectsInvalidTemplates() {
        assertEquals("Checking in", V1QuickMessages.normalize("  Checking in  "))
        assertNull(V1QuickMessages.normalize("\n"))
        assertNull(V1QuickMessages.normalize("a\nb"))
        assertNull(V1QuickMessages.normalize("x".repeat(161)))
    }
    @Test fun boundsAndDeduplicates() {
        assertNull(V1QuickMessages.add(List(12) { "Message $it" }, "Another"))
        assertNull(V1QuickMessages.add(listOf("Hello"), " Hello "))
        assertEquals(listOf("Hello"), V1QuickMessages.add(emptyList(), "Hello"))
    }
    @Test fun movesWithoutLosingEntries() {
        val original = listOf("A", "B", "C")
        assertEquals(listOf("B", "A", "C"), V1QuickMessages.move(original, 1, -1))
        assertEquals(original, V1QuickMessages.move(original, 0, -1))
        assertEquals(original, V1QuickMessages.move(original, 3, -1))
        assertEquals(original, V1QuickMessages.move(original, 0, 2))
    }

    @Test fun editsInPlaceAtCapacityWithoutLosingOrderOrTheOriginalList() {
        val original = List(12) { "Message $it" }
        val edited = assertNotNull(V1QuickMessages.edit(original, 7, "Message 7", "  Updated  "))
        assertEquals(List(12) { if (it == 7) "Updated" else "Message $it" }, edited)
        assertEquals(List(12) { "Message $it" }, original)
        assertEquals(edited, V1QuickMessages.edit(edited, 7, "Updated", "Updated"))
    }

    @Test fun refusedEditsPreserveDataAndOrder() {
        val original = listOf("A", "B", "C")
        for (value in listOf("", "  ", "A", " A ", "a\nb", "x".repeat(161))) {
            assertNull(V1QuickMessages.edit(original, 1, "B", value), value)
            assertEquals(listOf("A", "B", "C"), original)
        }
        assertEquals(listOf("A", "x".repeat(160), "C"),
            V1QuickMessages.edit(original, 1, "B", "x".repeat(160)))
    }

    @Test fun staleIndexValueOrReorderCannotEditAnotherTemplate() {
        val current = listOf("B", "A", "C")
        assertNull(V1QuickMessages.edit(current, 1, "B", "Updated"))
        assertNull(V1QuickMessages.edit(current, -1, "B", "Updated"))
        assertNull(V1QuickMessages.edit(current, 3, "C", "Updated"))
        assertNull(V1QuickMessages.edit(current, 1, "Old A", "Updated"))
        assertNull(V1QuickMessages.edit(List(13) { "M$it" }, 0, "M0", "Updated"))
        assertEquals(listOf("B", "A", "C"), current)
    }

    @Test fun loadRetainsTheNumberedKeyNormalizationAndOrderPolicy() {
        val keys = mapOf(0 to " B ", 1 to "A", 2 to "B", 3 to "bad\nvalue", 11 to "Last")
        val reads = mutableListOf<Int>()
        assertEquals(listOf("B", "A", "Last"), V1QuickMessages.load { reads += it; keys[it] })
        assertEquals((0 until 12).toList(), reads)
        assertEquals(emptyList(), V1QuickMessages.load { null })
    }

    @Test fun loadFailureIsDistinctFromAnEmptyStoreAndDoesNotRetry() {
        var reads = 0
        assertNull(V1QuickMessages.load { reads++; throw IllegalStateException("synthetic read") })
        assertEquals(1, reads)
    }

    @Test fun editedAndReorderedValuesSurviveSaveAndFreshReload() {
        val persisted = mutableMapOf<Int, String>()
        fun write(values: List<String>): Boolean {
            persisted.clear()
            values.forEachIndexed { index, value -> persisted[index] = value }
            return true
        }
        val original = listOf("A", "B", "C")
        assertTrue(V1QuickMessages.save(original, ::write))
        val edited = assertNotNull(V1QuickMessages.edit(
            assertNotNull(V1QuickMessages.load(persisted::get)), 1, "B", "Updated"))
        assertTrue(V1QuickMessages.save(edited, ::write))
        val moved = V1QuickMessages.move(edited, 2, -1)
        assertTrue(V1QuickMessages.save(moved, ::write))
        assertEquals(listOf("A", "C", "Updated"), V1QuickMessages.load(persisted::get))
        assertEquals(original, listOf("A", "B", "C"))
        assertEquals(listOf(0, 1, 2), persisted.keys.toList())
    }

    @Test fun invalidSaveDoesNotReachPersistence() {
        var writes = 0
        for (values in listOf(List(13) { "M$it" }, listOf("A", "A"), listOf(" A "),
            listOf(""), listOf("bad\nvalue"), listOf("x".repeat(161)))) {
            assertFalse(V1QuickMessages.save(values) { writes++; true })
        }
        assertEquals(0, writes)
    }

    @Test fun unsuccessfulSaveIsReportedOnceWithoutMutatingSubmittedValues() {
        val original = mutableListOf("A", "B")
        var writes = 0
        assertFalse(V1QuickMessages.save(original) { writes++; false })
        assertEquals(1, writes)
        writes = 0
        assertFalse(V1QuickMessages.save(original) { writes++; throw IllegalStateException("synthetic write") })
        assertEquals(1, writes)
        assertEquals(listOf("A", "B"), original)
    }

    @Test fun persistenceReceivesADefensiveSnapshotAndEmptySaveIsExplicit() {
        val original = mutableListOf("A", "B")
        assertTrue(V1QuickMessages.save(original) { snapshot ->
            original.clear()
            assertEquals(listOf("A", "B"), snapshot)
            true
        })
        var writes = 0
        assertTrue(V1QuickMessages.save(emptyList()) { snapshot ->
            writes++
            assertTrue(snapshot.isEmpty())
            true
        })
        assertEquals(1, writes)
    }

    @Test fun uncertainWriteMayLandWithoutBeingRetriedOrClaimedSuccessful() {
        val original = listOf("A", "B")
        val edited = assertNotNull(V1QuickMessages.edit(original, 1, "B", "Updated"))
        var landed = original
        var writes = 0
        assertFalse(V1QuickMessages.save(edited) { snapshot ->
            writes++
            landed = snapshot
            false
        })
        assertEquals(1, writes)
        assertEquals(listOf("A", "B"), original)
        assertEquals(listOf("A", "Updated"), landed)
    }
}
