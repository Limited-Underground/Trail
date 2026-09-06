package io.github.nbjelanovic.otclient

internal const val V1_TEST_CONNECTION_LOG_MAX_RECORDS = 512
internal const val V1_TEST_CONNECTION_LOG_MAX_BYTES = 64 * 1024

/** Written format. Versions 1 and 2 remain readable; every write upgrades to version 3. */
internal const val V1_TEST_CONNECTION_LOG_VERSION = 3
internal const val V1_TEST_CONNECTION_LOG_MIN_READABLE_VERSION = 1

enum class V1TestConnectionLogStatus { READY, CORRUPT, STORAGE_FAILURE, SESSION_REQUIRED, INVALID_EVENT, EXHAUSTED }

sealed interface V1TestConnectionLogEvent {
    data class Connection(val state: V1TestPhoneConnectionState) : V1TestConnectionLogEvent
    data class Lifecycle(val state: V1TestAppLifecycleState) : V1TestConnectionLogEvent
    /** Version-2 typed protected-flow milestone. */
    data class Trace(val emission: V1TestTraceEmission) : V1TestConnectionLogEvent
}

data class V1TestConnectionLogRecord(
    val session: Long,
    val elapsedMillis: Long,
    val event: V1TestConnectionLogEvent,
) {
    init { require(session > 0 && elapsedMillis >= 0) }
}

/** Implement with an atomic, app-private file. No Android or device identity enters this seam. */
interface V1TestConnectionLogStorage {
    fun read(): ByteArray?
    fun write(encoded: ByteArray): Boolean
    fun clear(): Boolean
}

/**
 * Single-owner recorder. The caller serializes operations and supplies session-relative elapsed
 * time. Persist before publishing; failed writes leave both history and counters unchanged.
 */
class V1TestConnectionLog(private val storage: V1TestConnectionLogStorage) {
    var status: V1TestConnectionLogStatus = V1TestConnectionLogStatus.READY
        private set

    /** Version of the file this recorder loaded, or the write version when no file existed. */
    var loadedVersion: Int = V1_TEST_CONNECTION_LOG_VERSION
        private set

    private var available = true
    private var lastSession = 0L
    private var currentSession: Long? = null
    private var lastElapsed = 0L
    private var lastConnection: V1TestPhoneConnectionState? = null
    private var records: List<V1TestConnectionLogRecord> = emptyList()

    init {
        try {
            val encoded = storage.read()
            if (encoded != null) {
                val decoded = decode(encoded)
                if (decoded == null) {
                    available = false
                    status = V1TestConnectionLogStatus.CORRUPT
                } else {
                    loadedVersion = decoded.version
                    lastSession = decoded.counter
                    records = decoded.records
                }
            }
        } catch (_: Exception) {
            available = false
            status = V1TestConnectionLogStatus.STORAGE_FAILURE
        }
    }

    fun startSession(): Boolean {
        if (!available) return false
        if (lastSession == Long.MAX_VALUE) return fail(V1TestConnectionLogStatus.EXHAUSTED)
        val candidate = lastSession + 1
        val retained = trimToByteBudget(records, candidate)
        if (!commit(candidate, retained)) return false
        records = retained
        lastSession = candidate
        currentSession = candidate
        lastElapsed = 0
        lastConnection = null
        return true
    }

    fun recordConnection(elapsedMillis: Long, state: V1TestPhoneConnectionState): Boolean =
        append(elapsedMillis, V1TestConnectionLogEvent.Connection(state))

    fun recordLifecycle(elapsedMillis: Long, state: V1TestAppLifecycleState): Boolean =
        append(elapsedMillis, V1TestConnectionLogEvent.Lifecycle(state))

    fun recordTrace(elapsedMillis: Long, emission: V1TestTraceEmission): Boolean =
        append(elapsedMillis, V1TestConnectionLogEvent.Trace(emission))

    private fun append(elapsed: Long, event: V1TestConnectionLogEvent): Boolean {
        if (!available) return false
        val session = currentSession ?: return fail(V1TestConnectionLogStatus.SESSION_REQUIRED)
        if (elapsed < 0 || elapsed < lastElapsed) return fail(V1TestConnectionLogStatus.INVALID_EVENT)
        if (event is V1TestConnectionLogEvent.Connection && event.state == lastConnection) {
            // Do not churn flash for repeated snapshots with the same connection state.
            lastElapsed = elapsed
            status = V1TestConnectionLogStatus.READY
            return true
        }
        val candidate = trimToByteBudget(
            (records + V1TestConnectionLogRecord(session, elapsed, event))
                .takeLast(V1_TEST_CONNECTION_LOG_MAX_RECORDS),
        )
        if (!commit(lastSession, candidate)) return false
        records = candidate
        lastElapsed = elapsed
        if (event is V1TestConnectionLogEvent.Connection) lastConnection = event.state
        return true
    }

    fun snapshot(): List<V1TestConnectionLogRecord> = records.toList()

    fun exportText(): String = buildString {
        appendLine("Trail V1-Test connection log (format $V1_TEST_CONNECTION_LOG_VERSION, loaded $loadedVersion)")
        appendLine("Activity-bound observation, not a continuous radio trace.")
        appendLine("Elapsed milliseconds restart at each recording session. Gaps are unobserved, not proof of disconnection.")
        appendLine("Connection and app lifecycle rows are coarse presentation categories.")
        appendLine("Stage rows are typed milestones taken from exact runtime transitions, never from a UI label.")
        appendLine("Stage rows sharing one elapsed value inside a transaction came from one runtime transition.")
        appendLine("Attaching to an already-established session records no transaction; missing stages are never back-filled.")
        appendLine("Returning-owner authorization Pending and Denied are not published by the current runtime.")
        appendLine("Generation is the bound connected-device service generation, not the private BLE transport generation.")
        appendLine("Reason and connection_diagnostic are separate runtime facts; UNAVAILABLE means not captured, including older records.")
        appendLine("PROTECTED_PROTOCOL_INFO_REJECTED and SNAPSHOT_REJECTED identify the failure phase, not proof of explicit device rejection.")
        appendLine("No BLE PIN, key, group secret, message content, address, or raw payload is recorded. No automatic upload.")
        appendLine("Recorder status: ${status.name}")
        appendLine("Records: ${records.size}/$V1_TEST_CONNECTION_LOG_MAX_RECORDS")
        appendLine("C/L: session\telapsed_ms\tkind\tstate")
        appendLine("S:   session\telapsed_ms\tS\tstage\ttransaction\tordinal\tgeneration\tsince_ms\tattempt\treason\tconnection_diagnostic")
        records.forEach { appendLine(recordLine(it)) }
    }

    fun clear(): Boolean {
        if (!runCatching { storage.clear() }.getOrDefault(false)) return fail(V1TestConnectionLogStatus.STORAGE_FAILURE)
        records = emptyList()
        lastSession = 0
        currentSession = null
        lastElapsed = 0
        lastConnection = null
        loadedVersion = V1_TEST_CONNECTION_LOG_VERSION
        available = true
        status = V1TestConnectionLogStatus.READY
        return true
    }

    private fun commit(session: Long, candidate: List<V1TestConnectionLogRecord>): Boolean {
        val encoded = encode(session, candidate)
        if (encoded.size > V1_TEST_CONNECTION_LOG_MAX_BYTES) return fail(V1TestConnectionLogStatus.EXHAUSTED)
        if (!runCatching { storage.write(encoded) }.getOrDefault(false)) return fail(V1TestConnectionLogStatus.STORAGE_FAILURE)
        status = V1TestConnectionLogStatus.READY
        return true
    }

    private fun fail(value: V1TestConnectionLogStatus): Boolean { status = value; return false }

    /** Oldest-first eviction keeps the newest terminal events inside the fixed byte budget. */
    private fun trimToByteBudget(
        candidate: List<V1TestConnectionLogRecord>,
        session: Long = lastSession,
    ): List<V1TestConnectionLogRecord> {
        val lengths = candidate.map { recordLine(it).length + 1 }
        var total = headerLine(session).length + lengths.sum()
        var drop = 0
        while (drop < candidate.size - 1 && total > V1_TEST_CONNECTION_LOG_MAX_BYTES) {
            total -= lengths[drop]
            drop += 1
        }
        return if (drop == 0) candidate else candidate.drop(drop)
    }

    private fun headerLine(session: Long): String =
        "OTCL\t$V1_TEST_CONNECTION_LOG_VERSION\t$session\n"

    private fun encode(session: Long, values: List<V1TestConnectionLogRecord>): ByteArray = buildString {
        append(headerLine(session))
        values.forEach { appendLine(recordLine(it)) }
    }.toByteArray(Charsets.US_ASCII)

    private fun recordLine(record: V1TestConnectionLogRecord): String {
        val suffix = when (val event = record.event) {
            is V1TestConnectionLogEvent.Connection -> "C\t${event.state.name}"
            is V1TestConnectionLogEvent.Lifecycle -> "L\t${event.state.name}"
            is V1TestConnectionLogEvent.Trace -> with(event.emission) {
                "S\t${stage.name}\t$transaction\t$ordinal\t$generation\t$sinceMillis\t$attempt\t${reason.name}\t${connectionDiagnostic?.name ?: "UNAVAILABLE"}"
            }
        }
        return "${record.session}\t${record.elapsedMillis}\t$suffix"
    }

    private class Decoded(
        val version: Int,
        val counter: Long,
        val records: List<V1TestConnectionLogRecord>,
    )

    private fun decode(bytes: ByteArray): Decoded? {
        if (bytes.isEmpty() || bytes.size > V1_TEST_CONNECTION_LOG_MAX_BYTES || bytes.any { it.toInt() !in 0..127 }) return null
        val text = bytes.toString(Charsets.US_ASCII)
        if (!text.endsWith('\n')) return null
        val lines = text.dropLast(1).split('\n')
        if (lines.size > V1_TEST_CONNECTION_LOG_MAX_RECORDS + 1) return null
        val header = lines.first().split('\t')
        if (header.size != 3 || header[0] != "OTCL") return null
        val version = number(header[1])?.takeIf {
            it in V1_TEST_CONNECTION_LOG_MIN_READABLE_VERSION.toLong()..V1_TEST_CONNECTION_LOG_VERSION.toLong()
        }?.toInt() ?: return null
        val counter = number(header[2]) ?: return null
        val decoded = ArrayList<V1TestConnectionLogRecord>()
        var previousSession = 0L
        var previousElapsed = 0L
        var previousOrdinal = 0L
        for (line in lines.drop(1)) {
            val fields = line.split('\t')
            if (fields.size < 4) return null
            val session = number(fields[0])?.takeIf { it in 1..counter && it >= previousSession } ?: return null
            val elapsed = number(fields[1]) ?: return null
            if (session == previousSession && elapsed < previousElapsed) return null
            if (session != previousSession) previousOrdinal = 0
            val event = when (fields[2]) {
                "C" -> if (fields.size != 4) null else {
                    V1TestPhoneConnectionState.entries.firstOrNull { it.name == fields[3] }
                        ?.let { V1TestConnectionLogEvent.Connection(it) }
                }
                "L" -> if (fields.size != 4) null else {
                    V1TestAppLifecycleState.entries.firstOrNull { it.name == fields[3] }
                        ?.let { V1TestConnectionLogEvent.Lifecycle(it) }
                }
                "S" -> if (version < 2 || fields.size != (if (version == 2) 10 else 11)) null else {
                    decodeTrace(fields, previousOrdinal)
                }
                else -> null
            } ?: return null
            if (event is V1TestConnectionLogEvent.Trace) previousOrdinal = event.emission.ordinal
            decoded += V1TestConnectionLogRecord(session, elapsed, event)
            previousSession = session
            previousElapsed = elapsed
        }
        return Decoded(version, counter, decoded)
    }

    private fun decodeTrace(fields: List<String>, previousOrdinal: Long): V1TestConnectionLogEvent.Trace? {
        val stage = V1TestTraceStage.entries.firstOrNull { it.name == fields[3] } ?: return null
        val transaction = number(fields[4]) ?: return null
        val ordinal = number(fields[5])?.takeIf { it >= 1 && it > previousOrdinal } ?: return null
        val generation = number(fields[6]) ?: return null
        val since = number(fields[7]) ?: return null
        val attempt = number(fields[8])?.takeIf { it <= Int.MAX_VALUE } ?: return null
        val reason = V1TestTraceReason.entries.firstOrNull { it.name == fields[9] } ?: return null
        val diagnostic = if (fields.size == 10 || fields[10] == "UNAVAILABLE") null else {
            BleConnectionDiagnostic.entries.firstOrNull { it.name == fields[10] } ?: return null
        }
        if (transaction == 0L && since != 0L) return null
        return V1TestConnectionLogEvent.Trace(
            V1TestTraceEmission(stage, transaction, ordinal, generation, since, attempt.toInt(), reason, diagnostic),
        )
    }

    private fun number(value: String): Long? = value.takeIf {
        it.isNotEmpty() && it.all { character -> character in '0'..'9' } && (it == "0" || !it.startsWith('0'))
    }?.toLongOrNull()
}
