package io.github.nbjelanovic.otclient

internal const val V1_TEXT_OUTBOX_LIMIT = 20
internal const val V1_TEXT_OUTBOX_MAX_ATTEMPTS = 3

internal enum class V1TextOutboxPhase { WAITING, IN_FLIGHT, SENT, FAILED, UNCONFIRMED }
internal enum class V1TextOutboxResult { SENT, FAILED, RETRYABLE, UNCONFIRMED }

internal data class V1TextOutboxEntry(
    val id: Long,
    val text: String,
    val state: V1TextOutboxPhase,
    val attempts: Int,
) {
    override fun toString() = "V1TextOutboxEntry(state=$state, attempts=$attempts, text=redacted)"
}

internal data class V1TextOutboxSnapshot(
    val recipient: String? = null,
    val entries: List<V1TextOutboxEntry> = emptyList(),
) {
    override fun toString() = "V1TextOutboxSnapshot(count=${entries.size})"
}

internal interface V1TextOutboxStorage {
    fun load(): V1TextOutboxSnapshot?
    fun save(snapshot: V1TextOutboxSnapshot): Boolean
}

/** Owned by the BLE runtime thread. A durable reservation precedes each possible BLE write. */
internal class V1TextOutbox(
    private val storage: V1TextOutboxStorage,
    private val nowMillis: () -> Long,
) {
    private val loaded = storage.load()
    private var snapshot = loaded ?: V1TextOutboxSnapshot()
    var unavailable = loaded == null
        private set
    private var recipientToken = snapshot.recipient?.let { V1QuickStatusRecipient() }
    val recipient: V1QuickStatusRecipient? get() = recipientToken.takeUnless { unavailable }
    val entries: List<V1TextOutboxEntry> get() = snapshot.entries
    private var retryReadyAt = 0L

    init {
        // A process interruption can occur between a write and the terminal board event.
        if (entries.any { it.state == V1TextOutboxPhase.IN_FLIGHT }) {
            commit(snapshot.copy(entries = entries.map {
                if (it.state == V1TextOutboxPhase.IN_FLIGHT) it.copy(state = V1TextOutboxPhase.UNCONFIRMED) else it
            }))
        }
        waitBeforeRetry(waiting()?.attempts ?: 0)
    }

    fun bindRecipient(digest: String): Boolean {
        if (unavailable) return false
        if (digest == snapshot.recipient) return true
        val rebound = snapshot.entries.map {
            when (it.state) {
                V1TextOutboxPhase.WAITING -> it.copy(state = V1TextOutboxPhase.FAILED)
                V1TextOutboxPhase.IN_FLIGHT -> it.copy(state = V1TextOutboxPhase.UNCONFIRMED)
                else -> it
            }
        }
        if (!commit(V1TextOutboxSnapshot(digest, rebound))) return false
        recipientToken = V1QuickStatusRecipient()
        retryReadyAt = 0
        return true
    }

    fun enqueue(recipient: V1QuickStatusRecipient, text: String): Boolean {
        if (recipient !== this.recipient || V1TextComposer.refusal(text) != null) return false
        val lastId = entries.lastOrNull()?.id ?: 0L
        if (lastId == Long.MAX_VALUE) return false
        val retained = entries.toMutableList()
        if (retained.size >= V1_TEXT_OUTBOX_LIMIT) {
            val oldestFinished = retained.indexOfFirst {
                it.state != V1TextOutboxPhase.WAITING && it.state != V1TextOutboxPhase.IN_FLIGHT
            }
            if (oldestFinished < 0) return false
            retained.removeAt(oldestFinished)
        }
        retained.add(V1TextOutboxEntry(lastId + 1, text, V1TextOutboxPhase.WAITING, attempts = 0))
        return commit(snapshot.copy(entries = retained.toList()))
    }

    fun nextDelayMillis(): Long? {
        if (unavailable || waiting() == null || entries.any { it.state == V1TextOutboxPhase.IN_FLIGHT }) return null
        return (retryReadyAt - nowMillis()).coerceAtLeast(0)
    }

    fun beginAttempt(): V1TextOutboxEntry? {
        if (nextDelayMillis() != 0L) return null
        val entry = waiting() ?: return null
        val attempt = entry.copy(state = V1TextOutboxPhase.IN_FLIGHT, attempts = entry.attempts + 1)
        return attempt.takeIf { replace(attempt) }
    }

    fun finishAttempt(id: Long, result: V1TextOutboxResult) {
        val entry = entries.firstOrNull { it.id == id && it.state == V1TextOutboxPhase.IN_FLIGHT } ?: return
        val phase = when (result) {
            V1TextOutboxResult.SENT -> V1TextOutboxPhase.SENT
            V1TextOutboxResult.FAILED -> V1TextOutboxPhase.FAILED
            V1TextOutboxResult.UNCONFIRMED -> V1TextOutboxPhase.UNCONFIRMED
            V1TextOutboxResult.RETRYABLE -> if (entry.attempts < V1_TEXT_OUTBOX_MAX_ATTEMPTS) {
                V1TextOutboxPhase.WAITING
            } else V1TextOutboxPhase.FAILED
        }
        if (replace(entry.copy(state = phase))) {
            waitBeforeRetry(if (phase == V1TextOutboxPhase.WAITING) entry.attempts else 0)
        }
    }

    private fun waitBeforeRetry(attempts: Int) {
        retryReadyAt = nowMillis() + when (attempts) {
            1 -> 5_000L
            2 -> 15_000L
            else -> 0L
        }
    }

    private fun waiting() = entries.firstOrNull { it.state == V1TextOutboxPhase.WAITING }

    private fun replace(entry: V1TextOutboxEntry) =
        commit(snapshot.copy(entries = entries.map { if (it.id == entry.id) entry else it }))

    private fun commit(next: V1TextOutboxSnapshot): Boolean {
        if (unavailable || !storage.save(next)) {
            unavailable = true
            return false
        }
        snapshot = next
        return true
    }
}
