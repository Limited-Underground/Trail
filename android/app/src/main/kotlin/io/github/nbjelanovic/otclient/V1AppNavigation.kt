package io.github.nbjelanovic.otclient

enum class V1AppDestination { MESSAGES, GROUP, DEVICE }

enum class V1AppNavigationLayout { PORTRAIT_BOTTOM_BAR, LANDSCAPE_NAVIGATION_RAIL }

data class V1Viewport(val widthDp: Int, val heightDp: Int) {
    init {
        require(widthDp > 0)
        require(heightDp > 0)
    }
}

object V1AppLayoutPolicy {
    fun navigationFor(viewport: V1Viewport): V1AppNavigationLayout =
        if (viewport.widthDp > viewport.heightDp) {
            V1AppNavigationLayout.LANDSCAPE_NAVIGATION_RAIL
        } else {
            V1AppNavigationLayout.PORTRAIT_BOTTOM_BAR
        }
}

sealed interface V1MessageDraftRoute {
    data class Group(val groupToken: V1GroupToken) : V1MessageDraftRoute
    data class Direct(val peerToken: V1DirectPeerToken) : V1MessageDraftRoute
}

class V1AppNavigationState private constructor(
    val selectedDestination: V1AppDestination,
    drafts: Map<V1MessageDraftRoute, String>,
) {
    private val drafts = drafts.toMap()

    fun select(destination: V1AppDestination): V1AppNavigationState =
        V1AppNavigationState(destination, drafts)

    fun draftFor(route: V1MessageDraftRoute): String = drafts[route].orEmpty()

    fun updateDraft(route: V1MessageDraftRoute, value: String): V1AppNavigationState? {
        if (value.length > V1_NAVIGATION_DRAFT_MAX_CHARS) return null
        if (value.any { it.isISOControl() && it != '\n' && it != '\t' }) return null
        val updated = if (value.isEmpty()) drafts - route else drafts + (route to value)
        return V1AppNavigationState(selectedDestination, updated)
    }

    override fun toString(): String =
        "V1AppNavigationState(selectedDestination=$selectedDestination, draftCount=${drafts.size}, drafts=redacted)"

    companion object {
        fun empty(): V1AppNavigationState = V1AppNavigationState(V1AppDestination.MESSAGES, emptyMap())
    }
}

internal const val V1_NAVIGATION_DRAFT_MAX_CHARS = 500
