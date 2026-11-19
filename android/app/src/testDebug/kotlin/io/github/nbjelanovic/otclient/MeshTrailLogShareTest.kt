package io.github.nbjelanovic.otclient

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import java.io.File
import java.lang.reflect.Proxy
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class MeshTrailLogShareTest {
    private lateinit var context: Context
    private lateinit var directory: File
    private val sample = "MeshTrail operation log\nStatus: Bluetooth off\nEncoding: café\n"

    @Before fun prepareDisposableCache() {
        context = RuntimeEnvironment.getApplication()
        val temporaryRoot = File(requireNotNull(System.getProperty("java.io.tmpdir"))).canonicalFile.toPath()
        check(context.cacheDir.canonicalFile.toPath().startsWith(temporaryRoot))
        directory = File(context.cacheDir, "support-reports")
        check(directory.isDirectory || directory.mkdirs())
        check(requireNotNull(directory.listFiles()).isEmpty())
    }

    @Test fun shareSheetCarriesTheUtf8TextFileWithAReadOnlyGrant() = withHostProvider {
        val host = CaptureContext(context)
        assertTrue(shareMeshTrailLog(host, sample))
        val file = requireNotNull(directory.listFiles()).single()
        assertTrue(file.name.endsWith(".txt"))
        assertArrayEquals(sample.toByteArray(Charsets.UTF_8), file.readBytes())

        val chooser = host.launches.single()
        assertEquals(Intent.ACTION_CHOOSER, chooser.action)
        val send = requireNotNull(chooser.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java))
        assertEquals(Intent.ACTION_SEND, send.action)
        assertEquals("text/plain", send.type)
        assertNull(send.component)
        assertNull(send.`package`)
        assertNull(send.getStringArrayExtra(Intent.EXTRA_EMAIL))
        assertNull(send.getCharSequenceExtra(Intent.EXTRA_TEXT))
        assertEquals("MeshTrail operation log", send.getStringExtra(Intent.EXTRA_SUBJECT))
        val uri = requireNotNull(send.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java))
        assertEquals("content", uri.scheme)
        assertEquals(context.packageName + ".support-files", uri.authority)
        assertEquals(listOf("support_reports", file.name), uri.pathSegments)
        assertEquals("MeshTrail-operation-log.txt", uri.getQueryParameter("displayName"))
        assertEquals(uri, requireNotNull(send.clipData).getItemAt(0).uri)
        val sharingFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
            Intent.FLAG_GRANT_PREFIX_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
        assertEquals(Intent.FLAG_GRANT_READ_URI_PERMISSION, send.flags and sharingFlags)
        assertEquals(Intent.FLAG_GRANT_READ_URI_PERMISSION, chooser.flags and sharingFlags)
    }

    @Test fun eightRecentExportsArePreservedAndExpiredFilesMakeRoom() = withHostProvider {
        val host = CaptureContext(context)
        val priorSupport = File(directory, "trail-support-previous.txt")
        priorSupport.writeText("Previously shared support report", Charsets.UTF_8)
        repeat(7) { assertTrue(shareMeshTrailLog(host, sample + it)) }
        val prior = requireNotNull(directory.listFiles()).associate { it.name to it.readBytes().toList() }
        assertEquals(8, prior.size)
        assertFalse(shareMeshTrailLog(host, sample + "extra"))
        assertEquals(7, host.launches.size)
        assertEquals(prior, requireNotNull(directory.listFiles()).associate { it.name to it.readBytes().toList() })

        val expired = File(directory, prior.keys.first { it != priorSupport.name })
        assertTrue(expired.setLastModified(System.currentTimeMillis() - 25L * 60 * 60 * 1000))
        assertTrue(shareMeshTrailLog(host, sample + "replacement"))
        assertFalse(expired.exists())
        assertEquals(8, host.launches.size)
        val after = requireNotNull(directory.listFiles()).associate { it.name to it.readBytes().toList() }
        assertEquals(8, after.size)
        prior.filterKeys { it != expired.name }.forEach { (name, bytes) -> assertEquals(bytes, after[name]) }
    }

    @Test fun oversizeUtf8IsRejectedBeforePreparingAnAttachment() = withHostProvider {
        val host = CaptureContext(context)
        assertFalse(shareMeshTrailLog(host, "é".repeat(65 * 1024)))
        assertTrue(host.launches.isEmpty())
        assertTrue(requireNotNull(directory.listFiles()).isEmpty())
    }

    @Test fun unresolvableProviderLeavesNoUnsharedCopies() {
        val host = CaptureContext(context, missingProvider = true)
        repeat(10) { assertFalse(shareMeshTrailLog(host, sample)) }
        assertTrue(host.launches.isEmpty())
        assertTrue(requireNotNull(directory.listFiles()).isEmpty())
    }

    @Test fun launchFailureDoesNotThrowOrRevokeAnUncertainHandoff() = withHostProvider {
        val host = CaptureContext(context, failAtLaunch = true)
        assertFalse(shareMeshTrailLog(host, sample))
        assertEquals(1, host.launches.size)
        assertArrayEquals(sample.toByteArray(Charsets.UTF_8), requireNotNull(directory.listFiles()).single().readBytes())
    }

    private class CaptureContext(
        base: Context,
        private val missingProvider: Boolean = false,
        private val failAtLaunch: Boolean = false,
    ) : ContextWrapper(base) {
        val launches = mutableListOf<Intent>()

        override fun getPackageName(): String =
            super.getPackageName() + if (missingProvider) ".unavailable_fixture" else ""

        override fun startActivity(intent: Intent) {
            launches += intent
            if (failAtLaunch) throw ActivityNotFoundException("Host fixture")
        }
    }

    // FileProvider 1.13.1 joins its root with a literal slash. Replace only URI
    // mapping on Windows hosts where canonical paths contain backslashes.
    private fun withHostProvider(block: () -> Unit) {
        val authority = context.packageName + ".support-files"
        val provider = requireNotNull(context.packageManager.resolveContentProvider(authority, 0))
        assertFalse(provider.exported)
        assertTrue(provider.grantUriPermissions)
        requireNotNull(context.contentResolver.acquireContentProviderClient(authority)).close()
        if (File.separatorChar != '\\') {
            block()
            return
        }

        val cacheField = FileProvider::class.java.getDeclaredField("sCache").apply { isAccessible = true }
        @Suppress("UNCHECKED_CAST")
        val cache = cacheField.get(null) as MutableMap<String, Any>
        val strategy = FileProvider::class.java.declaredClasses.single { it.simpleName == "PathStrategy" }
        val adapter = Proxy.newProxyInstance(strategy.classLoader, arrayOf(strategy)) { _, method, args ->
            when (method.name) {
                "getUriForFile" -> {
                    val file = (requireNotNull(args).single() as File).canonicalFile
                    check(file.parentFile == directory.canonicalFile)
                    Uri.Builder().scheme("content").authority(authority)
                        .appendPath("support_reports").appendPath(file.name).build()
                }
                "getFileForUri" -> {
                    val uri = requireNotNull(args).single() as Uri
                    check(uri.authority == authority && uri.pathSegments.size == 2)
                    check(uri.pathSegments[0] == "support_reports")
                    File(directory, uri.pathSegments[1]).canonicalFile.also {
                        check(it.parentFile == directory.canonicalFile)
                    }
                }
                else -> error("Unexpected host path operation")
            }
        }
        val original = cache.put(authority, adapter)
        try {
            block()
        } finally {
            if (original == null) cache.remove(authority) else cache[authority] = original
        }
    }
}
