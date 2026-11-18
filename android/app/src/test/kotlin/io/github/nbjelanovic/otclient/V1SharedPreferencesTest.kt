package io.github.nbjelanovic.otclient

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import java.io.File
import java.security.MessageDigest
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode
import org.robolectric.util.ReflectionHelpers
import org.robolectric.util.ReflectionHelpers.ClassParameter

/**
 * Host Android-35 framework/disk checks, not UI or process-death acceptance.
 * The callbacks below use real SharedPreferences, matching the existing numbered-key
 * boundary. V1MessagesScreen itself is not composed or exercised by these tests.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], manifest = Config.NONE, application = Application::class)
@LooperMode(LooperMode.Mode.PAUSED)
class V1SharedPreferencesTest {
    private lateinit var context: Application
    private lateinit var preferences: SharedPreferences
    private lateinit var preferenceFile: File

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        preferences = context.getSharedPreferences("v1-local-templates", Context.MODE_PRIVATE)
        assertEquals("android.app.SharedPreferencesImpl", preferences.javaClass.name)
        val dataDirectory = File(requireNotNull(context.applicationInfo.dataDir))
        preferenceFile = File(dataDirectory, "shared_prefs/v1-local-templates.xml")
        // Every filesystem fault below is confined to this test worker's temporary app data.
        val temporaryRoot = File(System.getProperty("java.io.tmpdir")).canonicalFile.toPath()
        assertTrue(dataDirectory.canonicalFile.toPath().startsWith(temporaryRoot))
        assertTrue(preferences.edit().clear().commit())
    }

    private fun save(values: List<String>, store: SharedPreferences = preferences): Boolean =
        V1QuickMessages.save(values) { snapshot ->
            store.edit().clear().also { editor ->
                snapshot.forEachIndexed { index, text -> editor.putString("template-$index", text) }
            }.commit()
        }

    private fun load(store: SharedPreferences): List<String>? =
        V1QuickMessages.load { index -> store.getString("template-$index", null) }

    private fun xmlDigest(): String = MessageDigest.getInstance("SHA-256")
        .digest(preferenceFile.readBytes()).joinToString("") { "%02x".format(it) }

    /** Bypass only the framework's same-process cache, so reload parses the XML anew. */
    private fun freshReader(): SharedPreferences = ReflectionHelpers.callConstructor(
        Class.forName("android.app.SharedPreferencesImpl").asSubclass(SharedPreferences::class.java),
        ClassParameter.from(File::class.java, preferenceFile),
        ClassParameter.from(Int::class.javaPrimitiveType!!, Context.MODE_PRIVATE),
    )

    @Test
    fun savedEditsAndOrderReloadFromRealXmlWithoutOldCachedValues() {
        assertTrue(save(listOf("Stopping here", "On my way", "Please check in")))
        val edited = V1QuickMessages.edit(load(preferences)!!, 1, "On my way", "Back at camp")!!
        val reordered = V1QuickMessages.move(edited, 1, -1)
        assertTrue(save(reordered))
        assertTrue(preferenceFile.isFile)
        val fromDisk = freshReader()
        assertNotSame(preferences, fromDisk)
        assertEquals(reordered, load(fromDisk))
        assertEquals(setOf("template-0", "template-1", "template-2"), fromDisk.all.keys)
        println("OT0313: Android-35 SharedPreferencesImpl XML round-trip; distinct reader; sha256=${xmlDigest()}")
    }

    @Test
    fun shrinkingAndEmptySaveRemoveNumberedKeysWithoutChangingOtherPreferenceFiles() {
        val other = context.getSharedPreferences("ot0313-unrelated-fixture", Context.MODE_PRIVATE)
        assertTrue(other.edit().putString("sentinel", "retained").commit())
        assertTrue(save(listOf("One", "Two", "Three")))
        assertTrue(save(listOf("Only one")))
        assertEquals(mapOf("template-0" to "Only one"), freshReader().all)
        assertTrue(save(emptyList()))
        assertTrue(freshReader().all.isEmpty())
        assertEquals("retained", other.getString("sentinel", null))
    }

    /** A real FileOutputStream failure; no mocked editor or forced commit return value. */
    private fun withBlockedPreferenceDirectory(action: () -> Unit) {
        val parent = preferenceFile.parentFile!!
        val retained = File(parent.parentFile, "ot0313-retained-shared-prefs")
        assertTrue(parent.isDirectory)
        assertFalse(retained.exists())
        assertTrue(parent.renameTo(retained))
        try {
            assertTrue(parent.createNewFile()) // A file cannot contain the preference XML.
            action()
        } finally {
            if (parent.isFile) assertTrue(parent.delete())
            assertTrue(retained.renameTo(parent))
        }
    }

    @Test
    fun failedCommitIsReportedOnceAlthoughFrameworkMemoryChangesAndDiskKeepsOldValues() {
        val original = listOf("Original entry")
        assertTrue(save(original))
        val originalXml = preferenceFile.readBytes()
        var calls = 0
        withBlockedPreferenceDirectory {
            val succeeded = V1QuickMessages.save(listOf("Uncertain replacement")) { values ->
                calls++
                preferences.edit().clear().putString("template-0", values.single()).commit()
            }
            assertFalse(succeeded)
            assertEquals(1, calls)
            // Android updates memory before disk. False must not be interpreted as rollback.
            assertEquals(listOf("Uncertain replacement"), load(preferences))
        }
        assertArrayEquals(originalXml, preferenceFile.readBytes())
        assertEquals(original, load(freshReader()))
        println("OT0313: real blocked-parent commit=false; one callback; memory changed; prior XML and fresh read preserved; sha256=${xmlDigest()}")
    }

    @Test
    fun emptyStoreWriteFailureDoesNotBecomeDurableAfterCacheIndependentReload() {
        val initialXml = preferenceFile.readBytes()
        withBlockedPreferenceDirectory {
            assertFalse(save(listOf("Not persisted")))
            assertEquals(listOf("Not persisted"), load(preferences))
        }
        assertArrayEquals(initialXml, preferenceFile.readBytes())
        assertEquals(emptyList<String>(), load(freshReader()))
        println("OT0313: real blocked-parent commit=false; empty durable store preserved; sha256=${xmlDigest()}")
    }

    @Test
    fun wrongTypeInActualPreferencesIsAReadFailureAndDoesNotRewriteTheStore() {
        assertTrue(preferences.edit().putInt("template-0", 7).commit())
        val originalXml = preferenceFile.readBytes()
        assertNull(load(freshReader()))
        assertArrayEquals(originalXml, preferenceFile.readBytes())
        assertEquals(7, freshReader().getInt("template-0", -1))
    }
}
