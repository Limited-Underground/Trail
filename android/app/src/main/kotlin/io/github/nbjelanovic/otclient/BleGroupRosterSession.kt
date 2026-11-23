package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGroupConfigurationCodec
import io.github.nbjelanovic.otprotocol.CompanionGroupOperation
import io.github.nbjelanovic.otprotocol.CompanionGroupResult
import io.github.nbjelanovic.otprotocol.CompanionGroupRosterState
import io.github.nbjelanovic.otprotocol.CompanionGroupScope
import io.github.nbjelanovic.otprotocol.CompanionGroupStatus

/** Runs on the protected configuration owner. A mutation is never repeated. */
internal class BleGroupRosterSession(
    private val isCurrent: () -> Boolean,
    private val allocate: () -> UInt?,
    private val send: (UInt, ByteArray) -> Boolean,
    private val changed: () -> Unit,
) {
    private data class Pending(val exchange: UInt, val operation: CompanionGroupOperation)
    private data class Readback(
        val scope: CompanionGroupScope,
        val state: CompanionGroupRosterState,
        val bytes: ByteArray,
        var used: Int = 0,
    )
    internal class Group(val scope: CompanionGroupScope, val summary: V1BoardGroupSummary)

    private var pending: Pending? = null
    private var intent: V1BoardGroupAction? = null
    private var expectedScope: CompanionGroupScope? = null
    private var confirmed = emptyList<Group>()
    private val discovered = mutableListOf<Group>()
    private var slot = 0
    private var readback: Readback? = null

    var state = V1BoardGroupState(supported = true)
        private set
    val busy get() = intent != null
    val pendingExchange get() = pending?.exchange

    fun locationGroup(selected: V1BoardGroupSummary): Group? =
        if (!isCurrent() || busy) null else confirmed.firstOrNull {
            it.summary === selected && !selected.distrusted && selected.locationRule in 1..2
        }

    fun submit(action: V1BoardGroupAction): Boolean {
        if (busy || !isCurrent()) return false
        val target = (action as? V1BoardGroupAction.StopTrust)?.let { stop ->
            confirmed.firstOrNull { it.summary === stop.group && !it.summary.distrusted }
                ?: return false
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
        }
        intent = action
        expectedScope = target?.scope
        confirmed = emptyList()
        state = state.copy(busy = true, canCreate = false, groups = emptyList(), notice = null)
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
            else -> {
                val scope = result.scope
                if (scope == null || expectedScope?.let { it != scope } == true) fail()
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
            else -> null
        }
        confirmed = discovered.toList()
        clearTransaction()
        state = V1BoardGroupState(
            supported = true,
            available = true,
            canCreate = confirmed.size < CompanionGroupConfigurationCodec.SLOT_COUNT,
            groups = confirmed.map { it.summary },
            notice = message,
        )
        changed()
    }

    private fun fail(status: CompanionGroupStatus? = null) {
        val notice = when {
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
