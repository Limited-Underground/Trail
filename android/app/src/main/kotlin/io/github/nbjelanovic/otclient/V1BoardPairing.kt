package io.github.nbjelanovic.otclient

import android.os.SystemClock
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.text.input.KeyboardType
import io.github.nbjelanovic.otprotocol.COMPANION_PAIRING_CODE_MAX
import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionEnrollmentRole
import java.security.SecureRandom
import kotlinx.coroutines.delay

// D-110: 5 minutes (was 2, D-087), the same as the board's request window.
internal const val BOARD_PAIRING_WINDOW_MILLIS = 300_000L

sealed interface BoardPairingState {
    data object Available : BoardPairingState
    data object Waiting : BoardPairingState
    data class Ready(val deadlineMillis: Long) : BoardPairingState
    data class Rejected(val reason: CompanionActionRejectReason) : BoardPairingState

    fun isBusy(nowMillis: Long): Boolean =
        this == Waiting || (this is Ready && nowMillis < deadlineMillis)
}

internal fun pairingCodeNumber(text: String): Int? =
    if (text.length == 6 && text.all { it in '0'..'9' }) text.toInt().takeIf { it != 0 } else null

internal fun pairingCodeText(code: Int): String = code.toString().padStart(6, '0')

private fun newPairingCode(): Int = SecureRandom().nextInt(COMPANION_PAIRING_CODE_MAX) + 1

internal fun boardPairingRejection(reason: CompanionActionRejectReason): String = when (reason) {
    CompanionActionRejectReason.UNSUPPORTED_ACTION -> "Your board does not support this way of pairing."
    CompanionActionRejectReason.UNAVAILABLE -> "Your board is unavailable for pairing right now."
    CompanionActionRejectReason.POLICY_DENIED -> "Your board did not allow pairing."
    CompanionActionRejectReason.BUSY -> "Your board already has a pairing request. Wait before trying again."
    else -> "Your board couldn't start pairing. Try again."
}

@Composable
internal fun V1BoardPairingPanel(
    state: BoardPairingState,
    generateCode: () -> Int = ::newPairingCode,
    clockMillis: () -> Long = SystemClock::elapsedRealtime,
    retainedInitiatorCode: String? = null,
    onInitiatorCodeSubmitted: ((String?) -> Unit)? = null,
    submit: (CompanionActionRequest) -> Boolean,
) {
    var role by remember { mutableStateOf<CompanionEnrollmentRole?>(null) }
    var codeText by remember { mutableStateOf("") }
    var submittedInitiatorCode by remember { mutableStateOf<String?>(null) }
    var sendFailed by remember { mutableStateOf(false) }
    val deadline = (state as? BoardPairingState.Ready)?.deadlineMillis
    var remaining by remember(deadline) { mutableLongStateOf(deadline?.let { (it - clockMillis()).coerceAtLeast(0) } ?: 0) }
    LaunchedEffect(deadline) {
        if (deadline != null) {
            while (true) {
                remaining = (deadline - clockMillis()).coerceAtLeast(0)
                if (remaining == 0L) break
                delay(minOf(remaining, 1000))
            }
        }
    }

    val attemptActive = state == BoardPairingState.Waiting ||
        (state is BoardPairingState.Ready && remaining > 0)
    LaunchedEffect(attemptActive) {
        if (!attemptActive) submittedInitiatorCode = null
    }

    Text("Pair two boards", style = MaterialTheme.typography.titleMedium)
    val visibleInitiatorCode = if (onInitiatorCodeSubmitted != null) retainedInitiatorCode else submittedInitiatorCode
    if (attemptActive) visibleInitiatorCode?.let { code ->
        Text(code, style = MaterialTheme.typography.displaySmall)
    }
    when (state) {
        BoardPairingState.Available -> Unit
        BoardPairingState.Waiting -> {
            Text("Waiting for your board to answer.")
            return
        }
        is BoardPairingState.Ready -> {
            if (remaining > 0) {
                Text("Your board is ready to pair. Keep both boards close, then compare the long codes on the board screens and press the button on each.")
                val seconds = (remaining + 999) / 1000
                Text("Pairing window: ${seconds / 60}:${(seconds % 60).toString().padStart(2, '0')} left")
                return
            }
            Text("The pairing window ended.")
        }
        is BoardPairingState.Rejected -> if (role == null) Text(boardPairingRejection(state.reason))
    }

    if (sendFailed) Text("Couldn't send the pairing request. Check the connection and try again.")
    when (val selectedRole = role) {
        null -> {
            OutlinedButton(onClick = {
                sendFailed = false
                codeText = pairingCodeText(generateCode())
                role = CompanionEnrollmentRole.INITIATOR
            }) { Text("Start pairing") }
            OutlinedButton(onClick = {
                sendFailed = false
                role = CompanionEnrollmentRole.RESPONDER
            }) { Text("Join pairing") }
        }
        else -> {
            if (selectedRole == CompanionEnrollmentRole.INITIATOR) {
                Text(codeText, style = MaterialTheme.typography.displaySmall)
                Text("On the other phone, tap Join pairing and enter this code. Then tap Send to my board.")
            } else {
                OutlinedTextField(
                    value = codeText,
                    onValueChange = { value -> codeText = value.filter { it in '0'..'9' }.take(6) },
                    label = { Text("6-digit pairing code") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    singleLine = true,
                )
            }
            val code = pairingCodeNumber(codeText)
            OutlinedButton(onClick = {
                val submittedCode = codeText
                codeText = ""
                role = null
                val starterCode = submittedCode.takeIf { selectedRole == CompanionEnrollmentRole.INITIATOR }
                onInitiatorCodeSubmitted?.invoke(starterCode)
                sendFailed = !submit(CompanionActionRequest(
                    CompanionActionKind.START_ENROLLMENT, enrollmentRole = selectedRole, pairingCode = checkNotNull(code),
                ))
                submittedInitiatorCode = starterCode.takeIf { !sendFailed && onInitiatorCodeSubmitted == null }
                if (sendFailed) onInitiatorCodeSubmitted?.invoke(null)
            }, enabled = code != null) { Text("Send to my board") }
            TextButton(onClick = {
                codeText = ""
                role = null
            }) { Text("Cancel") }
        }
    }
}
