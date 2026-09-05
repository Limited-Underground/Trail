package io.github.nbjelanovic.otclient

internal const val V1_TEST_DIAGNOSTIC_MAX_RECORDS = 512
internal const val V1_TEST_NOTE_MAX_CHARS = 160

enum class V1TestPhoneConnectionState { DISCONNECTED, CONNECTING, AUTHORIZING, READY, RECONNECTING, FAILED }
enum class V1TestGpsHealthState { CURRENT, STALE, UNAVAILABLE, DISABLED }
enum class V1TestAppLifecycleState { FOREGROUND, BACKGROUND, STOPPED }
enum class V1TestRestartReason { COLD_START, PROCESS_RESTART, DEVICE_RECONNECT, UNKNOWN }
enum class V1TestFailureCode { BLE, RADIO, GPS, STORAGE, APP, UNKNOWN }

sealed interface V1TestDiagnosticPayload {
    data class PhoneConnection(val state: V1TestPhoneConnectionState) : V1TestDiagnosticPayload

    data class RadioCounters(val transmitted: Long, val received: Long, val dropped: Long) : V1TestDiagnosticPayload {
        init { require(transmitted >= 0 && received >= 0 && dropped >= 0) }
    }

    data class RadioSignal(val rssiDbm: Int, val snrTenthsDb: Int) : V1TestDiagnosticPayload {
        init { require(rssiDbm in -200..0 && snrTenthsDb in -500..500) }
    }

    data class QueueHealth(val depth: Int, val retries: Long, val oldestAgeMillis: Long?) : V1TestDiagnosticPayload {
        init {
            require(depth >= 0 && retries >= 0)
            require(oldestAgeMillis == null || oldestAgeMillis >= 0)
        }
    }

    data class GpsHealth(val state: V1TestGpsHealthState, val fixAgeMillis: Long?) : V1TestDiagnosticPayload {
        init { require(fixAgeMillis == null || fixAgeMillis >= 0) }
    }

    data class PowerSample(
        val millivolts: Int,
        val estimatedPercent: Int?,
        val charging: Boolean,
    ) : V1TestDiagnosticPayload {
        init {
            require(millivolts in 0..10_000)
            require(estimatedPercent == null || estimatedPercent in 0..100)
        }
    }

    data class AppLifecycle(val state: V1TestAppLifecycleState) : V1TestDiagnosticPayload
    data class Restart(val reason: V1TestRestartReason) : V1TestDiagnosticPayload
    data class Failure(val code: V1TestFailureCode) : V1TestDiagnosticPayload

    class TesterNote private constructor(val value: String) : V1TestDiagnosticPayload {
        override fun toString(): String = "TesterNote(redacted)"

        companion object {
            fun create(value: String): TesterNote? =
                value.takeIf {
                    it.length in 1..V1_TEST_NOTE_MAX_CHARS &&
                        it == it.trim() &&
                        it.none(Char::isISOControl)
                }?.let(::TesterNote)
        }
    }
}

data class V1TestDiagnosticRecord(
    val sequence: Long,
    val elapsedMillis: Long,
    val payload: V1TestDiagnosticPayload,
) {
    init {
        require(sequence >= 0)
        require(elapsedMillis >= 0)
    }

    override fun toString(): String =
        "V1TestDiagnosticRecord(sequence=$sequence, elapsedMillis=$elapsedMillis, payload=redacted)"
}

class V1TestDiagnosticRingBuffer(
    private val capacity: Int = V1_TEST_DIAGNOSTIC_MAX_RECORDS,
    private val allowUserSuppliedText: Boolean = false,
    initialSequence: Long = 0,
) {
    init {
        require(capacity in 1..V1_TEST_DIAGNOSTIC_MAX_RECORDS)
        require(initialSequence >= 0)
    }

    private val records = ArrayDeque<V1TestDiagnosticRecord>(capacity)
    private var nextSequence = initialSequence
    private var lastElapsedMillis: Long? = null

    fun append(elapsedMillis: Long, payload: V1TestDiagnosticPayload): Boolean {
        if (elapsedMillis < 0 || elapsedMillis < (lastElapsedMillis ?: 0)) return false
        if (nextSequence == Long.MAX_VALUE) return false
        if (payload is V1TestDiagnosticPayload.TesterNote && !allowUserSuppliedText) return false
        val record = V1TestDiagnosticRecord(nextSequence, elapsedMillis, payload)
        if (records.size == capacity) records.removeFirst()
        records.addLast(record)
        lastElapsedMillis = elapsedMillis
        nextSequence++
        return true
    }

    fun snapshot(): List<V1TestDiagnosticRecord> = records.toList()
    fun clear() = records.clear()
}
