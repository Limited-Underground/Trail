package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.TextLayoutResult
import org.junit.rules.RuleChain
import org.junit.rules.TestRule
import org.junit.runners.model.Statement
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.ParameterizedRobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import org.robolectric.annotation.LooperMode

/** Host Compose check that each language's translated text shows without clipping on a 360 dp phone. */
@RunWith(ParameterizedRobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h640dp-port-xxhdpi")
// Real text measurement; the default legacy graphics mode does not measure text width.
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@LooperMode(LooperMode.Mode.PAUSED)
class AppLanguageLayoutTest(private val qualifier: String) {
    private val compose = createComposeRule()
    // The language is set before the compose host activity starts, as on a phone set to it.
    private val language = TestRule { base, _ ->
        object : Statement() {
            override fun evaluate() {
                RuntimeEnvironment.setQualifiers("+$qualifier")
                base.evaluate()
            }
        }
    }
    @get:Rule val composeHost: RuleChain = RuleChain.outerRule(language).around(withRobolectricComposeHost(compose))

    companion object {
        @JvmStatic
        @ParameterizedRobolectricTestRunner.Parameters(name = "{0}")
        fun languages(): List<Array<String>> =
            listOf("en", "es", "it", "fr", "de", "hr", "b+sr+Latn", "ru", "uk").map { arrayOf(it) }
    }

    private fun text(id: Int): String = RuntimeEnvironment.getApplication().getString(id)

    private fun assertShownWhole(label: String, maxLines: Int = Int.MAX_VALUE) {
        val node = compose.onNodeWithText(label, useUnmergedTree = true).assertIsDisplayed()
        val layouts = mutableListOf<TextLayoutResult>()
        node.fetchSemanticsNode().config[SemanticsActions.GetTextLayoutResult].action?.invoke(layouts)
        val layout = layouts.single()
        val last = layout.lineCount - 1
        // Whole pixels: the measured text width is a float, the laid-out size is rounded.
        val textWidth = (0..last).maxOf { layout.getLineRight(it) - layout.getLineLeft(it) }
        val clipped = textWidth > layout.size.width + 1f || layout.didOverflowHeight ||
            layout.isLineEllipsized(last) || layout.getLineEnd(last, visibleEnd = true) < label.length
        assertFalse("'$label' is clipped in $qualifier: text=$textWidth box=${layout.size} " +
            "lines=${layout.lineCount} h=${layout.didOverflowHeight} ell=${layout.isLineEllipsized(last)} " +
            "end=${layout.getLineEnd(last, visibleEnd = true)}/${label.length}", clipped)
        assertTrue("'$label' wraps onto ${layout.lineCount} lines in $qualifier (max $maxLines)",
            layout.lineCount <= maxLines)
        val bounds = node.fetchSemanticsNode().boundsInRoot
        val screenWidth = RuntimeEnvironment.getApplication().resources.displayMetrics.widthPixels
        assertTrue("'$label' runs off the screen in $qualifier", bounds.left >= 0f && bounds.right <= screenWidth + 1f)
    }

    @Test fun navigationAndAppSettingsFit() {
        val tabs = listOf(R.string.nav_messages, R.string.nav_group, R.string.nav_device).map(::text)
        val labels = listOf(R.string.settings_app_title, R.string.settings_show_build_label, R.string.settings_language,
            R.string.settings_language_system_default).map(::text)
        compose.setContent {
            MaterialTheme {
                V1AppShell(V1AppNavigationState.empty().select(V1AppDestination.DEVICE), {}, messages = {}, group = {},
                    device = { AppDisplaySettings(showBuildLabel = true, onShowBuildLabelChanged = {}, saveFailed = true) })
            }
        }
        // Tab labels must stay on one line; settings text may wrap.
        tabs.forEach { assertShownWhole(it, maxLines = 1) }
        labels.forEach { assertShownWhole(it) }
        assertShownWhole(text(R.string.settings_save_failed))
    }

    @Test fun languageChoiceListsEveryLanguageInItsOwnName() {
        val language = text(R.string.settings_language)
        compose.setContent {
            MaterialTheme { AppDisplaySettings(showBuildLabel = true, onShowBuildLabelChanged = {}, saveFailed = false) }
        }
        compose.onNodeWithText(language).performClick()
        compose.waitForIdle()
        assertShownWhole(text(R.string.settings_language_choose))
        AppLanguage.entries.forEach { assertShownWhole(it.nativeName, maxLines = 1) }
        compose.onNodeWithText(text(R.string.action_cancel)).performClick()
        compose.waitForIdle()
        compose.onNodeWithText(text(R.string.settings_language_choose)).assertDoesNotExist()
    }
}
