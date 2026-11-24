package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionDirectBinding
import io.github.nbjelanovic.otprotocol.CompanionDirectCommand
import io.github.nbjelanovic.otprotocol.CompanionDirectConfigurationCodec
import io.github.nbjelanovic.otprotocol.CompanionDirectDelivery
import io.github.nbjelanovic.otprotocol.CompanionDirectOperation
import io.github.nbjelanovic.otprotocol.CompanionDirectResult
import io.github.nbjelanovic.otprotocol.CompanionDirectRole
import io.github.nbjelanovic.otprotocol.CompanionDirectState
import io.github.nbjelanovic.otprotocol.CompanionDirectStatus
import io.github.nbjelanovic.otprotocol.CompanionDirectText

/** Where an invitation to talk outside a group stands (A6). The board owns consent; this only mirrors it. */
sealed interface V1DirectInvite {
    data object None : V1DirectInvite
    data class Waiting(val role: CompanionDirectRole, val code: Int) : V1DirectInvite {
        override fun toString() = "Waiting(role=$role, code=redacted)"
    }
    /** Both people compare [checkCode] before either accepts. */
    data class Review(val binding: CompanionDirectBinding) : V1DirectInvite {
        val checkCode: String get() = binding.peerCheckCode()
    }
    /** This board said yes; waiting for the other board to confirm. */
    data class Finishing(val binding: CompanionDirectBinding) : V1DirectInvite
    data class Ended(val outcome: V1DirectOutcome) : V1DirectInvite
}

enum class V1DirectOutcome { CONNECTED, DECLINED, BLOCKED, FULL, EXPIRED, STALE, REFUSED, UNAVAILABLE }

data class V1DirectConversation(
    val slot: Int,
    val state: CompanionDirectState,
    val generation: ULong,
    val binding: CompanionDirectBinding,
) {
    /** Text may be sent only on a Ready conversation with a live traffic generation from the board. */
    val composerEnabled: Boolean get() = state == CompanionDirectState.READY && generation != 0uL
    override fun toString() = "V1DirectConversation(slot=$slot, state=$state)"
}

data class V1DirectMessage(val messageId: UInt, val outgoing: Boolean, val text: String, val delivery: CompanionDirectDelivery?) {
    override fun toString() = "V1DirectMessage(outgoing=$outgoing, delivery=$delivery, text=redacted)"
}

/**
 * The phone side of conversations outside a group. Pure state: every method returns the next
 * command to send to the protected board connection, or null. The board's answers come back
 * through [onResult]. Drafts stay on the phone and are only offered once the exact same
 * conversation (peer and scope) is Ready; a different peer in a slot drops them.
 */
class V1DirectConversations {
    var invite: V1DirectInvite = V1DirectInvite.None
        private set
    private val slots = arrayOfNulls<V1DirectConversation>(CompanionDirectConfigurationCodec.CONVERSATIONS)
    private val drafts = HashMap<CompanionDirectBinding, String>()
    private val messages = HashMap<CompanionDirectBinding, MutableList<V1DirectMessage>>()
    private var nextMessageId = 1u

    val conversations: List<V1DirectConversation> get() = slots.filterNotNull()

    fun conversation(binding: CompanionDirectBinding) = slots.firstOrNull { it?.binding == binding }
    fun messages(binding: CompanionDirectBinding): List<V1DirectMessage> = messages[binding].orEmpty().toList()

    /** A draft is kept per conversation but only shown with an enabled composer. */
    fun draft(binding: CompanionDirectBinding): String? =
        drafts[binding]?.takeIf { conversation(binding)?.composerEnabled == true }

    fun keepDraft(binding: CompanionDirectBinding, text: String) {
        if (text.isEmpty()) drafts.remove(binding) else drafts[binding] = text
    }

    fun start(role: CompanionDirectRole, code: Int): CompanionDirectCommand? {
        if (invite is V1DirectInvite.Waiting || invite is V1DirectInvite.Review || invite is V1DirectInvite.Finishing) return null
        if (code !in CompanionDirectConfigurationCodec.CODE_MIN..CompanionDirectConfigurationCodec.CODE_MAX) return null
        invite = V1DirectInvite.Waiting(role, code)
        return CompanionDirectCommand.Begin(role, code)
    }

    /** Ask the board which peer the authenticated attempt reached. Repeat while it is busy. */
    fun pollReview(): CompanionDirectCommand? = CompanionDirectCommand.Review.takeIf { invite is V1DirectInvite.Waiting }

    fun accept(): CompanionDirectCommand? =
        (invite as? V1DirectInvite.Review)?.let { CompanionDirectCommand.Accept(it.binding) }

    fun decline(): CompanionDirectCommand? =
        if (invite is V1DirectInvite.Waiting || invite is V1DirectInvite.Review) CompanionDirectCommand.Decline else null

    fun pollFinish(): CompanionDirectCommand? =
        (invite as? V1DirectInvite.Finishing)?.let { CompanionDirectCommand.Finish(it.binding) }

    fun clearOutcome() {
        if (invite is V1DirectInvite.Ended) invite = V1DirectInvite.None
    }

    fun refresh(): List<CompanionDirectCommand> =
        (0 until CompanionDirectConfigurationCodec.CONVERSATIONS).map { CompanionDirectCommand.Read(it) }

    fun end(binding: CompanionDirectBinding): CompanionDirectCommand? =
        conversation(binding)?.let { CompanionDirectCommand.End(binding) }

    fun block(binding: CompanionDirectBinding): CompanionDirectCommand {
        // Blocking is allowed from a pending review too; the board refuses anything it cannot bind.
        return CompanionDirectCommand.Block(binding)
    }

    /** Null unless the composer is enabled; the board checks the generation again. */
    fun send(binding: CompanionDirectBinding, text: String): CompanionDirectCommand? {
        val conversation = conversation(binding)?.takeIf { it.composerEnabled } ?: return null
        val id = nextMessageId
        val command = CompanionDirectCommand.Send(CompanionDirectText(conversation.slot, conversation.generation, id, text))
        if (CompanionDirectConfigurationCodec.encode(command) == null) return null
        nextMessageId = if (id == UInt.MAX_VALUE) 1u else id + 1u
        messages.getOrPut(binding) { mutableListOf() } += V1DirectMessage(id, true, text, null)
        drafts.remove(binding)
        return command
    }

    /** The protected connection dropped: board state must be read again, and invites start over. */
    fun disconnected() {
        slots.fill(null)
        if (invite !is V1DirectInvite.Ended) invite = V1DirectInvite.None
    }

    fun onResult(result: CompanionDirectResult): CompanionDirectCommand? = when (result) {
        is CompanionDirectResult.Plain -> onPlain(result)
        is CompanionDirectResult.Reviewed -> {
            if (invite is V1DirectInvite.Waiting) invite = V1DirectInvite.Review(result.binding)
            null
        }
        is CompanionDirectResult.Conversation -> {
            onConversation(result)
            null
        }
        is CompanionDirectResult.Received -> {
            val conversation = slots.getOrNull(result.text.slot)
            if (conversation != null && conversation.composerEnabled && conversation.generation == result.text.generation) {
                messages.getOrPut(conversation.binding) { mutableListOf() } +=
                    V1DirectMessage(result.text.messageId, false, result.text.text, null)
            }
            null
        }
        is CompanionDirectResult.Delivery -> {
            val conversation = slots.getOrNull(result.slot)
            if (conversation != null && conversation.generation == result.generation) {
                messages[conversation.binding]?.let { list ->
                    val index = list.indexOfFirst { it.outgoing && it.messageId == result.messageId }
                    if (index >= 0) list[index] = list[index].copy(delivery = result.phase)
                }
            }
            null
        }
        is CompanionDirectResult.BlockedPeer -> null
    }

    private fun onPlain(result: CompanionDirectResult.Plain): CompanionDirectCommand? {
        val status = result.status
        when (result.operation) {
            CompanionDirectOperation.BEGIN, CompanionDirectOperation.REVIEW ->
                if (invite is V1DirectInvite.Waiting && status != CompanionDirectStatus.OK && status != CompanionDirectStatus.BUSY) {
                    invite = V1DirectInvite.Ended(outcome(status))
                }
            CompanionDirectOperation.ACCEPT -> (invite as? V1DirectInvite.Review)?.let {
                invite = if (status == CompanionDirectStatus.OK) V1DirectInvite.Finishing(it.binding) else V1DirectInvite.Ended(outcome(status))
                if (status == CompanionDirectStatus.OK) return CompanionDirectCommand.Finish(it.binding)
            }
            CompanionDirectOperation.DECLINE ->
                if (status == CompanionDirectStatus.OK && (invite is V1DirectInvite.Waiting || invite is V1DirectInvite.Review)) {
                    invite = V1DirectInvite.Ended(V1DirectOutcome.DECLINED)
                }
            CompanionDirectOperation.FINISH -> if (invite is V1DirectInvite.Finishing && status != CompanionDirectStatus.BUSY) {
                invite = V1DirectInvite.Ended(if (status == CompanionDirectStatus.OK) V1DirectOutcome.CONNECTED else outcome(status))
            }
            CompanionDirectOperation.SEND -> Unit  // delivery events carry the outcome
            else -> Unit
        }
        return null
    }

    private fun onConversation(result: CompanionDirectResult.Conversation) {
        val previous = slots[result.slot]
        val binding = result.binding
        if (binding == null) {
            slots[result.slot] = null
            previous?.let { forget(it.binding) }
            return
        }
        if (previous != null && previous.binding != binding) forget(previous.binding)
        slots[result.slot] = V1DirectConversation(result.slot, result.state, result.generation, binding)
    }

    private fun forget(binding: CompanionDirectBinding) {
        drafts.remove(binding)
        messages.remove(binding)
    }

    private fun outcome(status: CompanionDirectStatus) = when (status) {
        CompanionDirectStatus.BLOCKED -> V1DirectOutcome.BLOCKED
        CompanionDirectStatus.FULL -> V1DirectOutcome.FULL
        CompanionDirectStatus.EXPIRED -> V1DirectOutcome.EXPIRED
        CompanionDirectStatus.STALE -> V1DirectOutcome.STALE
        CompanionDirectStatus.UNAVAILABLE -> V1DirectOutcome.UNAVAILABLE
        else -> V1DirectOutcome.REFUSED
    }
}
