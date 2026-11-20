package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionPairingReason
import io.github.nbjelanovic.otprotocol.CompanionPairingState
import io.github.nbjelanovic.otprotocol.CompanionDeviceParametersCodec
import io.github.nbjelanovic.otprotocol.DeviceParameterGnssState
import io.github.nbjelanovic.otprotocol.DeviceParameterPowerSource
import io.github.nbjelanovic.otprotocol.DeviceParameterResetReason
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption.ATOMIC_MOVE
import java.nio.file.StandardCopyOption.REPLACE_EXISTING
import java.util.zip.CRC32

private const val SEGMENT_BYTES = 32 * 1024
private const val MAX_VALUE_CHARS = 320
private const val UNSIGNED = "(0|[1-9][0-9]{0,9})"
private const val SIGNED = "(0|-?[1-9][0-9]{0,3})"
private val elapsedSeconds = Regex("0|[1-9][0-9]{0,15}")

private fun choices(vararg allowed: String): Regex =
    Regex(allowed.joinToString("|") { Regex.escape(it) })
private fun enumChoices(values: Iterable<Enum<*>>): Regex = choices(*values.map { it.name }.toTypedArray())
private fun metrics(fields: String): Regex = Regex("age_ms=$UNSIGNED $fields")

/** Project only the fixed outcome and its current/previous display status. */
internal fun pairingResultLogValue(state: CompanionPairingState, historical: Boolean): String {
    val label = when (state) {
        CompanionPairingState.NONE -> "unknown"
        CompanionPairingState.RUNNING -> "running"
        CompanionPairingState.PAIRED_LOCAL -> "paired locally"
        CompanionPairingState.PAIRED_BOTH -> "paired on both boards"
        CompanionPairingState.FAILED -> "failed"
        CompanionPairingState.CANCELLED -> "cancelled"
        CompanionPairingState.INCOMPLETE -> "incomplete"
    }
    return if (historical) "previous $label" else label
}

private fun pairingValues(): Regex = choices(
    "available", "waiting", "ready", "refused",
    *CompanionPairingState.entries.flatMap { state ->
        listOf(pairingResultLogValue(state, historical = false), pairingResultLogValue(state, historical = true))
    }.toTypedArray(),
)

/** Only these schemas can enter the store or leave it, including when loading from disk. */
internal enum class MeshTrailLogKind(val label: String, private val shape: Regex) {
    APP("App", choices("opened", "Nearby Devices permission denied")),
    APP_VERSION("App version code", Regex(UNSIGNED)),
    SERVICE("Bluetooth service", choices("owner closed")),
    SERVICE_REFUSED("Bluetooth service refused", enumChoices(ConnectedDeviceServiceStartFailure.entries)),
    CONNECTION("Connection", choices(
        "inactive", "idle", "blocked", "scanning", "scan complete", "finding saved board",
        "awaiting authorization", "connecting", "negotiating", "ready", "reconnecting",
        "failed", "closed", "board erase requesting", "board erase in progress",
        "board erase verifying", "board erase unverified", "board erase complete",
    )),
    CONNECTION_BLOCK("Connection blocked", enumChoices(BleRuntimeBlock.entries)),
    CONNECTION_PHASE("Connection phase", enumChoices(BleNegotiationPhase.entries)),
    CONNECTION_ERROR("Connection error", enumChoices(BleRuntimeFailure.entries)),
    CONNECTION_DETAIL("Connection detail", enumChoices(BleConnectionDiagnostic.entries)),
    BLUETOOTH_RECOVERY("Bluetooth recovery", enumChoices(BluetoothRecoveryTrigger.entries)),
    BLUETOOTH_RETRY_REFUSED("Bluetooth retry refused", enumChoices(BluetoothRecoveryRefusal.entries)),
    AUTHORIZATION("Authorization", choices(
        "starting", "pending", "accepted", "denied", "invalid result",
        "expired", "unavailable", "unsupported", "unknown",
    )),
    PAIRING("Board pairing", pairingValues()),
    PAIRING_REFUSED("Board pairing refused", enumChoices(CompanionActionRejectReason.entries)),
    PAIRING_RESULT_REASON("Board pairing result reason",
        enumChoices(CompanionPairingReason.entries.filter { it != CompanionPairingReason.NONE })),
    PARAMETERS("Device parameters", Regex(
        "invalid record|sequence=$UNSIGNED age_ms=$UNSIGNED uptime_ms=(0|[1-9][0-9]{0,19})",
    )),
    BATTERY("Board battery", metrics("millivolts=$UNSIGNED percent=$UNSIGNED")),
    POWER("Board power", metrics("source=(${enumChoices(DeviceParameterPowerSource.entries).pattern})")),
    PACKET("Last received packet signal", metrics(
        "rssi_dbm=$SIGNED snr_quarter_db=$SIGNED frequency_hz=$UNSIGNED",
    )),
    RADIO("Radio counters", metrics(
        "tx_attempts=$UNSIGNED tx_completed=$UNSIGNED rx_frames=$UNSIGNED rx_errors=$UNSIGNED " +
            "rx_dropped=$UNSIGNED tx_refused=$UNSIGNED tx_busy_dropped=$UNSIGNED tx_prelaunch_dropped=$UNSIGNED",
    )),
    CAD("Radio channel deferrals", metrics("count=$UNSIGNED")),
    GNSS("GNSS health", metrics(
        "state=(${enumChoices(DeviceParameterGnssState.entries).pattern}) satellites=($UNSIGNED|unknown)",
    )),
    RESET("Board reset", metrics("reason=(${enumChoices(DeviceParameterResetReason.entries).pattern})")),
    HEAP("Board heap", metrics("free_bytes=$UNSIGNED minimum_free_bytes=$UNSIGNED")),
    MAIN_STACK("Main task stack", metrics("minimum_free_bytes=$UNSIGNED")),
    BLE_STACK("Bluetooth task stack", metrics("minimum_free_bytes=$UNSIGNED")),
    STORAGE("Board storage", metrics("used_entries=$UNSIGNED free_entries=$UNSIGNED total_entries=$UNSIGNED")),
    FIRMWARE("Board software", metrics("release_generation=$UNSIGNED partition_offset=$UNSIGNED"));

    fun accepts(value: String): Boolean = value.length <= MAX_VALUE_CHARS && shape.matches(value)
}

/** Two oldest-first segments in the app's no-backup directory. The owner never passes user text. */
internal class MeshTrailOperationLog(
    private val directory: File,
    private val segmentBytes: Int = SEGMENT_BYTES,
) {
    private val previous = File(directory, "previous.log")
    private val current = File(directory, "current.log")

    init {
        require(segmentBytes in 128..SEGMENT_BYTES)
    }

    @Synchronized
    fun append(seconds: Long, kind: MeshTrailLogKind, value: String): Boolean =
        appendBatch(seconds, listOf(kind to value))

    @Synchronized
    fun recordDeviceParameters(seconds: Long, record: ByteArray): Boolean {
        val snapshot = CompanionDeviceParametersCodec.decode(record)
        if (snapshot == null) {
            append(seconds, MeshTrailLogKind.PARAMETERS, "invalid record")
            return false
        }
        return appendBatch(seconds, meshTrailParameterEntries(snapshot))
    }

    private fun appendBatch(seconds: Long, entries: List<Pair<MeshTrailLogKind, String>>): Boolean {
        if (!elapsedSeconds.matches(seconds.toString()) || entries.any { !it.first.accepts(it.second) }) return false
        val payload = buildString {
            for ((kind, value) in entries) {
                val line = "$seconds\t${kind.name}\t$value"
                append(line).append('\t').append(checksum(line)).append('\n')
            }
        }.toByteArray(Charsets.UTF_8)
        if (payload.size > segmentBytes) return false

        return try {
            if (!directory.isDirectory && !directory.mkdirs()) return false
            val size = current.length()
            val separatorBytes = if (size > 0L) 1 else 0
            if (size > segmentBytes) {
                // Refuse to move an oversized corrupt file into the retained segment.
                if (!current.isFile) return false
                current.outputStream().close()
            } else if (size + separatorBytes + payload.size > segmentBytes) {
                if (!current.isFile) return false
                Files.move(current.toPath(), previous.toPath(), ATOMIC_MOVE, REPLACE_EXISTING)
            }
            // A separator prevents a crash-truncated row from swallowing a subsequent complete batch.
            java.io.FileOutputStream(current, true).use { output ->
                if (current.length() > 0L) output.write('\n'.code)
                output.write(payload)
            }
            true
        } catch (_: IOException) {
            false
        } catch (_: SecurityException) {
            false
        }
    }

    @Synchronized
    fun exportText(): String {
        val segments = listOf(readSegment(previous), readSegment(current))
        return buildString {
            appendLine("MeshTrail operation log")
            appendLine("Log format 2")
            appendLine("Times are seconds since logging started and may restart when the app restarts.")
            if (segments.any { it.corrupt }) appendLine("Some entries were unreadable.")
            if (segments.all { it.rows.isEmpty() }) appendLine("No events recorded yet.")
            for (segment in segments) segment.rows.forEach { appendLine(it) }
        }
    }

    private data class Segment(val rows: List<String> = emptyList(), val corrupt: Boolean = false)

    private fun readSegment(file: File): Segment {
        if (!file.exists()) return Segment()
        if (!file.isFile) return Segment(corrupt = true)
        val buffer = ByteArray(segmentBytes + 1)
        val size = try {
            file.inputStream().use { input ->
                var offset = 0
                while (offset < buffer.size) {
                    val read = input.read(buffer, offset, buffer.size - offset)
                    if (read == -1) break
                    offset += read
                }
                offset
            }
        } catch (_: IOException) {
            return Segment(corrupt = true)
        } catch (_: SecurityException) {
            return Segment(corrupt = true)
        }
        if (size > segmentBytes) return Segment(corrupt = true)
        val text = buffer.decodeToString(endIndex = size)
        var corrupt = size > 0 && !text.endsWith('\n')
        val complete = if (corrupt) text.substringBeforeLast('\n', missingDelimiterValue = "") else text
        val rows = complete.lineSequence().filter { it.isNotEmpty() }.mapNotNull { row ->
            render(row).also { if (it == null) corrupt = true }
        }.toList()
        return Segment(rows, corrupt)
    }

    private fun render(row: String): String? {
        val parts = row.split('\t', limit = 4)
        if (parts.size != 4 || !elapsedSeconds.matches(parts[0])) return null
        val kind = MeshTrailLogKind.entries.firstOrNull { it.name == parts[1] } ?: return null
        if (!kind.accepts(parts[2]) || checksum(row.substringBeforeLast('\t')) != parts[3]) return null
        return "+${parts[0]}s  ${kind.label}: ${parts[2]}"
    }

    private fun checksum(line: String): String =
        CRC32().apply { update(line.toByteArray(Charsets.UTF_8)) }.value.toString(16).padStart(8, '0')
}
