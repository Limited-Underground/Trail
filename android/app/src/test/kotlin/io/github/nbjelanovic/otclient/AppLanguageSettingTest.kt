package io.github.nbjelanovic.otclient

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** The app keeps no language store of its own: Android 13+ holds the choice (D-192). */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w360dp-h640dp-port")
class AppLanguageSettingTest {
    private val compose = createComposeRule()
    @get:Rule val composeHost = withRobolectricComposeHost(compose)

    @Test fun androidKeepsTheChoiceAndSystemDefaultClearsIt() {
        val context = RuntimeEnvironment.getApplication()
        assertNull(AppLanguageSetting.current(context))
        assertTrue(AppLanguageSetting.select(context, AppLanguage.SERBIAN_CYRILLIC))
        assertEquals(AppLanguage.SERBIAN_CYRILLIC, AppLanguageSetting.current(context))
        assertTrue(AppLanguageSetting.select(context, null))
        assertNull(AppLanguageSetting.current(context))
    }

    @Test fun phonesBeforeAndroid13ShowNoLanguageChoice() {
        compose.setContent {
            MaterialTheme {
                AppDisplaySettings(showBuildLabel = true, onShowBuildLabelChanged = {}, saveFailed = false,
                    languageChoiceAvailable = false)
            }
        }
        compose.onNodeWithText("App settings").assertIsDisplayed()
        compose.onNodeWithText("Language").assertDoesNotExist()
    }
}
