package io.github.nbjelanovic.otclient

internal const val V1_SUPPORT_NOTE_MAX_CHARS = 2_000
internal const val V1_SUPPORT_REPORT_SCHEMA = "OT-SUPPORT/1"
internal const val V1_SUPPORT_REPORT_MIME_TYPE = "text/plain"

enum class V1SupportPhoneConnection { UNKNOWN, DISCONNECTED, CONNECTING, AUTHORIZING, READY, RECONNECTING, FAILED }
enum class V1SupportRadioHealth { UNKNOWN, UNAVAILABLE, READY, DEGRADED, FAULT, IDLE, TRANSMITTING, RECEIVING }
enum class V1SupportGpsHealth { UNKNOWN, UNAVAILABLE, SEARCHING, CURRENT, STALE, FAULT, DISABLED }
enum class V1SupportPowerHealth {
    UNKNOWN,
    EXTERNAL,
    NORMAL,
    LOW,
    CRITICAL,
    FAULT,
    CHARGING,
    BATTERY_ESTIMATE_AVAILABLE,
    BATTERY_ESTIMATE_UNAVAILABLE,
}
enum class V1SupportCode { NONE, BLE_RECONNECT, BLE_AUTHORIZATION, RADIO, GPS, STORAGE, APP }

class V1PublicDiagnosticLabel private constructor(val value: String) {
    override fun toString(): String = "V1PublicDiagnosticLabel(redacted)"

    companion object {
        private val allowed = Regex("[A-Za-z0-9 ._()+-]{1,80}")
        private val uuid = Regex("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}")
        private val separatedHardwareAddress = Regex("(?i)(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}")
        private val longHexIdentifier = Regex("(?i)[0-9a-f]{12,}")

        fun create(value: String): V1PublicDiagnosticLabel? =
            value.takeIf {
                it == it.trim() &&
                    allowed.matches(it) &&
                    !uuid.containsMatchIn(it) &&
                    !separatedHardwareAddress.containsMatchIn(it) &&
                    !longHexIdentifier.containsMatchIn(it) &&
                    !it.contains("PIN", ignoreCase = true) &&
                    !it.contains("KEY", ignoreCase = true)
            }?.let(::V1PublicDiagnosticLabel)
    }
}

data class V1SafeDiagnosticSnapshot(
    val appVersion: V1PublicDiagnosticLabel,
    val firmwareVersion: V1PublicDiagnosticLabel,
    val phoneModel: V1PublicDiagnosticLabel,
    val androidVersion: V1PublicDiagnosticLabel,
    val deviceModel: V1PublicDiagnosticLabel,
    val radioRegion: V1RadioRegion?,
    val phoneConnectionState: V1SupportPhoneConnection,
    val radioState: V1SupportRadioHealth,
    val gpsState: V1SupportGpsHealth,
    val powerState: V1SupportPowerHealth,
    val reconnectCount: Int?,
    val lastSupportCode: V1SupportCode?,
) {
    init { require(reconnectCount == null || reconnectCount >= 0) }

    override fun toString(): String = "V1SafeDiagnosticSnapshot(redacted)"
}

data class V1SupportReport(val fileName: String, val mimeType: String, val text: String)

object V1SupportReportGenerator {
    fun create(snapshot: V1SafeDiagnosticSnapshot, problemNote: String): V1SupportReport? {
        val note = normalizeNote(problemNote) ?: return null
        val text = buildString {
            appendLine(V1_SUPPORT_REPORT_SCHEMA)
            appendLine("Review before sharing. This file is not sent automatically.")
            appendLine()
            appendLine("[User-supplied problem description]")
            appendLine("The text below is included exactly for review and may contain details you typed.")
            appendLine(if (note.isEmpty()) "No description supplied." else note)
            appendLine()
            appendLine("[Versions]")
            appendLine("App: ${snapshot.appVersion.value}")
            appendLine("Firmware: ${snapshot.firmwareVersion.value}")
            appendLine("Phone: ${snapshot.phoneModel.value}")
            appendLine("Android: ${snapshot.androidVersion.value}")
            appendLine("Device: ${snapshot.deviceModel.value}")
            appendLine()
            appendLine("[Configuration]")
            appendLine("Radio region: ${snapshot.radioRegion?.publicCode ?: "Not available"}")
            appendLine()
            appendLine("[Current health]")
            appendLine("Phone connection: ${snapshot.phoneConnectionState}")
            appendLine("Radio: ${snapshot.radioState}")
            appendLine("GPS: ${snapshot.gpsState}")
            appendLine("Power: ${snapshot.powerState}")
            appendLine("Reconnect count: ${snapshot.reconnectCount ?: "Not available"}")
            appendLine("Last support code: ${snapshot.lastSupportCode ?: "Not available"}")
            appendLine()
            appendLine("[Excluded from automatic diagnostics]")
            appendLine("PINs, keys, pairing IDs, hardware addresses, invitation secrets,")
            appendLine("message bodies, group secrets, and coordinates.")
        }
        return V1SupportReport("trail-support-report.txt", V1_SUPPORT_REPORT_MIME_TYPE, text)
    }

    private fun normalizeNote(value: String): String? {
        if (value.length > V1_SUPPORT_NOTE_MAX_CHARS) return null
        val normalized = value.replace("\r\n", "\n").replace('\r', '\n')
        if (normalized.any { it.isISOControl() && it != '\n' && it != '\t' }) return null
        return normalized.trim()
    }
}
