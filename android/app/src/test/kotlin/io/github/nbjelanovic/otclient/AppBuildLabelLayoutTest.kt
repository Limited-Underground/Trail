package io.github.nbjelanovic.otclient

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.requiredWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.unit.dp
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode

/** Host Compose geometry, not phone/screenshot or physical UI acceptance. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h800dp-mdpi")
@LooperMode(LooperMode.Mode.PAUSED)
class AppBuildLabelLayoutTest {
    @get:Rule val compose = createComposeRule()
    private val label = "1.0.0-dev · f3aeb77-dirty · debug"

    private fun render(widthDp: Int, shown: Boolean = true) {
        compose.setContent {
            MaterialTheme {
                Box(Modifier.requiredWidth(widthDp.dp)) {
                    V1StatusStrip(TrailAppUiState.ChooseMode, shown, label)
                }
            }
        }
    }

    @Test fun narrow360DpKeepsBuildAndExistingIconsInsideTheStrip() {
        render(360)
        assertLayout(360f)
    }

    // 480 dp at density 3 is 1440 px; the task supplies pixels, not a phone density.
    @Test @Config(qualifiers = "w480dp-h900dp-xxhdpi")
    fun s24Width1440PixelsKeepsBuildAndExistingIconsInsideTheStrip() {
        render(480)
        assertLayout(1440f)
    }

    @Test fun hiddenBuildRemovesOnlyTheLabel() {
        render(360, shown = false)
        compose.onNodeWithContentDescription("App build $label").assertDoesNotExist()
        statusIcons().forEach { it.assertIsDisplayed() }
    }

    private fun statusIcons() = listOf(
        compose.onNodeWithContentDescription("Bluetooth disconnected"),
        compose.onNodeWithContentDescription("Heltec GPS unknown; satellites unavailable"),
        compose.onNodeWithContentDescription("Heltec battery unavailable"),
        compose.onNodeWithContentDescription("Heltec transmit unavailable; receive unavailable"),
    )

    private fun assertLayout(widthPixels: Float) {
        val build = compose.onNodeWithContentDescription("App build $label").assertIsDisplayed()
        val nodes = statusIcons()
        val buildBounds = build.fetchSemanticsNode().boundsInRoot
        // Below every status icon, so a top-edge camera cutout cannot cover it.
        for (node in nodes) {
            assertTrue("build label must sit below the icon row",
                buildBounds.top >= node.fetchSemanticsNode().boundsInRoot.bottom)
        }
        assertTrue(buildBounds.left >= 0f && buildBounds.right <= widthPixels)
        for (node in nodes) {
            val bounds = node.assertIsDisplayed().fetchSemanticsNode().boundsInRoot
            assertTrue(bounds.left >= 0f && bounds.right <= widthPixels)
        }
        val results = mutableListOf<TextLayoutResult>()
        build.performSemanticsAction(SemanticsActions.GetTextLayoutResult) { it(results) }
        assertEquals(1, results.size)
        assertFalse("The normal build label must fit without ellipsis", results.single().hasVisualOverflow)
    }
}
