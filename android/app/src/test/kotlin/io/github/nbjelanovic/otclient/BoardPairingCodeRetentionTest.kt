package io.github.nbjelanovic.otclient

import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertSame
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionPairingReason
import io.github.nbjelanovic.otprotocol.CompanionPairingResult
import io.github.nbjelanovic.otprotocol.CompanionPairingState

@org.junit.runner.RunWith(org.robolectric.RobolectricTestRunner::class)
@org.robolectric.annotation.Config(sdk = [35], qualifiers = "w360dp-h800dp-mdpi")
class BoardPairingCodeRetentionTest {
    private val scope = BoardPairingCodeScope("endpoint", 1L)

    @Test fun localCompletionClearsKeptStarterCode() {
        assertTerminalClearsKeptCode(terminalResult(CompanionPairingState.PAIRED_LOCAL))
    }

    @Test fun bilateralCompletionClearsKeptStarterCode() {
        assertTerminalClearsKeptCode(terminalResult(CompanionPairingState.PAIRED_BOTH))
    }

    @Test fun refusedRequestClearsKeptStarterCode() {
        assertTerminalClearsKeptCode(BoardPairingState.Rejected(CompanionActionRejectReason.BUSY))
    }

    @Test fun failedPairingClearsKeptStarterCode() {
        assertTerminalClearsKeptCode(terminalResult(CompanionPairingState.FAILED, CompanionPairingReason.STORAGE))
    }

    @Test fun cancelledPairingClearsKeptStarterCode() {
        assertTerminalClearsKeptCode(terminalResult(CompanionPairingState.CANCELLED, CompanionPairingReason.REQUEST_ENDED))
    }

    @Test fun incompletePairingClearsKeptStarterCode() {
        assertTerminalClearsKeptCode(terminalResult(CompanionPairingState.INCOMPLETE, CompanionPairingReason.STORAGE))
    }

    private fun terminalResult(
        state: CompanionPairingState,
        reason: CompanionPairingReason = CompanionPairingReason.NONE,
    ): BoardPairingState.Result {
        val peer = if (state == CompanionPairingState.PAIRED_LOCAL || state == CompanionPairingState.PAIRED_BOTH) 0x34ab else 0
        return BoardPairingState.Result(CompanionPairingResult(
            state, reason, peerShortId = peer, originExchangeId = 7,
            originSessionNonce = scope.sessionNonce, revision = 2,
        ))
    }

    private fun assertTerminalClearsKeptCode(terminal: BoardPairingState) {
        val model = BoardPairingCodeRetention()
        model.retain(scope, "123456", 100L)
        assertEquals("123456", model.current(scope, BoardPairingState.Waiting, 101L))
        assertNull(model.current(scope, terminal, 102L))
        assertNull(model.expiresAtMillis)
        assertNull(model.current(scope, BoardPairingState.Waiting, 103L))
    }

    @Test fun activityObservationGapsPreserveTheAttemptButBoundDisconnectClearsIt() {
        val model = BoardPairingCodeRetention()
        model.retain(scope, "000042", 100L)
        model.observe(TrailAppUiState.ChooseMode, 101L)
        val unbound = TrailAppUiState.BluetoothDevice(
            BleRuntimeState.Idle, NearbyDevicesPermissionState.GRANTED,
            false, false, DeviceAuthorizationUiState.None,
            serviceState = ConnectedDeviceServiceUiState.START_REQUIRED,
        )
        model.observe(unbound, 102L)
        assertEquals("000042", model.current(scope, BoardPairingState.Waiting, 103L))
        model.observe(unbound.copy(serviceState = ConnectedDeviceServiceUiState.RUNNING), 104L)
        assertNull(model.expiresAtMillis)
    }

    @Test
    fun waitingAndReadyKeepTheCodeUntilTheEarlierDeadline() {
        val model = BoardPairingCodeRetention()
        model.retain(scope, "000042", 100L)
        assertEquals(300100L, model.expiresAtMillis)
        assertEquals("000042", model.current(scope, BoardPairingState.Waiting, 101L))
        assertEquals("000042", model.current(scope, BoardPairingState.Ready(150L), 120L))
        assertEquals(150L, model.expiresAtMillis)
        assertNull(model.current(scope, BoardPairingState.Ready(150L), 150L))
        assertNull(model.expiresAtMillis)
    }

    @Test
    fun lateReadyCannotExtendOriginalRequestLifetime() {
        val model = BoardPairingCodeRetention()
        model.retain(scope, "123456", 10L)
        assertEquals("123456", model.current(scope, BoardPairingState.Ready(500000L), 20L))
        assertEquals(300010L, model.expiresAtMillis)
        model.expire(300010L)
        assertNull(model.current(scope, BoardPairingState.Ready(500000L), 300011L))
    }

    @Test
    fun differentEndpointOrSessionAndTerminalStatesDiscardTheCode() {
        val states = listOf(
            BoardPairingState.Available,
            BoardPairingState.Rejected(CompanionActionRejectReason.POLICY_DENIED),
        )
        for (state in states) {
            val model = BoardPairingCodeRetention()
            model.retain(scope, "123456", 10L)
            assertNull(model.current(scope, state, 11L))
        }
        for (other in listOf(scope.copy(endpointToken = "another"), scope.copy(sessionNonce = 2L))) {
            val model = BoardPairingCodeRetention()
            model.retain(scope, "123456", 10L)
            assertNull(model.current(other, BoardPairingState.Waiting, 11L))
            assertNull(model.current(scope, BoardPairingState.Waiting, 12L))
        }
    }

    @Test
    fun disconnectRollbackOverflowAndMalformedCodesDiscardTheCode() {
        val model = BoardPairingCodeRetention()
        model.retain(scope, "123456", 100L)
        model.reconcile(BleRuntimeState.Idle, 101L)
        assertNull(model.expiresAtMillis)
        model.retain(scope, "123456", 100L)
        assertNull(model.current(scope, BoardPairingState.Waiting, 99L))
        model.retain(scope, "123456", Long.MAX_VALUE)
        assertNull(model.expiresAtMillis)
        for (code in listOf("", "000000", "12345", "1234567", "12a456")) {
            model.retain(scope, code, 100L)
            assertNull(model.expiresAtMillis)
        }
    }

    @Test
    fun replacementAttemptReplacesTheCodeAndFreshProcessDoesNotRecoverIt() {
        val model = BoardPairingCodeRetention()
        model.retain(scope, "123456", 10L)
        model.retain(scope, "654321", 20L)
        assertEquals("654321", model.current(scope, BoardPairingState.Waiting, 21L))
        assertNull(BoardPairingCodeRetention().current(scope, BoardPairingState.Waiting, 21L))
    }

    @Test
    fun retainedViewModelStoreKeepsTheAttemptAndClearingItDiscardsTheCode() {
        val store = ViewModelStore()
        val firstOwner = object : ViewModelStoreOwner { override val viewModelStore = store }
        val first = ViewModelProvider(firstOwner)[BoardPairingCodeRetention::class.java]
        first.retain(scope, "000042", 10L)
        val replacementOwner = object : ViewModelStoreOwner { override val viewModelStore = store }
        val restored = ViewModelProvider(replacementOwner)[BoardPairingCodeRetention::class.java]
        assertSame(first, restored)
        assertEquals("000042", restored.current(scope, BoardPairingState.Waiting, 11L))
        store.clear()
        assertNull(restored.expiresAtMillis)
    }
}
