package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

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
}
