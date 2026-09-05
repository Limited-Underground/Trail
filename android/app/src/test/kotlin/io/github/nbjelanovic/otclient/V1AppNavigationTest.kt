package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class V1AppNavigationTest {
    @Test
    fun messagesIsTheDefaultDestination() {
        assertEquals(V1AppDestination.MESSAGES, V1AppNavigationState.empty().selectedDestination)
    }

    @Test
    fun portraitUsesBottomBarAndLandscapeUsesNavigationRail() {
        assertEquals(V1AppNavigationLayout.PORTRAIT_BOTTOM_BAR, V1AppLayoutPolicy.navigationFor(V1Viewport(412, 915)))
        assertEquals(V1AppNavigationLayout.LANDSCAPE_NAVIGATION_RAIL, V1AppLayoutPolicy.navigationFor(V1Viewport(915, 412)))
    }

    @Test
    fun destinationChangesDoNotDiscardTheMessageDraft() {
        val route = groupRoute(3)
        val drafted = assertNotNull(V1AppNavigationState.empty().updateDraft(route, "Meet at the trailhead"))
        val device = drafted.select(V1AppDestination.GROUP).select(V1AppDestination.DEVICE)
        assertEquals("Meet at the trailhead", device.draftFor(route))
        assertEquals("", device.draftFor(directRoute(4)))
        assertEquals(false, device.toString().contains("trailhead"))
    }

    @Test
    fun invalidViewportAndUnsafeOrOversizeDraftFailClosed() {
        assertFailsWith<IllegalArgumentException> { V1Viewport(0, 100) }
        val route = groupRoute(3)
        assertNull(V1AppNavigationState.empty().updateDraft(route, "bad\u0000draft"))
        assertNull(V1AppNavigationState.empty().updateDraft(route, "x".repeat(V1_NAVIGATION_DRAFT_MAX_CHARS + 1)))
    }

    private fun groupRoute(marker: Int) = V1MessageDraftRoute.Group(
        requireNotNull(V1GroupToken.create(ByteArray(V1GroupToken.SIZE_BYTES) { marker.toByte() })),
    )

    private fun directRoute(marker: Int) = V1MessageDraftRoute.Direct(
        requireNotNull(V1DirectPeerToken.create(ByteArray(V1DirectPeerToken.SIZE_BYTES) { marker.toByte() })),
    )
}
