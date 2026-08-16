package io.github.nbjelanovic.otclient

import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ConnectedDeviceManifestPolicyTest {
    @Test
    fun manifestDeclaresExactConnectedDeviceForegroundSurface() {
        val manifest = projectFile("src/main/AndroidManifest.xml").readText()
        val permissions = Regex("<uses-permission[^>]+android:name=\"([^\"]+)\"")
            .findAll(manifest)
            .map { it.groupValues[1] }
            .toSet()
        assertEquals(
            setOf(
                "android.permission.BLUETOOTH_SCAN",
                "android.permission.BLUETOOTH_CONNECT",
                "android.permission.FOREGROUND_SERVICE",
                "android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE",
                "android.permission.POST_NOTIFICATIONS",
            ),
            permissions,
        )
        assertTrue(manifest.contains("android:name=\".TrailConnectedDeviceService\""))
        assertTrue(manifest.contains("android:exported=\"false\""))
        assertTrue(manifest.contains("android:foregroundServiceType=\"connectedDevice\""))
        assertFalse(manifest.contains("<receiver"))
        listOf("LOCATION", "INTERNET", "STORAGE", "MANAGE_EXTERNAL_STORAGE").forEach {
            assertFalse(manifest.contains(it))
        }
    }

    @Test
    fun serviceSourceIsNonStickyExplicitAndContainsNoAutoStartPath() {
        val source = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/TrailConnectedDeviceService.kt",
        ).readText()
        assertTrue(source.contains("intent?.action != ACTION_START_FROM_VISIBLE_USER"))
        assertTrue(source.contains("START_NOT_STICKY"))
        assertFalse(source.contains("START_STICKY"))
        assertFalse(source.contains("BOOT_COMPLETED"))
        assertFalse(source.contains("startScan()"))
        assertFalse(source.contains("FakeCompanionTransport"))
        assertTrue(source.contains("FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE"))
        val permissionGate = source.indexOf("if (!nearbyPermissionsGranted())")
        val promotionGate = source.indexOf("if (!promoteToForeground())")
        val ownerCreation = source.indexOf("createOwner(generation)")
        assertTrue(permissionGate >= 0 && promotionGate > permissionGate && ownerCreation > promotionGate)
        assertTrue(source.contains("catch (_: SecurityException)"))
        assertTrue(source.contains("stopSelfResult(startId)"))
        assertTrue(source.contains("PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE"))
        assertTrue(source.contains("CONNECTED_DEVICE_NOTIFICATION_TITLE"))
        assertTrue(source.contains("CONNECTED_DEVICE_NOTIFICATION_TEXT"))
    }

    @Test
    fun activityKeepsScreenOnOnlyThroughItsVisibleWindow() {
        val activitySource = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt",
        ).readText()
        val keepScreenOn =
            "window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)"
        val superOnCreate = activitySource.indexOf("super.onCreate(savedInstanceState)")
        val keepScreenOnIndex = activitySource.indexOf(keepScreenOn)
        val controllerCreation = activitySource.indexOf("appController =")
        assertTrue(superOnCreate >= 0)
        assertTrue(keepScreenOnIndex > superOnCreate)
        assertTrue(controllerCreation > keepScreenOnIndex)
        assertEquals(1, Regex(Regex.escape(keepScreenOn)).findAll(activitySource).count())
        listOf(
            "PowerManager",
            "WakeLock",
            "WAKE_LOCK",
            "setTurnScreenOn",
            "FLAG_TURN_SCREEN_ON",
            "setShowWhenLocked",
            "FLAG_SHOW_WHEN_LOCKED",
            "screenBrightness",
        ).forEach {
            assertFalse(activitySource.contains(it))
        }

        val serviceSource = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/TrailConnectedDeviceService.kt",
        ).readText()
        assertFalse(serviceSource.contains("FLAG_KEEP_SCREEN_ON"))
    }

    @Test
    fun activityRendersTheExactTrailArtworkWithoutAStartupDelay() {
        val activitySource = projectFile(
            "src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt",
        ).readText()
        val artwork = projectFile(
            "src/main/res/drawable-nodpi/limited_underground_trail.png",
        )

        assertEquals(2_559_044L, artwork.length())
        val artworkSha256 = MessageDigest.getInstance("SHA-256")
            .digest(artwork.readBytes())
            .joinToString("") { byte -> "%02X".format(byte) }
        assertEquals(
            "A3024504BA261ADDAFD2A85F49F6BCE630D1E9AB994EEA348D5842A6D2AB7422",
            artworkSha256,
        )
        assertTrue(activitySource.contains("R.drawable.limited_underground_trail"))
        assertTrue(activitySource.contains("contentDescription = null"))
        assertTrue(activitySource.contains("contentScale = ContentScale.Fit"))
        assertTrue(activitySource.contains("Text(\"Limited Underground\""))
        assertTrue(activitySource.contains("Text(\"Trail\""))
        listOf("Thread.sleep", "delay(", "postDelayed", "SplashScreen").forEach {
            assertFalse(activitySource.contains(it))
        }
    }

    private fun projectFile(path: String): File {
        val direct = File(path)
        if (direct.isFile) return direct
        val fromAndroid = File("app", path)
        require(fromAndroid.isFile) { "Required Android source file is missing: $path" }
        return fromAndroid
    }
}
