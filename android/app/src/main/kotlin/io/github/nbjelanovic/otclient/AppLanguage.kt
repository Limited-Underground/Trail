package io.github.nbjelanovic.otclient

import android.app.LocaleManager
import android.content.Context
import android.os.Build
import android.os.LocaleList

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
    // Folder values-sr (no script): Android treats plain sr as Cyrillic, so phones set to
    // Serbian, Serbian (Serbia) or Serbian (Cyrillic) all get it (D-196).
    SERBIAN_CYRILLIC("sr-Cyrl", "Српски"),
    RUSSIAN("ru", "Русский"),
    UKRAINIAN("uk", "Українська"),
    ;

    companion object {
        fun fromTag(tag: String?): AppLanguage? =
            entries.firstOrNull { it.tag.equals(tag, ignoreCase = true) }
    }
}

/**
 * Applies the in-app language choice; `null` means follow the phone. Only Android 13 and later
 * offer it: the system keeps the choice (per-app language, also editable in the phone's app
 * settings), so the app stores nothing itself. Android 12 and older follow the phone's language.
 */
internal object AppLanguageSetting {
    fun isAvailable(): Boolean = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU

    fun current(context: Context): AppLanguage? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return null
        val locales = context.getSystemService(LocaleManager::class.java)?.applicationLocales
        return if (locales == null || locales.isEmpty) null else AppLanguage.fromTag(locales[0].toLanguageTag())
    }

    /** Returns false when this phone cannot take an app language. The system recreates the screen. */
    fun select(context: Context, language: AppLanguage?): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return false
        val manager = context.getSystemService(LocaleManager::class.java) ?: return false
        manager.applicationLocales =
            if (language == null) LocaleList.getEmptyLocaleList() else LocaleList.forLanguageTags(language.tag)
        return true
    }
}
