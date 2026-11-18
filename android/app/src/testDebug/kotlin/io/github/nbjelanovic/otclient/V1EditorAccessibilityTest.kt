package io.github.nbjelanovic.otclient

import android.content.Context
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Real composed editor checks in a host Android runtime, not TalkBack/device acceptance. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w411dp-h891dp")
@OptIn(ExperimentalTestApi::class)
class V1EditorAccessibilityTest {
    @get:Rule val ui = createComposeRule()
    private val preferences get() = RuntimeEnvironment.getApplication().getSharedPreferences("v1-local-templates", Context.MODE_PRIVATE)

    @Before fun emptyHostFixture() { check(preferences.edit().clear().commit()) }

    private fun show() {
        ui.setContent {
            MaterialTheme {
                // A small content viewport models the space left by an open phone keyboard.
                Box(Modifier.fillMaxWidth().height(320.dp)) {
                    V1MessagesScreen("No device connected", onConnect = {}, initiallyEditing = true)
                }
            }
        }
    }
    private fun field() = ui.onNode(hasSetTextAction())
    private fun edit() {
        check(preferences.edit().putString("template-0", "Original local template").commit())
        show()
        ui.onNodeWithContentDescription("Edit quick message 1").performScrollTo().performClick()
        field().performScrollTo().performTextReplacement("Edited local template")
        field().performClick()
    }
    private fun assertDraft(value: String) = field().assert(
        SemanticsMatcher.expectValue(SemanticsProperties.EditableText, AnnotatedString(value)))

    @Test fun saveStaysVisibleWhenEditorContentExceedsKeyboardViewport() {
        show()
        ui.onNodeWithText("Save quick message").assertIsDisplayed()
        field().performScrollTo().performTextInput("New local template")
        ui.onNodeWithText("Save quick message").assertIsDisplayed().assertIsEnabled()
        ui.onNodeWithText("Save quick message").assertHeightIsAtLeast(48.dp).assertWidthIsAtLeast(48.dp)
    }

    @Test fun tabAndReverseTabMoveFocusWithoutEditingText() {
        show()
        field().performScrollTo().performTextInput("New local template")
        field().performKeyInput { pressKey(Key.Tab) }
        ui.onNodeWithText("Save quick message").assertIsFocused()
        assertDraft("New local template")
        ui.onNodeWithText("Save quick message").performKeyInput {
            keyDown(Key.ShiftLeft); pressKey(Key.Tab); keyUp(Key.ShiftLeft)
        }
        field().assertIsFocused()
    }

    @Test fun editTraversesFieldSaveCancelInBothDirections() {
        edit()
        field().performKeyInput { pressKey(Key.Tab) }
        ui.onNodeWithText("Save changes").assertIsFocused().assertIsDisplayed()
        ui.onNodeWithText("Save changes").performKeyInput { pressKey(Key.Tab) }
        ui.onNodeWithText("Cancel edit").assertIsFocused().assertIsDisplayed()
        ui.onNodeWithText("Cancel edit").performKeyInput {
            keyDown(Key.ShiftLeft); pressKey(Key.Tab); keyUp(Key.ShiftLeft)
        }
        ui.onNodeWithText("Save changes").assertIsFocused()
        assertDraft("Edited local template")
    }

    @Test fun invalidDraftSkipsDisabledSaveAndCancelKeepsSavedValue() {
        edit()
        field().performTextClearance()
        ui.onNodeWithText("Save changes").assertIsNotEnabled()
        field().performKeyInput { pressKey(Key.Tab) }
        ui.onNodeWithText("Cancel edit").assertIsFocused().performClick()
        ui.runOnIdle { assertEquals("Original local template", preferences.getString("template-0", null)) }
    }

    @Test fun nextImeActionReachesSaveAndExplicitSaveWritesExistingSchema() {
        show()
        field().performScrollTo().performTextInput("New local template")
        field().performImeAction()
        ui.onNodeWithText("Save quick message").assertIsFocused().performClick()
        ui.runOnIdle { assertEquals("New local template", preferences.getString("template-0", null)) }
    }

    @Test fun repeatedTemplateActionsHaveContextualLabels() {
        check(preferences.edit().putString("template-0", "One").putString("template-1", "Two").commit())
        show()
        ui.onNodeWithContentDescription("Remove quick message 1").assertHasClickAction()
        ui.onNodeWithContentDescription("Remove quick message 2").assertHasClickAction()
        ui.onNodeWithContentDescription("Move quick message 2 up").assertIsEnabled()
        ui.onNodeWithContentDescription("Move quick message 1 down").assertIsEnabled()
    }
}
