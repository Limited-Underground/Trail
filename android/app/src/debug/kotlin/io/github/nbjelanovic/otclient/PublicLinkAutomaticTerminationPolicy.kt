package io.github.nbjelanovic.otclient

/**
 * Debug/test-only phone timing policy for the already-built 15-second target window.
 *
 * The phone never requests disconnect in OT-085B. A disconnect outside this
 * deliberately wider callback-observation interval is denied without publishing
 * the measured duration.
 */
internal object PublicLinkAutomaticTerminationPolicy {
    const val TARGET_WINDOW_MILLIS = 15_000L
    private const val EARLY_CALLBACK_ALLOWANCE_MILLIS = 2_000L
    private const val LATE_CALLBACK_ALLOWANCE_MILLIS = 5_000L
    private const val WAIT_MARGIN_MILLIS = 4_000L
    const val MIN_ACCEPTED_MILLIS =
        TARGET_WINDOW_MILLIS - EARLY_CALLBACK_ALLOWANCE_MILLIS
    const val MAX_ACCEPTED_MILLIS =
        TARGET_WINDOW_MILLIS + LATE_CALLBACK_ALLOWANCE_MILLIS
    const val WAIT_MILLIS = MAX_ACCEPTED_MILLIS + WAIT_MARGIN_MILLIS

    fun acceptsElapsed(elapsedMillis: Long): Boolean =
        elapsedMillis in MIN_ACCEPTED_MILLIS..MAX_ACCEPTED_MILLIS
}
