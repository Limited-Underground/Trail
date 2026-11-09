package io.github.nbjelanovic.otclient

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.AnnotatedString
import java.io.File
import org.junit.Assert.*
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.util.ReflectionHelpers
import org.robolectric.util.ReflectionHelpers.ClassParameter

/** Actual composed editor and Android-35 filesystem failures; no device/process-death claim. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w411dp-h891dp")
class V1EditorWriteFailureTest {
    @get:Rule val ui = createComposeRule()
    private val preferences get() = RuntimeEnvironment.getApplication()
        .getSharedPreferences("v1-local-templates", Context.MODE_PRIVATE)
    private lateinit var preferenceFile: File

    @Before fun emptyHostFixture() {
        val dataDirectory = File(requireNotNull(RuntimeEnvironment.getApplication().applicationInfo.dataDir))
        val temporaryRoot = File(requireNotNull(System.getProperty("java.io.tmpdir"))).canonicalFile.toPath()
        check(dataDirectory.canonicalFile.toPath().startsWith(temporaryRoot))
        assertEquals("android.app.SharedPreferencesImpl", preferences.javaClass.name)
        check(preferences.edit().clear().commit())
        preferenceFile = File(dataDirectory, "shared_prefs/v1-local-templates.xml")
    }

    private fun show() = ui.setContent {
        MaterialTheme {
            V1MessagesScreen("No device connected", onConnect = {}, initiallyEditing = true)
        }
    }

    private fun field() = ui.onNode(hasSetTextAction())

    private fun assertDraft(value: String) = field().assert(
        SemanticsMatcher.expectValue(SemanticsProperties.EditableText, AnnotatedString(value)),
    )

    private fun assertUncertainSave() = ui.onNodeWithText(
        "Save could not be confirmed. Your draft is still here; saved state is uncertain.",
    ).assertExists()

    /** Identical real blocked-parent mechanism to the accepted storage test, within this fixture only. */
    private fun withBlockedPreferenceDirectory(action: () -> Unit) {
        val parent = requireNotNull(preferenceFile.parentFile)
        val retained = File(parent.parentFile, "ot0313-ui-retained-shared-prefs")
        check(parent.isDirectory && !retained.exists())
        check(parent.renameTo(retained))
        try {
            check(parent.createNewFile())
            action()
        } finally {
            if (parent.isFile) check(parent.delete())
            check(retained.renameTo(parent))
        }
    }

    /** Parse durable XML with a distinct actual framework instance, bypassing only its process cache. */
    private fun durableValues(): Map<String, *> = ReflectionHelpers.callConstructor(
        Class.forName("android.app.SharedPreferencesImpl").asSubclass(SharedPreferences::class.java),
        ClassParameter.from(File::class.java, preferenceFile),
        ClassParameter.from(Int::class.javaPrimitiveType!!, Context.MODE_PRIVATE),
    ).all

    @Test fun failedAddKeepsDraftAndDoesNotPublishAnEntryUntilExplicitDurableRetry() {
        val originalXml = preferenceFile.readBytes()
        show()
        field().performScrollTo().performTextInput("Uncertain new template")
        withBlockedPreferenceDirectory {
            ui.onNodeWithText("Save quick message").performClick()
            assertDraft("Uncertain new template")
            assertUncertainSave()
            ui.onNodeWithContentDescription("Edit quick message 1").assertDoesNotExist()
            // A false commit can change Android's cache: the UI must not interpret that as saved.
            ui.runOnIdle { assertEquals("Uncertain new template", preferences.getString("template-0", null)) }
        }
        assertArrayEquals(originalXml, preferenceFile.readBytes())
        assertTrue(durableValues().isEmpty())

        ui.onNodeWithText("Save quick message").performClick()
        assertDraft("")
        ui.onNodeWithContentDescription("Edit quick message 1").assertExists()
        ui.onNodeWithText("Save could not be confirmed.", substring = true).assertDoesNotExist()
        assertEquals(mapOf("template-0" to "Uncertain new template"), durableValues())
    }

    @Test fun failedEditKeepsOriginalListAndEditDraftUntilExplicitDurableRetry() {
        check(preferences.edit().putString("template-0", "Original local template").commit())
        val originalXml = preferenceFile.readBytes()
        show()
        ui.onNodeWithContentDescription("Edit quick message 1").performScrollTo().performClick()
        field().performScrollTo().performTextReplacement("Uncertain edited template")
        withBlockedPreferenceDirectory {
            ui.onNodeWithText("Save changes").performClick()
            assertDraft("Uncertain edited template")
            assertUncertainSave()
            ui.onNodeWithText("Original local template").assertExists()
            ui.onNodeWithText("Cancel edit").assertIsEnabled()
        }
        assertArrayEquals(originalXml, preferenceFile.readBytes())
        assertEquals(mapOf("template-0" to "Original local template"), durableValues())

        ui.onNodeWithText("Save changes").performClick()
        assertDraft("")
        ui.onNodeWithText("Cancel edit").assertDoesNotExist()
        ui.onNodeWithText("Uncertain edited template").assertExists()
        assertEquals(mapOf("template-0" to "Uncertain edited template"), durableValues())
    }

    @Test fun failedReorderAndRemovalKeepVisibleOrderAndBothDurableEntries() {
        check(preferences.edit().putString("template-0", "One").putString("template-1", "Two").commit())
        val originalXml = preferenceFile.readBytes()
        show()
        withBlockedPreferenceDirectory {
            ui.onNodeWithContentDescription("Move quick message 1 down").performScrollTo().performClick()
            assertUncertainSave()
            // Scroll clipping changes bounds, so compare the actual list's semantic tree order.
            assertEquals(listOf("One", "Two"), ui.onAllNodes(hasText("One") or hasText("Two"))
                .fetchSemanticsNodes().map { node -> node.config[SemanticsProperties.Text].single().text })
            ui.onNodeWithContentDescription("Remove quick message 1").performScrollTo().performClick()
            ui.onNodeWithText("One").assertExists()
            ui.onNodeWithText("Two").assertExists()
            ui.onNodeWithContentDescription("Edit quick message 2").assertExists()
        }
        assertArrayEquals(originalXml, preferenceFile.readBytes())
        assertEquals(mapOf("template-0" to "One", "template-1" to "Two"), durableValues())

        ui.onNodeWithContentDescription("Remove quick message 1").performScrollTo().performClick()
        ui.onNodeWithText("One").assertDoesNotExist()
        assertEquals(mapOf("template-0" to "Two"), durableValues())
    }

    @Test fun unreadableStoredTypeDisablesEditorWithoutRewritingTheDurableRecord() {
        check(preferences.edit().putInt("template-0", 7).commit())
        val originalXml = preferenceFile.readBytes()
        show()
        ui.onNodeWithText("Saved quick messages could not be read. Editing is unavailable.").assertExists()
        // Disabled fields intentionally expose no SetText action; locate the field by its label.
        ui.onNodeWithText("Your quick message").assertIsNotEnabled()
        ui.onNodeWithText("Save quick message").assertIsNotEnabled()
        ui.onNodeWithText("Done").assertIsNotEnabled()
        assertArrayEquals(originalXml, preferenceFile.readBytes())
        assertEquals(mapOf("template-0" to 7), durableValues())
    }
}
