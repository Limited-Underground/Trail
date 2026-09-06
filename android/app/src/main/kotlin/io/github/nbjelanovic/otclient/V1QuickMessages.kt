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

    fun move(current: List<String>, index: Int, delta: Int): List<String> {
        val target = index + delta
        if (delta !in listOf(-1, 1) || index !in current.indices || target !in current.indices) return current
        return current.toMutableList().apply { add(target, removeAt(index)) }
    }
}
