package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

// The policy under test is intentionally available only in the debug source set.
class PublicLinkAutomaticTerminationPolicyTest {
    @Test
    fun onlyBoundedTargetTerminationTimingIsAccepted() {
        assertFalse(PublicLinkAutomaticTerminationPolicy.acceptsElapsed(-1))
        assertFalse(
            PublicLinkAutomaticTerminationPolicy.acceptsElapsed(
                PublicLinkAutomaticTerminationPolicy.MIN_ACCEPTED_MILLIS - 1,
            ),
        )
        assertTrue(
            PublicLinkAutomaticTerminationPolicy.acceptsElapsed(
                PublicLinkAutomaticTerminationPolicy.MIN_ACCEPTED_MILLIS,
            ),
        )
        assertTrue(
            PublicLinkAutomaticTerminationPolicy.acceptsElapsed(
                PublicLinkAutomaticTerminationPolicy.TARGET_WINDOW_MILLIS,
            ),
        )
        assertTrue(
            PublicLinkAutomaticTerminationPolicy.acceptsElapsed(
                PublicLinkAutomaticTerminationPolicy.MAX_ACCEPTED_MILLIS,
            ),
        )
        assertFalse(
            PublicLinkAutomaticTerminationPolicy.acceptsElapsed(
                PublicLinkAutomaticTerminationPolicy.MAX_ACCEPTED_MILLIS + 1,
            ),
        )
        assertTrue(
            PublicLinkAutomaticTerminationPolicy.WAIT_MILLIS >
                PublicLinkAutomaticTerminationPolicy.MAX_ACCEPTED_MILLIS,
        )
    }
}
