package io.github.nbjelanovic.otclient

import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue

private enum class GroupInvitationPractice { CLOSED, READY, INVITED, REVIEWING, JOINED }

/** In-memory invite and join simulation. It has no connection or board state input. */
@Composable
internal fun GroupScreenVariantContent() {
    var stage by remember { mutableStateOf(GroupInvitationPractice.CLOSED) }
    if (stage == GroupInvitationPractice.CLOSED) {
        OutlinedButton(onClick = { stage = GroupInvitationPractice.READY }) {
            Text("Practice inviting and joining (debug)")
        }
        return
    }
    Text("Practice mode. These actions use sample data and do not change your device.")
    when (stage) {
        GroupInvitationPractice.READY -> Button(onClick = { stage = GroupInvitationPractice.INVITED }) {
            Text("Create sample invitation")
        }
        GroupInvitationPractice.INVITED -> {
            Text("Sample invitation from Camp group")
            Button(onClick = { stage = GroupInvitationPractice.REVIEWING }) { Text("Review sample invitation") }
        }
        GroupInvitationPractice.REVIEWING -> {
            Text("Camp group · Location sharing is optional")
            Button(onClick = { stage = GroupInvitationPractice.JOINED }) { Text("Join sample group") }
        }
        GroupInvitationPractice.JOINED -> Text("Joined Camp group in practice mode")
        GroupInvitationPractice.CLOSED -> Unit
    }
    OutlinedButton(onClick = { stage = GroupInvitationPractice.CLOSED }) { Text("Close practice mode") }
}
