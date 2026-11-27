package io.github.nbjelanovic.otclient

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Comment
import org.w3c.dom.Element

/**
 * Keeps the language files in step (D-192): every English string is in every language file,
 * no language file has strings English lacks, every language file says whether it was
 * reviewed, and the picker, locales_config.xml and values-* folders list the same languages.
 */
class AppLanguageResourcesTest {
    private val res = File("src/main/res")

    private fun folderFor(tag: String): String = when {
        tag == "en" -> "values"
        // Plain sr resolves as Cyrillic for sr, sr-RS and sr-Cyrl phones (D-196).
        tag == "sr-Cyrl" -> "values-sr"
        tag.contains('-') -> "values-b+" + tag.replace('-', '+')
        else -> "values-$tag"
    }

    private fun stringNames(file: File, translatableOnly: Boolean): Set<String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val names = mutableSetOf<String>()
        for (tagName in listOf("string", "plurals", "string-array")) {
            val nodes = document.getElementsByTagName(tagName)
            for (i in 0 until nodes.length) {
                val element = nodes.item(i) as Element
                if (translatableOnly && element.getAttribute("translatable") == "false") continue
                names += "$tagName:${element.getAttribute("name")}"
            }
        }
        return names
    }

    private fun reviewMarker(file: File): String? {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val first = document.childNodes.let { nodes -> (0 until nodes.length).map(nodes::item) }
            .firstOrNull { it is Comment } ?: return null
        return first.textContent.trim().substringBefore(':')
    }

    @Test fun pickerLocalesConfigAndFoldersListTheSameLanguages() {
        val picker = AppLanguage.entries.map { it.tag }.toSet()
        val config = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File(res, "xml/locales_config.xml")).getElementsByTagName("locale")
            .let { nodes -> (0 until nodes.length).map { (nodes.item(it) as Element).getAttribute("android:name") } }
            .toSet()
        assertEquals("locales_config.xml must match AppLanguage", picker, config)
        val folders = res.listFiles()!!.filter { File(it, "strings.xml").isFile }.map { it.name }.toSet()
        assertEquals("values-* folders with strings.xml must match AppLanguage",
            picker.map(::folderFor).toSet(), folders)
    }

    @Test fun everyLanguageHasExactlyTheEnglishStrings() {
        val english = stringNames(File(res, "values/strings.xml"), translatableOnly = true)
        val problems = AppLanguage.entries.filter { it != AppLanguage.ENGLISH }.flatMap { language ->
            val file = File(res, "${folderFor(language.tag)}/strings.xml")
            val translated = stringNames(file, translatableOnly = false)
            (english - translated).map { "${language.tag}: missing $it" } +
                (translated - english).map { "${language.tag}: not in English $it" }
        }
        assertTrue("Language files out of step:\n" + problems.joinToString("\n"), problems.isEmpty())
    }

    @Test fun everyLanguageFileSaysWhetherItWasReviewed() {
        val unmarked = AppLanguage.entries.filter { it != AppLanguage.ENGLISH }
            .filter { reviewMarker(File(res, "${folderFor(it.tag)}/strings.xml")) !in setOf("UNREVIEWED", "REVIEWED") }
            .map { it.tag }
        assertTrue("First comment must start UNREVIEWED: or REVIEWED: in $unmarked", unmarked.isEmpty())
    }
}
