package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGroupConfigurationCodec
import io.github.nbjelanovic.otprotocol.CompanionGroupInvite
import io.github.nbjelanovic.otprotocol.CompanionGroupOperation
import io.github.nbjelanovic.otprotocol.CompanionGroupResult
import io.github.nbjelanovic.otprotocol.CompanionGroupRosterState
import io.github.nbjelanovic.otprotocol.CompanionGroupScope
import io.github.nbjelanovic.otprotocol.CompanionGroupStatus

private const val MAX_MEMBERS = 6

/** Runs on the protected configuration owner. A mutation is never repeated. */
internal class BleGroupRosterSession(
    private val isCurrent: () -> Boolean,
    private val allocate: () -> UInt?,
    private val send: (UInt, ByteArray) -> Boolean,
    private val changed: () -> Unit,
    private val now: () -> Long = System::currentTimeMillis,
) {
    private data class Pending(val exchange: UInt, val operation: CompanionGroupOperation)
    private data class Readback(
        val scope: CompanionGroupScope,
        val state: CompanionGroupRosterState,
        val bytes: ByteArray,
        var used: Int = 0,
    )
    private data class Group(val scope: CompanionGroupScope, val summary: V1BoardGroupSummary)

    private var pending: Pending? = null
    private var intent: V1BoardGroupAction? = null
    private var expectedScope: CompanionGroupScope? = null
    private var confirmed = emptyList<Group>()
    private val discovered = mutableListOf<Group>()
    private var slot = 0
    private var readback: Readback? = null
    // F8 Invite readback and Join upload, one at a time.
    private class InviteBytes(val id: ByteArray?, val bytes: ByteArray, var used: Int = 0)
    private var inviteRead: InviteBytes? = null
    private var joinUpload: InviteBytes? = null
    private var joinReview: CompanionGroupInvite? = null
    private var issued: V1BoardGroupInvite? = null

    var state = V1BoardGroupState(supported = true)
        private set
    val busy get() = intent != null
    val pendingExchange get() = pending?.exchange

    fun submit(action: V1BoardGroupAction): Boolean {
        if (busy || !isCurrent()) return false
        val selected = when (action) {
            is V1BoardGroupAction.StopTrust -> action.group
            is V1BoardGroupAction.Invite -> action.group
            else -> null
        }
        val target = selected?.let { group ->
            confirmed.firstOrNull { it.summary === group && !it.summary.distrusted } ?: return false
        }
        if (action is V1BoardGroupAction.Invite &&
            checkNotNull(target).summary.members.size >= MAX_MEMBERS) return false
        val review = (action as? V1BoardGroupAction.Join)?.let { join ->
            if (!state.available || !state.canCreate) return false
            CompanionGroupConfigurationCodec.decodeInvite(join.invite) ?: return false
        }
        val (operation, payload) = when (action) {
            V1BoardGroupAction.Refresh ->
                CompanionGroupOperation.DISCOVER to CompanionGroupConfigurationCodec.discover()
            is V1BoardGroupAction.Create -> {
                if (!state.canCreate) return false
                CompanionGroupOperation.CREATE to (CompanionGroupConfigurationCodec.create(action.locationRule) ?: return false)
            }
            is V1BoardGroupAction.StopTrust ->
                CompanionGroupOperation.STOP_TRUST to (CompanionGroupConfigurationCodec.stopTrust(checkNotNull(target).scope) ?: return false)
            is V1BoardGroupAction.Invite ->
                CompanionGroupOperation.INVITE to
                    (CompanionGroupConfigurationCodec.invite(checkNotNull(target).scope, action.label) ?: return false)
            is V1BoardGroupAction.Join ->
                CompanionGroupOperation.JOIN to (CompanionGroupConfigurationCodec.joinPage(action.invite, 0) ?: return false)
        }
        if (action is V1BoardGroupAction.Join) {
            joinUpload = InviteBytes(null, action.invite)
            joinReview = review
        }
        intent = action
        expectedScope = target?.scope
        confirmed = emptyList()
        state = state.copy(busy = true, canCreate = false, groups = emptyList(), notice = null, invite = null)
        return request(operation, payload, announce = true)
    }

    fun receive(exchange: UInt, payload: ByteArray): Boolean {
        val request = pending ?: return false
        if (request.exchange != exchange || !isCurrent()) return false
        pending = null
        val result = CompanionGroupConfigurationCodec.decodeResult(payload)
        when {
            result == null || result.operation != request.operation -> fail()
            result.operation == CompanionGroupOperation.READ_ROSTER -> read(result)
            result.status != CompanionGroupStatus.OK -> fail(result.status)
            result.operation == CompanionGroupOperation.DISCOVER -> readSlot()
            result.operation == CompanionGroupOperation.READ_INVITE -> readInvitePage(result)
            result.operation == CompanionGroupOperation.JOIN -> joined(result)
            else -> {
                val scope = result.scope
                if (scope == null || expectedScope?.let { it != scope } == true) fail()
                else if (result.operation == CompanionGroupOperation.INVITE) startInviteRead(result)
                else {
                    expectedScope = scope
                    readSlot()
                }
            }
        }
        return true
    }

    fun lost(exchange: UInt) {
        if (pending?.exchange == exchange && isCurrent()) fail()
    }

    fun close() {
        clearTransaction()
        confirmed = emptyList()
        state = V1BoardGroupState()
    }

    private fun startInviteRead(result: CompanionGroupResult) {
        val page = result.invite ?: return fail()
        val id = page.inviteId ?: return fail()
        inviteRead = InviteBytes(id, ByteArray(page.totalBytes))
        readInviteNext()
    }

    private fun readInviteNext() {
        val read = inviteRead ?: return fail()
        val payload = CompanionGroupConfigurationCodec.readInvite(checkNotNull(read.id), read.used) ?: return fail()
        request(CompanionGroupOperation.READ_INVITE, payload)
    }

    private fun readInvitePage(result: CompanionGroupResult) {
        val read = inviteRead
        val page = result.invite
        if (read == null || page == null || page.inviteId?.contentEquals(read.id) != true ||
            page.offset != read.used || page.totalBytes != read.bytes.size ||
            page.bytes.isEmpty() || page.bytes.size > read.bytes.size - read.used
        ) return fail()
        page.bytes.copyInto(read.bytes, read.used)
        read.used += page.bytes.size
        if (read.used < read.bytes.size) return readInviteNext()
        val scope = expectedScope ?: return fail()
        val invite = CompanionGroupConfigurationCodec.decodeInvite(read.bytes)
        if (invite == null || invite.groupId != scope.groupId || !invite.adminKey.contentEquals(scope.adminFingerprint) ||
            !invite.inviteId.contentEquals(read.id)
        ) return fail()
        issued = V1BoardGroupInvite(invite.groupId, invite.label, V1InviteCodeText.encode(read.bytes), now())
        inviteRead = null
        readSlot()
    }

    private fun joined(result: CompanionGroupResult) {
        val upload = joinUpload
        val page = result.invite
        if (upload == null || page == null || page.offset != upload.used || page.totalBytes != upload.bytes.size) return fail()
        upload.used += minOf(CompanionGroupConfigurationCodec.INVITE_PAGE_BYTES, upload.bytes.size - upload.used)
        if (upload.used < upload.bytes.size) {
            if (result.scope != null) return fail()
            val payload = CompanionGroupConfigurationCodec.joinPage(upload.bytes, upload.used) ?: return fail()
            request(CompanionGroupOperation.JOIN, payload)
            return
        }
        val review = joinReview ?: return fail()
        val scope = result.scope
        if (scope == null || scope.groupId != review.groupId || !scope.adminFingerprint.contentEquals(review.adminKey) ||
            page.inviteId?.contentEquals(review.inviteId) != true
        ) return fail()
        joinUpload = null
        readSlot()
    }

    private fun readSlot() {
        val current = readback
        val payload = CompanionGroupConfigurationCodec.readRoster(slot, current?.used ?: 0, current?.scope, current?.state)
        if (payload == null) fail()
        else request(CompanionGroupOperation.READ_ROSTER, payload)
    }

    private fun read(result: CompanionGroupResult) {
        if (result.status == CompanionGroupStatus.MISSING && readback == null) {
            nextSlot()
            return
        }
        val scope = result.scope
        val chunk = result.chunk
        if (result.status != CompanionGroupStatus.OK || scope == null || chunk == null || chunk.slot != slot) {
            fail(result.status)
            return
        }
        val assembled = readback ?: Readback(scope, chunk.state, ByteArray(chunk.totalBytes)).also { readback = it }
        if (assembled.scope != scope || assembled.state != chunk.state ||
            assembled.bytes.size != chunk.totalBytes || assembled.used != chunk.offset ||
            chunk.bytes.isEmpty() || chunk.bytes.size > assembled.bytes.size - assembled.used
        ) {
            fail()
            return
        }
        chunk.bytes.copyInto(assembled.bytes, assembled.used)
        assembled.used += chunk.bytes.size
        if (assembled.used < assembled.bytes.size) {
            readSlot()
            return
        }
        val roster = CompanionGroupConfigurationCodec.decodeRoster(assembled.bytes, scope)
        if (roster == null || discovered.any {
                it.scope.groupId == scope.groupId && it.scope.adminFingerprint.contentEquals(scope.adminFingerprint)
            }) {
            fail()
            return
        }
        val summary = V1BoardGroupSummary(
            slot = slot,
            groupId = scope.groupId,
            version = roster.version,
            locationRule = roster.locationRule,
            members = roster.members.map { V1BoardGroupMember(it.alias, it.administrator) },
            distrusted = chunk.state == CompanionGroupRosterState.DISTRUSTED,
        )
        discovered += Group(scope, summary)
        nextSlot()
    }

    private fun nextSlot() {
        slot++
        readback = null
        if (slot < CompanionGroupConfigurationCodec.SLOT_COUNT) readSlot() else finish()
    }

    private fun finish() {
        val expected = expectedScope
        val match = discovered.firstOrNull { it.scope == expected }?.summary
        val message = when (val action = intent) {
            is V1BoardGroupAction.Create -> {
                if (match == null || match.distrusted || match.locationRule != action.locationRule) return fail()
                "Group created. Your board confirmed the signed roster."
            }
            is V1BoardGroupAction.StopTrust -> {
                if (match?.distrusted != true) return fail()
                "Your board confirmed that it stopped trusting this group."
            }
            is V1BoardGroupAction.Invite -> {
                if (match == null || match.distrusted || issued == null) return fail()
                "Invitation ready. It works once, for 3 minutes."
            }
            is V1BoardGroupAction.Join -> {
                val accepted = joinReview ?: return fail()
                "Your device accepted the invitation to ${accepted.label}. Joining finishes when " +
                    "your device reaches the group's administrator device."
            }
            else -> null
        }
        confirmed = discovered.toList()
        val invite = issued.takeIf { intent is V1BoardGroupAction.Invite }
        clearTransaction()
        state = V1BoardGroupState(
            supported = true,
            available = true,
            canCreate = confirmed.size < CompanionGroupConfigurationCodec.SLOT_COUNT,
            groups = confirmed.map { it.summary },
            notice = message,
            invite = invite,
        )
        changed()
    }

    private fun fail(status: CompanionGroupStatus? = null) {
        val notice = when {
            intent is V1BoardGroupAction.Invite && status == CompanionGroupStatus.REFUSED ->
                "Your device can't invite to this group now. Only the administrator's device invites, " +
                    "and a group holds six people."
            intent is V1BoardGroupAction.Invite && status == CompanionGroupStatus.STALE ->
                "The group changed. Refresh groups and try again."
            intent is V1BoardGroupAction.Invite -> "Couldn't create the invitation. Refresh groups before trying again."
            intent is V1BoardGroupAction.Join && status == CompanionGroupStatus.STALE ->
                "This invitation was already used, has expired, or is for a group this device already has. " +
                    "Ask for a new one."
            intent is V1BoardGroupAction.Join && status == CompanionGroupStatus.REFUSED ->
                "Your device refused this invitation. It may be damaged or your own, or this device " +
                    "already has four groups."
            intent is V1BoardGroupAction.Join -> "Couldn't confirm the invitation. Refresh groups before trying again."
            intent is V1BoardGroupAction.Create || intent is V1BoardGroupAction.StopTrust ->
                "Couldn't confirm the group change. Refresh groups before trying again."
            status == CompanionGroupStatus.UNAVAILABLE ->
                "Your board can't manage groups right now."
            else -> "Couldn't read groups from your board. Try refreshing."
        }
        clearTransaction()
        confirmed = emptyList()
        state = V1BoardGroupState(supported = true, notice = notice)
        changed()
    }

    private fun clearTransaction() {
        pending = null
        intent = null
        expectedScope = null
        discovered.clear()
        slot = 0
        readback = null
        inviteRead = null
        joinUpload = null
        joinReview = null
        issued = null
    }

    private fun request(operation: CompanionGroupOperation, payload: ByteArray, announce: Boolean = false): Boolean {
        val exchange = allocate()
        if (exchange == null) {
            fail()
            return false
        }
        val request = Pending(exchange, operation)
        pending = request
        if (announce) changed()
        if (pending !== request || !isCurrent()) return false
        val written = try { send(exchange, payload) } catch (_: Exception) { false }
        if (!written && pending === request && isCurrent()) fail()
        return written
    }
}
