package io.github.nbjelanovic.otclient

import android.app.Activity
import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.os.Build
import android.os.LocaleList
import java.util.Locale

/**
 * App languages (D-096, D-192). Each one is shown in its own name, so the list stays
 * readable whatever language the app is currently in. Keep this list, res/xml/locales_config.xml
 * and the res/values-* folders in step; AppLanguageResourcesTest checks all three.
 */
internal enum class AppLanguage(val tag: String, val nativeName: String) {
    ENGLISH("en", "English"),
    SPANISH("es", "Español"),
    ITALIAN("it", "Italiano"),
    FRENCH("fr", "Français"),
    GERMAN("de", "Deutsch"),
    CROATIAN("hr", "Hrvatski"),
    SERBIAN_LATIN("sr-Latn", "Srpski"),
    RUSSIAN("ru", "Русский"),
    UKRAINIAN("uk", "Українська"),
    ;

    companion object {
        fun fromTag(tag: String?): AppLanguage? =
            entries.firstOrNull { it.tag.equals(tag, ignoreCase = true) }
    }
}

/**
 * Stores and applies the in-app language choice; `null` means follow the phone.
 * Android 13+ keeps the choice itself (per-app language, also editable in system Settings).
 * Older phones keep it in the app's own preferences and the activity applies it on start.
 */
internal object AppLanguageSetting {
    private const val PREFERENCES = "v1-app-settings"
    private const val KEY = "app-language"

    fun current(context: Context): AppLanguage? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val locales = context.getSystemService(LocaleManager::class.java)?.applicationLocales
            if (locales == null || locales.isEmpty) null else AppLanguage.fromTag(locales[0].toLanguageTag())
        } else {
            AppLanguage.fromTag(preferences(context).getString(KEY, null))
        }

    /** Returns false only when an older phone could not save the choice. */
    fun select(activity: Activity, language: AppLanguage?): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // The system recreates the activity in the new language.
            activity.getSystemService(LocaleManager::class.java)?.applicationLocales =
                if (language == null) LocaleList.getEmptyLocaleList() else LocaleList.forLanguageTags(language.tag)
            return true
        }
        val saved = preferences(activity).edit().putString(KEY, language?.tag).commit()
        if (saved) activity.recreate()
        return saved
    }

    /** For Activity.attachBaseContext on Android 12 and older; Android 13+ needs nothing. */
    fun wrap(base: Context): Context {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return base
        val language = AppLanguage.fromTag(preferences(base).getString(KEY, null)) ?: return base
        val locale = Locale.forLanguageTag(language.tag)
        val configuration = Configuration(base.resources.configuration)
        configuration.setLocales(LocaleList(locale))
        return base.createConfigurationContext(configuration)
    }

    private fun preferences(context: Context) =
        context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
}
