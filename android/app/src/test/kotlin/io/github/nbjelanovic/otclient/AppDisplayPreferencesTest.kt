package io.github.nbjelanovic.otclient

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import java.io.File
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.util.ReflectionHelpers
import org.robolectric.util.ReflectionHelpers.ClassParameter

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], manifest = Config.NONE, application = Application::class)
class AppDisplayPreferencesTest {
    private lateinit var context: Application

    @Before fun setUp() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("v1-app-settings", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test fun buildLabelIsShownByDefault() {
        assertTrue(AppDisplayPreferences(context).showBuildLabel())
    }

    @Test fun hidingAndShowingPersistAcrossNewOwnersAndIndependentDiskReads() {
        assertTrue(AppDisplayPreferences(context).setShowBuildLabel(false))
        assertFalse(AppDisplayPreferences(context).showBuildLabel())
        assertFalse(freshReader().getBoolean("show-build-label", true))
        assertTrue(AppDisplayPreferences(context).setShowBuildLabel(true))
        assertTrue(AppDisplayPreferences(context).showBuildLabel())
        assertTrue(freshReader().getBoolean("show-build-label", false))
    }

    @Test fun settingDoesNotReplaceOtherSettingsOrTemplates() {
        val templates = context.getSharedPreferences("v1-local-templates", Context.MODE_PRIVATE)
        assertTrue(templates.edit().putString("template-0", "Retained").commit())
        val settings = context.getSharedPreferences("v1-app-settings", Context.MODE_PRIVATE)
        assertTrue(settings.edit().putString("other", "retained").commit())
        assertTrue(AppDisplayPreferences(context).setShowBuildLabel(false))
        assertEquals("Retained", templates.getString("template-0", null))
        assertEquals("retained", freshReader().getString("other", null))
    }

    private fun freshReader(): SharedPreferences = ReflectionHelpers.callConstructor(
        Class.forName("android.app.SharedPreferencesImpl").asSubclass(SharedPreferences::class.java),
        ClassParameter.from(File::class.java, File(context.applicationInfo.dataDir,
            "shared_prefs/v1-app-settings.xml")),
        ClassParameter.from(Int::class.javaPrimitiveType!!, Context.MODE_PRIVATE),
    )
}
