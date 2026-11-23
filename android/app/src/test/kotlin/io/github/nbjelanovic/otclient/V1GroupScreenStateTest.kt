package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1GroupScreenStateTest {
    @Test fun joinRequiresTheCurrentVerifiedOfferAndSubmissionAuthority() {
        val offer = V1GroupJoinOffer("Hiking group", V1GroupLocationRule.MEMBER_OPTIONAL)
        val choice = V1GroupJoinChoice.SHARE_LOCATION
        assertFalse(V1GroupScreenState(canSubmit = true).canCompleteJoin(offer, choice))
        val ready = V1GroupScreenState(canSubmit = true, verifiedJoin = V1GroupVerifiedJoinState(offer))
        assertTrue(ready.canCompleteJoin(offer, choice))
        assertFalse(ready.canCompleteJoin(V1GroupJoinOffer(offer.groupName, offer.locationRule), choice))
        assertFalse(ready.copy(canSubmit = false).canCompleteJoin(offer, choice))
        assertNull(ready.copy(setupAvailable = false).currentJoinOffer(offer))
    }

    @Test fun choicesAndFreshFixAreScopedToTheOfferedRule() {
        val off = V1GroupJoinOffer("Off group", V1GroupLocationRule.OFF)
        val preferred = V1GroupJoinOffer("Preferred group", V1GroupLocationRule.MEMBER_OPTIONAL)
        val required = V1GroupJoinOffer("Required group", V1GroupLocationRule.REQUIRED)
        val share = V1GroupJoinChoice.SHARE_LOCATION
        val without = V1GroupJoinChoice.WITHOUT_LOCATION
        val state = V1GroupScreenState(canSubmit = true, verifiedJoin = V1GroupVerifiedJoinState(off))
        assertTrue(state.canCompleteJoin(off, without))
        assertFalse(state.canCompleteJoin(off, share))

        val preferredState = state.copy(verifiedJoin = V1GroupVerifiedJoinState(preferred))
        assertTrue(preferredState.canCompleteJoin(preferred, share))
        assertTrue(preferredState.canCompleteJoin(preferred, without))
        val waiting = state.copy(verifiedJoin = V1GroupVerifiedJoinState(required))
        assertFalse(waiting.canCompleteJoin(required, share))
        val fixed = waiting.copy(verifiedJoin = V1GroupVerifiedJoinState(required, freshFixAvailable = true))
        assertTrue(fixed.canCompleteJoin(required, share))
        assertFalse(fixed.canCompleteJoin(required, without))
        assertFalse(fixed.canCompleteJoin(preferred, share))
    }
}
