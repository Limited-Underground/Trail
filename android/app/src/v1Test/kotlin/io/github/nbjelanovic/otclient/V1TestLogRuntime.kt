package io.github.nbjelanovic.otclient

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.AtomicFile
import java.io.File
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

internal class AndroidV1TestLogStorage(context: Context) : V1TestConnectionLogStorage {
    private val file = AtomicFile(File(context.filesDir, "v1-connection-log"))
    override fun read(): ByteArray? {
        if (!file.baseFile.exists() && !File(file.baseFile.path + ".bak").exists()) return null
        return file.openRead().use { input ->
            val buffer = ByteArray(65_537)
            var count = 0
            while (count < buffer.size) {
                val next = input.read(buffer, count, buffer.size - count)
                if (next < 0) break
                count += next
            }
            buffer.copyOf(count)
        }
    }
    override fun write(encoded: ByteArray): Boolean {
        if (encoded.size > 65_536) return false
        val output = file.startWrite()
        return try { output.write(encoded); file.finishWrite(output); true }
        catch (_: Exception) { file.failWrite(output); false }
    }
    override fun clear(): Boolean { file.delete(); return !file.baseFile.exists() && !File(file.baseFile.path + ".bak").exists() }
}

/** Bounded worker queue keeps disk I/O out of BLE/UI callbacks. Only safe enums cross it. */
internal class V1TestLogRuntime private constructor(context: Context) {
    private val storage = AndroidV1TestLogStorage(context.applicationContext)
    private var startElapsed = SystemClock.elapsedRealtime()
    private val handler = Handler(Looper.getMainLooper())
    private val executor = ThreadPoolExecutor(1, 1, 0, TimeUnit.MILLISECONDS,
        ArrayBlockingQueue<Runnable>(128), { task -> Thread(task, "v1-test-log").apply { isDaemon = true } })
    @Volatile private var queueFailure = false
    private val log by lazy { V1TestConnectionLog(storage).also { if (!it.startSession()) queueFailure = true } }
    /** Owned by the single worker thread; never touched from a BLE or UI callback. */
    private val machine = V1TestConnectionTraceMachine()
    private fun submit(onFailure: () -> Unit = {}, task: () -> Unit) {
        try { executor.execute { runCatching(task).onFailure { queueFailure = true; handler.post(onFailure) } } }
        catch (_: RuntimeException) { queueFailure = true; handler.post(onFailure) }
    }
    fun connection(state: V1TestPhoneConnectionState) {
        val observed = SystemClock.elapsedRealtime()
        submit { if (!log.recordConnection(elapsedSince(observed), state)) queueFailure = true }
    }
    fun lifecycle(state: V1TestAppLifecycleState) {
        val observed = SystemClock.elapsedRealtime()
        submit { if (!log.recordLifecycle(elapsedSince(observed), state)) queueFailure = true }
    }
    fun trace(generation: Long, state: TrailAppUiState.BluetoothDevice) {
        val observed = SystemClock.elapsedRealtime()
        submit {
            val elapsed = elapsedSince(observed)
            persist(elapsed, machine.observe(generation, elapsed, state))
        }
    }
    fun traceReleased(generation: Long) {
        val observed = SystemClock.elapsedRealtime()
        submit {
            val elapsed = elapsedSince(observed)
            persist(elapsed, machine.observeServiceDisconnected(generation, elapsed))
        }
    }
    fun traceReleased() {
        val observed = SystemClock.elapsedRealtime()
        submit {
            val elapsed = elapsedSince(observed)
            persist(elapsed, machine.observeServiceDisconnected(elapsed))
        }
    }
    private fun persist(elapsed: Long, emissions: List<V1TestTraceEmission>) {
        // Persist before the review screen can show a milestone as recorded.
        emissions.forEach { if (!log.recordTrace(elapsed, it)) queueFailure = true }
        if (machine.failure != null) queueFailure = true
    }
    private fun elapsedSince(observed: Long): Long = (observed - startElapsed).coerceAtLeast(0)
    fun read(callback: (String) -> Unit) = submit(onFailure = { callback("Recording unavailable: worker/storage failure. No complete log is claimed.") }) {
        val text = log.exportText() + "\nStorage status: ${log.status}\n" +
            (machine.failure?.let { "Trace recorder latched closed: $it. Later milestones were not derived.\n" } ?: "") +
            (if (machine.staleObservations > 0) "Stale-generation observations refused: ${machine.staleObservations}\n" else "") +
            if (queueFailure) "Some events were not recorded: worker queue/storage failure.\n" else ""
        handler.post { callback(text) }
    }
    fun clear(callback: (Boolean) -> Unit) = submit(onFailure = { callback(false) }) {
        val cleared = log.clear() && log.startSession()
        if (cleared) { queueFailure = false; machine.reset(); startElapsed = SystemClock.elapsedRealtime() }
        handler.post { callback(cleared) }
    }
    companion object {
        @Volatile private var instance: V1TestLogRuntime? = null
        fun get(context: Context): V1TestLogRuntime = instance ?: synchronized(this) {
            instance ?: V1TestLogRuntime(context).also { instance = it }
        }
    }
}
