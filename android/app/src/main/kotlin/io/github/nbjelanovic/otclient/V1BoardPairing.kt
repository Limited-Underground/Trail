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
import io.github.nbjelanovic.otprotocol.CompanionPairingResult
import io.github.nbjelanovic.otprotocol.CompanionPairingState
import io.github.nbjelanovic.otprotocol.CompanionPairingReason
import java.security.SecureRandom
import kotlinx.coroutines.delay

// D-110: 5 minutes (was 2, D-087), the same as the board's request window.
internal const val BOARD_PAIRING_WINDOW_MILLIS = 300_000L

sealed interface BoardPairingState {
    data object Available : BoardPairingState
    data object Waiting : BoardPairingState
    data object Unknown : BoardPairingState
    data class Result(val result: CompanionPairingResult, val historical: Boolean = false) : BoardPairingState
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

internal fun boardPairingResultText(result: CompanionPairingResult): Pair<String, String?> {
    val peer = result.peerShortId.toString(16).uppercase().padStart(4, '0')
    return when (result.state) {
        CompanionPairingState.PAIRED_LOCAL -> "Paired with board $peer" to
            "Saved on this board. Check the result on the other board."
        CompanionPairingState.PAIRED_BOTH -> "Both boards paired" to "Paired with board $peer"
        CompanionPairingState.INCOMPLETE -> "Pairing incomplete." to
            "Tap Resume pairing on both phones.\nTap Start pairing on the phone that started it before.\nTap Join pairing on the other phone and enter the same new code."
        CompanionPairingState.CANCELLED -> "Pairing was cancelled." to
            "The phone disconnected before pairing finished."
        CompanionPairingState.FAILED -> when (result.reason) {
            CompanionPairingReason.TIMED_OUT -> "Pairing timed out." to
                "Start again and confirm on both MeshTrail boards."
            CompanionPairingReason.NO_REPLY -> "The other board didn't answer." to
                "Bring the boards closer and check that both phones used the same pairing code."
            CompanionPairingReason.STORAGE -> "This board couldn't save the pairing." to
                "Check the board screen before trying again."
            CompanionPairingReason.RADIO_UNAVAILABLE -> "This board's radio stopped during pairing." to null
            else -> "Pairing couldn't finish." to "Check the messages on both boards."
        }
        CompanionPairingState.RUNNING -> "Pairing in progress." to
            "Compare the codes on both devices before confirming."
        CompanionPairingState.NONE -> "Pairing result unavailable." to "Check your MeshTrail board screen."
    }
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
    var resumeRequested by remember(state) { mutableStateOf(false) }
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
                Text("Keep the devices close.\nCompare the long codes on both screens.\nPress each device's button if the codes match.")
                val seconds = (remaining + 999) / 1000
                Text("Pairing window: ${seconds / 60}:${(seconds % 60).toString().padStart(2, '0')} left")
                return
            }
            Text("Pairing result unavailable.")
            Text("Check your MeshTrail board screen.")
        }
        is BoardPairingState.Rejected -> if (role == null) Text(boardPairingRejection(state.reason))
        BoardPairingState.Unknown -> {
            Text("Pairing result unavailable.")
            Text("Check your MeshTrail board screen.")
        }
        is BoardPairingState.Result -> {
            if (state.historical) Text("Last pairing attempt")
            val (title, detail) = boardPairingResultText(state.result)
            Text(title)
            detail?.let { Text(it) }
            if (state.result.state == CompanionPairingState.INCOMPLETE && !resumeRequested) {
                OutlinedButton(onClick = { resumeRequested = true }) { Text("Resume pairing") }
                return
            }
        }
    }

    if (sendFailed) Text("Couldn't send the pairing request.\nCheck the connection and try again.")
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
                Text("Tap Join pairing on the other phone and enter this code.\nThen tap Send to my board.")
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
