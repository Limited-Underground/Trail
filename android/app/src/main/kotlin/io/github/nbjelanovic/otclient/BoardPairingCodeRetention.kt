package io.github.nbjelanovic.otclient

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStoreOwner
import androidx.activity.compose.LocalActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import android.os.SystemClock
import kotlinx.coroutines.delay
import androidx.compose.runtime.remember

/** Presentation scope only. It cannot authorize or resume a pairing request. */
internal data class BoardPairingCodeScope(val endpointToken: String, val sessionNonce: Long)

/** Keeps the starter's submitted code only while its original in-memory attempt is live. */
internal class BoardPairingCodeRetention : ViewModel() {
    private data class Entry(
        val scope: BoardPairingCodeScope,
        val code: String,
        val deadlineMillis: Long,
    )

    private var entry by mutableStateOf<Entry?>(null)
    private var lastObservedMillis: Long? = null

    val expiresAtMillis: Long?
        get() = entry?.deadlineMillis

    fun retain(scope: BoardPairingCodeScope, code: String, nowMillis: Long) {
        clear()
        if (pairingCodeNumber(code) == null || nowMillis < 0 ||
            nowMillis > Long.MAX_VALUE - BOARD_PAIRING_WINDOW_MILLIS) return
        entry = Entry(scope, code, nowMillis + BOARD_PAIRING_WINDOW_MILLIS)
        lastObservedMillis = nowMillis
    }

    fun current(scope: BoardPairingCodeScope, state: BoardPairingState, nowMillis: Long): String? {
        expire(nowMillis)
        val current = entry ?: return null
        if (current.scope != scope || !state.isBusy(nowMillis)) {
            clear()
            return null
        }
        if (state is BoardPairingState.Ready && state.deadlineMillis < current.deadlineMillis) {
            entry = current.copy(deadlineMillis = state.deadlineMillis)
        }
        return entry?.code
    }

    fun observe(state: TrailAppUiState, nowMillis: Long) {
        expire(nowMillis)
        val bluetooth = state as? TrailAppUiState.BluetoothDevice ?: return
        // Unbinding an Activity publishes an observation gap, with no service readback.
        if (bluetooth.serviceState == ConnectedDeviceServiceUiState.RUNNING ||
            bluetooth.serviceState == ConnectedDeviceServiceUiState.BOUND_ONLY) {
            reconcile(bluetooth.runtimeState, nowMillis)
        }
    }

    fun reconcile(runtimeState: BleRuntimeState, nowMillis: Long) {
        val session = (runtimeState as? BleRuntimeState.Ready)?.session
        val pairing = session?.boardPairing
        if (session == null || pairing == null) {
            clear()
            return
        }
        current(
            BoardPairingCodeScope(session.companion.endpointToken, session.sessionNonce),
            pairing,
            nowMillis,
        )
    }

    fun expire(nowMillis: Long) {
        val current = entry ?: return
        val previous = lastObservedMillis
        if (nowMillis < 0 || (previous != null && nowMillis < previous) ||
            nowMillis >= current.deadlineMillis) {
            clear()
        } else {
            lastObservedMillis = nowMillis
        }
    }

    fun clear() {
        entry = null
        lastObservedMillis = null
    }

    override fun onCleared() {
        clear()
    }
}

@Composable
internal fun ExpireBoardPairingCode(
    retention: BoardPairingCodeRetention,
    clockMillis: () -> Long = SystemClock::elapsedRealtime,
) {
    LaunchedEffect(retention.expiresAtMillis) {
        while (retention.expiresAtMillis != null) {
            retention.expire(clockMillis())
            val deadline = retention.expiresAtMillis ?: break
            delay(minOf(1_000L, (deadline - clockMillis()).coerceAtLeast(1L)))
        }
    }
}

@Composable
internal fun rememberBoardPairingCodeRetention(): BoardPairingCodeRetention {
    val owner = LocalActivity.current as? ViewModelStoreOwner
    return remember(owner) {
        if (owner == null) BoardPairingCodeRetention()
        else ViewModelProvider(owner)[BoardPairingCodeRetention::class.java]
    }
}
