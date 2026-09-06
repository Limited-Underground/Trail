package io.github.nbjelanovic.otclient

import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * OT-177 separation gate. It runs in every application variant, so a production build proves the
 * V1-Test diagnostic recorder and its storage implementation are absent from production source.
 * Independent unsigned-release DEX inspection is a separate layer in
 * `Test-AndroidUnsignedReleaseArtifact.ps1`.
 */
class V1TestDiagnosticSeparationTest {
    private val testOnlySymbols = listOf(
        "V1TestConnectionLog",
        "V1TestConnectionLogStorage",
        "V1TestConnectionTraceMachine",
        "V1TestTraceEmission",
        "V1TestTraceStage",
        "V1TestTraceReason",
        "AndroidV1TestLogStorage",
        "V1TestLogRuntime",
        "V1TestRecordingConnector",
        "V1TestServiceObservation",
        "V1TestApplication",
        "V1TestLogActivity",
        "V1TestMainActivity",
        "V1TestConnectionCategory",
        "V1TestLogShareRetentionPolicy",
        "shareV1TestConnectionLog",
    )

    @Test
    fun productionSourceNeverNamesOrInstantiatesTheV1TestRecorder() {
        val production = productionSources()
        assertTrue(production.size >= 20, "Production source scan looks incomplete")
        val text = production.joinToString("\n") { it.readText() }
        testOnlySymbols.forEach { symbol ->
            assertFalse(text.contains(symbol), "Production source must not reference $symbol")
        }
        // A constructor call is the exact prohibited form; the name check above already covers it,
        // but assert it separately so an accidental fully-qualified use is still caught.
        assertEquals(
            0,
            testOnlySymbols.sumOf { symbol -> Regex("$symbol\\s*\\(").findAll(text).count() },
        )
    }

    @Test
    fun theV1TestRecorderStaysInsideItsOwnVariantSourceSet() {
        val variant = projectDirectory("src/v1Test/kotlin/io/github/nbjelanovic/otclient")
        val names = variant.walkTopDown().filter { it.isFile && it.extension == "kt" }.map { it.name }.toSet()
        listOf(
            "V1TestConnectionLog.kt",
            "V1TestConnectionTrace.kt",
            "V1TestLogRuntime.kt",
            "V1TestRecordingConnector.kt",
            "V1TestServiceObservation.kt",
            "V1TestApplication.kt",
        ).forEach { assertTrue(it in names, "$it must remain in the V1-Test source set") }
        val productionNames = productionSources().map { it.name }.toSet()
        assertTrue(names.intersect(productionNames).isEmpty())
    }

    @Test
    fun theTraceRecorderKeepsNoFreeTextOrSecretBearingFields() {
        val trace = projectFile(
            "src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestConnectionTrace.kt",
        ).readText()
        val log = projectFile(
            "src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestConnectionLog.kt",
        ).readText()
        // Property reads, not prose: the recorder may describe what it excludes, never read it.
        listOf(
            ".endpointToken",
            ".publicLabel",
            ".publicOperationToken",
            ".claimToken",
            ".sessionNonce",
            ".payload",
            ".snapshot",
            "android.util.Log",
            "Log.",
        ).forEach { forbidden ->
            assertFalse(trace.contains(forbidden), "Trace must not read $forbidden")
            assertFalse(log.contains(forbidden), "Persisted log must not read $forbidden")
        }
        // Every persisted stage/reason field is a closed enum name, never caller text.
        assertTrue(trace.contains("enum class V1TestTraceStage"))
        assertTrue(trace.contains("enum class V1TestTraceReason"))
        val emission = trace.substringAfter("data class V1TestTraceEmission(").substringBefore(")")
        assertEquals(
            listOf(
                "val stage: V1TestTraceStage",
                "val transaction: Long",
                "val ordinal: Long",
                "val generation: Long",
                "val sinceMillis: Long",
                "val attempt: Int",
                "val reason: V1TestTraceReason",
                "val connectionDiagnostic: BleConnectionDiagnostic? = null",
            ),
            emission.lines().map { it.trim().removeSuffix(",") }.filter { it.startsWith("val ") },
        )
        assertTrue(log.contains("V1_TEST_CONNECTION_LOG_MAX_RECORDS = 512"))
        assertTrue(log.contains("V1_TEST_CONNECTION_LOG_MAX_BYTES = 64 * 1024"))
        assertTrue(log.contains("V1_TEST_CONNECTION_LOG_VERSION = 3"))
        assertTrue(log.contains("V1_TEST_CONNECTION_LOG_MIN_READABLE_VERSION = 1"))
    }

    @Test
    fun theTraceObserverStaysAdditiveAndNeverStopsTheService() {
        val connector = projectFile(
            "src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestRecordingConnector.kt",
        ).readText()
        // The caller's observer is always re-delivered, so the production controller keeps its lease.
        assertTrue(connector.contains("observer(connection)"))
        assertTrue(connector.contains("connection.port.observe"))
        // close() releases only this connector's leases; it must never call stopService().
        val close = connector.substringAfter("override fun close() {").substringBefore("private inner class")
        assertFalse(close.contains("stopService"))
        val port = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/ConnectedDeviceSessionOwner.kt",
        ).readText()
        assertTrue(port.contains("MAX_OBSERVERS = 4"), "Additional observation must stay inside the observer budget")
    }

    @Test
    fun connectionLogRemainsVariantOnlyAndInheritsRestrictedSupportProvider() {
        val manifest = projectFile("src/v1Test/AndroidManifest.xml").readText()
        assertFalse(manifest.contains("androidx.core.content.FileProvider"))
        assertTrue(manifest.contains("android:name=\".V1TestApplication\""))
        assertTrue(manifest.contains("android:exported=\"false\""))
        assertFalse(manifest.contains("V1ScreenGalleryActivity"))
        val main = projectFile("src/main/AndroidManifest.xml").readText()
        assertTrue(main.contains("androidx.core.content.FileProvider"))
        assertTrue(main.contains("android:authorities=\"\${applicationId}.support-files\""))
        assertTrue(main.contains("android:exported=\"false\""))
        assertTrue(main.contains("android:grantUriPermissions=\"true\""))
        assertTrue(main.contains("support_file_paths"))
        assertFalse(main.contains("V1TestApplication"))
        val paths = projectFile("src/v1Test/res/xml/support_file_paths.xml").readText()
        assertTrue(paths.contains("path=\"support-reports/\""))
        assertEquals(projectFile("src/main/res/xml/support_file_paths.xml").readLines(), paths.lines().dropLastWhile { it.isEmpty() })
        val helper = projectFile("src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestLogShare.kt").readText()
        assertTrue(helper.contains("V1TestLogShareRetentionPolicy.plan"))
        assertTrue(helper.contains("Intent.FLAG_GRANT_READ_URI_PERMISSION"))
        assertTrue(helper.contains("File.createTempFile("))
    }

    @Test
    fun serviceRecorderCannotBeReleasedByActivityBindings() {
        val activity = projectFile("src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestMainActivity.kt").readText()
        assertFalse(activity.contains("V1TestRecordingConnector"))
        assertFalse(activity.contains("traceReleased"))
        val provider = projectFile("src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestApplication.kt").readText()
        assertTrue(provider.contains("ConnectedDeviceSessionObservationProvider"))
        val observation = projectFile("src/v1Test/kotlin/io/github/nbjelanovic/otclient/V1TestServiceObservation.kt").readText()
        assertFalse(observation.contains("stopService"))
        assertFalse(observation.contains("ConnectedDeviceServiceConnector"))
    }
    private fun productionSources(): List<File> =
        projectDirectory("src/main/kotlin/io/github/nbjelanovic/otclient")
            .walkTopDown().filter { it.isFile && it.extension == "kt" }.toList()

    private fun projectFile(path: String): File {
        val direct = File(path)
        if (direct.isFile) return direct
        val fromAndroid = File("app", path)
        require(fromAndroid.isFile) { "Required Android source file is missing: $path" }
        return fromAndroid
    }

    private fun projectDirectory(path: String): File {
        val direct = File(path)
        if (direct.isDirectory) return direct
        val fromAndroid = File("app", path)
        require(fromAndroid.isDirectory) { "Required Android source directory is missing: $path" }
        return fromAndroid
    }
}
