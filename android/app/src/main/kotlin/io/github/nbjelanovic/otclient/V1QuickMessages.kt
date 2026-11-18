package io.github.nbjelanovic.otclient

/** Local templates, not queue entries or delivery receipts. */
object V1QuickMessages {
    const val MAX_CUSTOM = 12
    const val MAX_CHARS = 160
    val builtIn = listOf("I'm OK", "Stopping here", "On my way", "Please check in")

    fun normalize(value: String): String? = value.trim().takeIf {
        it.isNotEmpty() && it.length <= MAX_CHARS && it.none(Char::isISOControl)
    }

    fun add(current: List<String>, value: String): List<String>? {
        val normalized = normalize(value) ?: return null
        if (current.size >= MAX_CUSTOM || normalized in current) return null
        return current + normalized
    }

    /** The original value fences a restored editor against a changed or reordered entry. */
    fun edit(current: List<String>, index: Int, original: String, value: String): List<String>? {
        if (current.size > MAX_CUSTOM || index !in current.indices || current[index] != original) return null
        val normalized = normalize(value) ?: return null
        if (current.indices.any { it != index && current[it] == normalized }) return null
        return current.toMutableList().apply { this[index] = normalized }
    }

    /** Preserve the existing numbered-key load policy; a read failure is not an empty store. */
    internal fun load(read: (Int) -> String?): List<String>? = try {
        (0 until MAX_CUSTOM).mapNotNull { read(it)?.let(::normalize) }.distinct()
    } catch (_: Exception) {
        null
    }

    /** Explicit local save only. A refused/uncertain write must not clear the UI's draft. */
    internal fun save(current: List<String>, write: (List<String>) -> Boolean): Boolean {
        if (current.size > MAX_CUSTOM) return false
        val snapshot = current.toList()
        if (snapshot.any { normalize(it) != it } || snapshot.distinct().size != snapshot.size) return false
        return try { write(snapshot) } catch (_: Exception) { false }
    }

    fun move(current: List<String>, index: Int, delta: Int): List<String> {
        val target = index + delta
        if (delta !in listOf(-1, 1) || index !in current.indices || target !in current.indices) return current
        return current.toMutableList().apply { add(target, removeAt(index)) }
    }
}
