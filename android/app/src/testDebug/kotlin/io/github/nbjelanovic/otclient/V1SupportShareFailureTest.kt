package io.github.nbjelanovic.otclient

import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import java.io.File
import java.lang.reflect.Proxy
import android.net.Uri
import androidx.core.content.FileProvider
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Actual host framework/cache behavior. No external Activity is ever launched. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class V1SupportShareFailureTest {
    private lateinit var context: Context
    private lateinit var directory: File

    @Before fun disposableHostCache() {
        context = RuntimeEnvironment.getApplication()
        val temporaryRoot = File(requireNotNull(System.getProperty("java.io.tmpdir"))).canonicalFile.toPath()
        check(context.cacheDir.canonicalFile.toPath().startsWith(temporaryRoot))
        directory = File(context.cacheDir, "support-reports")
        check(directory.isDirectory || directory.mkdirs())
        check(requireNotNull(directory.listFiles()).isEmpty())
    }

    /** Resolve a nonexistent provider through the real PackageManager; intercept all Activity calls. */
    private class HostContext(
        base: Context,
        private val missingProvider: Boolean,
        private val failAtHandoff: Boolean = false,
    ) : ContextWrapper(base) {
        var launchCalls = 0
            private set
        var capturedChooser: Intent? = null
            private set

        override fun getPackageName(): String = if (missingProvider) {
            "${super.getPackageName()}.missing_provider_fixture"
        } else super.getPackageName()

        override fun startActivity(intent: Intent) {
            launchCalls++
            capturedChooser = intent
            // Capture the handoff only: never call the platform or open a recipient.
            if (failAtHandoff) throw android.content.ActivityNotFoundException("Synthetic handoff uncertainty")
        }
    }

    /**
     * AndroidX 1.13.1 checks rootPath + '/' against canonical paths. On Windows those
     * paths use backslashes, so only the positive/uncertain handoff fixtures adapt
     * URI mapping. Failure fixtures still use the real missing-provider lookup.
     * This is not Android FileProvider or cross-app permission acceptance.
     */
    private fun <T> withHostSharePath(block: () -> T): T {
        val authority = "${context.packageName}.support-files"
        val provider = requireNotNull(context.packageManager.resolveContentProvider(authority, 0))
        check(!provider.exported && provider.grantUriPermissions)
        // Initialize the real provider before adapting its host-only path strategy.
        requireNotNull(context.contentResolver.acquireContentProviderClient(authority)).close()
        val cacheField = FileProvider::class.java.getDeclaredField("sCache").apply { isAccessible = true }
        @Suppress("UNCHECKED_CAST")
        val cache = cacheField.get(null) as MutableMap<String, Any>
        val previous = cache.remove(authority)
        if (File.separatorChar == '\\') {
            val strategyType = Class.forName("androidx.core.content.FileProvider\$PathStrategy")
            cache[authority] = Proxy.newProxyInstance(
                strategyType.classLoader, arrayOf(strategyType),
            ) { _, method, args ->
                when (method.name) {
                    "getUriForFile" -> {
                        val file = (requireNotNull(args).single() as File).canonicalFile
                        check(file.toPath().parent == directory.canonicalFile.toPath())
                        Uri.Builder().scheme("content").authority(authority)
                            .appendPath("support_reports").appendPath(file.name).build()
                    }
                    "getFileForUri" -> {
                        val uri = requireNotNull(args).single() as Uri
                        check(uri.scheme == "content" && uri.authority == authority)
                        check(uri.pathSegments.size == 2 && uri.pathSegments[0] == "support_reports")
                        File(directory, uri.pathSegments[1]).canonicalFile.also {
                            check(it.toPath().parent == directory.canonicalFile.toPath())
                        }
                    }
                    else -> error("Unexpected fixture path operation")
                }
            }
        }
        return try { block() } finally {
            cache.remove(authority)
            if (previous != null) cache[authority] = previous
        }
    }

    private fun report(): V1SupportReport {
        fun label(value: String) = requireNotNull(V1PublicDiagnosticLabel.create(value))
        return requireNotNull(V1SupportReportGenerator.create(
            V1SafeDiagnosticSnapshot(
                label("Host app"), label("Not available"), label("Host fixture"),
                label("35"), label("Not available"), null,
                V1SupportPhoneConnection.DISCONNECTED, V1SupportRadioHealth.UNKNOWN,
                V1SupportGpsHealth.UNKNOWN, V1SupportPowerHealth.UNKNOWN, null, null,
            ),
            "Synthetic problem description",
        ))
    }

    @Test fun preparationFailureRemovesOnlyTheNewUnofferedReport() {
        val previous = File(directory, "trail-support-previous.txt")
        previous.writeText("Previously offered synthetic report", Charsets.UTF_8)
        val previousBytes = previous.readBytes()
        val host = HostContext(context, missingProvider = true)

        val result = shareSupportReport(host, report())
        val remaining = requireNotNull(directory.listFiles()).map { it.name }.toSet()
        println("OT0320 pre-handoff provider failure: result=$result, files=$remaining, launchCalls=${host.launchCalls}")
        assertEquals("Support report could not be shared.", result)
        assertEquals(0, host.launchCalls)
        assertEquals(setOf(previous.name), remaining)
        assertArrayEquals(previousBytes, previous.readBytes())
    }

    @Test fun repeatedPreparationFailuresDoNotExhaustCapacity() {
        val previous = File(directory, "trail-support-previous.txt")
        previous.writeText("Previously offered synthetic report", Charsets.UTF_8)
        val host = HostContext(context, missingProvider = true)
        repeat(10) {
            assertEquals("Support report could not be shared.", shareSupportReport(host, report()))
            assertEquals(setOf(previous.name), requireNotNull(directory.listFiles()).map { it.name }.toSet())
        }
        assertEquals(0, host.launchCalls)
        assertEquals("Previously offered synthetic report", previous.readText(Charsets.UTF_8))
    }

    @Test fun successfulHandoffRetainsReviewedBytesAndReadOnlyGrant() = withHostSharePath {
        val previous = File(directory, "trail-support-previous.txt")
        previous.writeText("Previously offered synthetic report", Charsets.UTF_8)
        val host = HostContext(context, missingProvider = false)
        val probeUri = FileProvider.getUriForFile(host, "${host.packageName}.support-files", previous)
        android.content.ClipData.newUri(host.contentResolver, "Synthetic probe", probeUri)
        val reviewed = report()
        assertEquals("Share options opened.", shareSupportReport(host, reviewed))
        assertEquals(1, host.launchCalls)
        val created = requireNotNull(directory.listFiles()).single { it.name != previous.name }
        assertEquals(reviewed.text, created.readText(Charsets.UTF_8))
        assertEquals("Previously offered synthetic report", previous.readText(Charsets.UTF_8))
        val chooser = requireNotNull(host.capturedChooser)
        assertEquals(Intent.ACTION_CHOOSER, chooser.action)
        val send = requireNotNull(chooser.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java))
        assertEquals(Intent.ACTION_SEND, send.action)
        assertEquals(reviewed.mimeType, send.type)
        assertNull(send.component)
        assertNull(send.`package`)
        assertNull(send.getStringArrayExtra(Intent.EXTRA_EMAIL))
        val uri = requireNotNull(send.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java))
        assertEquals("content", uri.scheme)
        assertEquals("${context.packageName}.support-files", uri.authority)
        assertEquals(Intent.FLAG_GRANT_READ_URI_PERMISSION, send.flags and
            (Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION))
        assertEquals(send.getParcelableExtra(Intent.EXTRA_STREAM, android.net.Uri::class.java),
            requireNotNull(send.clipData).getItemAt(0).uri)
    }

    @Test fun uncertainHandoffRetainsTheNewReport() = withHostSharePath {
        val host = HostContext(context, missingProvider = false, failAtHandoff = true)
        val reviewed = report()
        assertEquals("Support report could not be shared.", shareSupportReport(host, reviewed))
        assertEquals(1, host.launchCalls)
        assertEquals(reviewed.text, requireNotNull(directory.listFiles()).single().readText(Charsets.UTF_8))
    }

    @Test fun capacityRefusalPreservesAllPreviouslyOfferedReports() {
        val previous = (0 until 8).associate { index ->
            val file = File(directory, "trail-support-previous-$index.txt")
            file.writeText("Previously offered synthetic report $index", Charsets.UTF_8)
            check(file.setLastModified(System.currentTimeMillis()))
            file.name to file.readBytes()
        }
        val host = HostContext(context, missingProvider = true)
        assertEquals("Eight recent reports are still shareable. Save this report instead.",
            shareSupportReport(host, report()))
        assertEquals(0, host.launchCalls)
        assertEquals(previous.keys, requireNotNull(directory.listFiles()).map { it.name }.toSet())
        previous.forEach { (name, bytes) -> assertArrayEquals(bytes, File(directory, name).readBytes()) }
    }
}
