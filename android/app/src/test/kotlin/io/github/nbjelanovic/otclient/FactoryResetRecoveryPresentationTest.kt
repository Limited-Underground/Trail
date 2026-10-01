package io.github.nbjelanovic.otclient

import kotlin.test.*

class FactoryResetRecoveryPresentationTest {
    @Test fun onlyTheUnconfirmedSystemBondOutcomeOffersManualCleanupGuidance() {
        val cleared=factoryResetRecoveryPresentation(BleRuntimeState.FactoryResetComplete(false))
        val retained=factoryResetRecoveryPresentation(BleRuntimeState.FactoryResetComplete(true))
        assertNull(cleared.bondGuidance)
        assertNotNull(retained.bondGuidance)
        assertEquals(cleared.detail,retained.detail)
    }

    @Test fun freshSetupRequiresTheExactLiveVerifiedCompletionAndOnlyDispatchesAScan() {
        val expected=BleRuntimeState.FactoryResetComplete(true)
        var live: BleRuntimeState?=expected
        var scans=0;var reads=0
        val delayed={ requestFreshSetupAfterReset(expected,{ reads++;live },{ scans++ }) }
        assertTrue(delayed());assertEquals(1,scans);assertSame(expected,live)
        for(replacement in listOf(null,BleRuntimeState.Idle,BleRuntimeState.FactoryResetComplete(true),
            BleRuntimeState.FactoryResetComplete(false))) {
            live=replacement;assertFalse(delayed());assertEquals(1,scans)
        }
        assertEquals(5,reads)
    }
}
