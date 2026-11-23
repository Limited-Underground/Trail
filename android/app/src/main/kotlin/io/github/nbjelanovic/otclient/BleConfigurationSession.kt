package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*

data class BleConfigurationState(
    val available: Boolean = false,
    val editorSessionId: Long = 0,
    val suggestedDeviceName: String? = null,
    val busy: Boolean = false,
    val deviceName: String? = null,
    val nameRevision: ULong? = null,
    val regionAvailable: Boolean = false,
    val regionSelectionId: Int? = null,
    val regionRevision: ULong? = null,
    val publicProfileAvailable: Boolean = false,
    val publicName: String? = null,
    val publiclyDiscoverable: Boolean? = null,
    val publicRevision: ULong? = null,
    val notice: String = "Device settings are unavailable.",
) {
    override fun toString() = "BleConfigurationState(available=$available, busy=$busy, name=redacted)"
}

/** One authenticated runtime session supplies this owner; it never establishes authority itself. */
internal class BleConfigurationSession(
    private val context: V1NameContext,
    private val isCurrent: () -> Boolean,
    private val allocate: () -> UInt?,
    private val send: (ByteArray) -> Boolean,
    private val civilSample: () -> Pair<UInt,Int>,
    private val changed: (BleConfigurationState) -> Unit,
    private val minorVersion: Int = 2,
    private val allowProfile06: Boolean = false,
    private val sendImmediateLocation: (ByteArray) -> Boolean = send,
) {
    private val names = V1NameTransaction(V1NameAuthoritySource {
        V1NameAuthority(if(isCurrent()) V1NamePhase.READY else V1NamePhase.DISCONNECTED,context)
    }, sharedExchangeAllocator=allocate)
    private data class RegionPending(val exchange: UInt, val request: CompanionRegionPayload)
    private var regionPending: RegionPending? = null
    private data class PublicPending(val exchange: UInt, val request: CompanionPublicProfilePayload)
    private var publicPending: PublicPending? = null
    private class LocationTarget(val group: V1BoardGroupSummary, val binding: LocationBinding)
    private class LocationPending(
        val exchange: UInt,
        val target: LocationTarget,
        val operation: CompanionLocationOperation,
        val lease: ULong,
        val result: (CompanionLocationReply?) -> Unit,
    )
    private class LocationCleanup(val target: LocationTarget, val operation: CompanionLocationOperation, val lease: Long)
    private var locationPending: LocationPending? = null
    private val locationCleanup = linkedMapOf<LocationBinding, LocationCleanup>()
    private var namePending: V1NameRequest? = null
    private var timeExchange: UInt? = null
    private var challenge: ULong? = null
    private var closed = false
    var state = BleConfigurationState(available=true,editorSessionId=context.device,regionAvailable=(minorVersion in listOf(3,CompanionPublicProfileCodec.PROFILE,CompanionConfirmationCodec.PROFILE) || allowProfile06 && minorVersion==6),publicProfileAvailable=(minorVersion==CompanionPublicProfileCodec.PROFILE || allowProfile06 && minorVersion==6),notice="Check the saved name before changing it.")
        private set
    private val groups: BleGroupRosterSession? = if (minorVersion == CompanionPublicProfileCodec.PROFILE) BleGroupRosterSession(
        isCurrent = isCurrent,
        allocate = allocate,
        send = { exchange, payload -> transmit(CompanionGroupConfigurationCodec.REQUEST_KIND, exchange, payload) },
        changed = { update(state.copy(busy = busy)) },
    ) else null

    val busy: Boolean get() = namePending != null || timeExchange != null || regionPending != null ||
        publicPending != null || groups?.busy == true || locationPending != null

    fun boardGroupState() = groups?.state ?: V1BoardGroupState()

    fun submitBoardGroupAction(action: V1BoardGroupAction): Boolean =
        !closed && !busy && groups?.submit(action) == true

    fun groupLocationBinding(group: V1BoardGroupSummary): LocationBinding? {
        if (closed || !isCurrent()) return null
        val current = groups?.locationGroup(group) ?: return null
        return LocationBinding(
            LocationAudience(LocationAudienceKind.GROUP, current.scope.groupId.toLong(),
                group.version.toLong(), rosterAuthority = current),
            context.device,
        )
    }

    fun submitGroupLocation(
        group: V1BoardGroupSummary,
        binding: LocationBinding,
        operation: CompanionLocationOperation,
        lease: Long,
        fix: PhoneLocationFix?,
        result: (CompanionLocationReply?) -> Unit,
    ): Boolean {
        if (locationCleanup.isNotEmpty()) return false
        return sendLocation(LocationTarget(group, binding), operation, lease, fix, result)
    }

    fun queueGroupLocationCleanup(
        group: V1BoardGroupSummary,
        binding: LocationBinding,
        operation: CompanionLocationOperation,
        lease: Long = 0L,
    ) {
        if ((operation != CompanionLocationOperation.STOP && operation != CompanionLocationOperation.WITHDRAW) ||
            groupLocationBinding(group) != binding) return
        // Only the current board's four verified slots can contribute cleanup work.
        locationCleanup.entries.removeAll { groupLocationBinding(it.value.target.group) != it.key }
        val previous = locationCleanup[binding]
        if (previous?.operation == CompanionLocationOperation.WITHDRAW) return
        if (locationCleanup.size >= CompanionGroupConfigurationCodec.SLOT_COUNT && previous == null) return
        locationCleanup[binding] = LocationCleanup(LocationTarget(group, binding), operation, lease)
    }

    val hasLocationCleanup: Boolean get() = locationCleanup.isNotEmpty()

    fun flushLocationCleanup(): Boolean {
        while (!busy && locationCleanup.isNotEmpty()) {
            val next = locationCleanup.entries.first().value
            locationCleanup.remove(next.target.binding)
            if (sendLocation(next.target, next.operation, next.lease, null) {}) return true
        }
        return false
    }

    private fun sendLocation(
        target: LocationTarget,
        operation: CompanionLocationOperation,
        lease: Long,
        fix: PhoneLocationFix?,
        result: (CompanionLocationReply?) -> Unit,
    ): Boolean {
        if (busy || groupLocationBinding(target.group) != target.binding) return false
        if (fix != null && (fix.capturedAtMs <= 0L || fix.ageMs !in 0L until PHONE_FIX_FRESH_MS)) return false
        val scope = groups?.locationGroup(target.group)?.scope ?: return false
        val request = CompanionLocationRequest(
            operation = operation, kind = CompanionLocationCodec.GROUP_KIND, slot = target.group.slot,
            scopeId = scope.groupId, generation = target.group.version.toULong(), lease = lease.toULong(),
            sample = fix?.capturedAtMs?.toULong() ?: 0uL, ageMs = fix?.ageMs?.toInt() ?: 0,
            latitudeE4 = fix?.latitudeE4 ?: 0, longitudeE4 = fix?.longitudeE4 ?: 0,
            adminFingerprint = scope.adminFingerprint, rosterDigest = scope.rosterDigest,
        )
        val payload = CompanionLocationCodec.encode(request) ?: return false
        val exchange = allocate() ?: return false
        if (groupLocationBinding(target.group) != target.binding) return false
        val pending = LocationPending(exchange, target, operation, lease.toULong(), result)
        locationPending = pending
        // Keep the coordinate buffer inside this dispatch. No observer runs between admission and the write.
        val write = if (fix == null) send else sendImmediateLocation
        val sent = runCatching { transmit(CompanionLocationCodec.REQUEST_KIND, exchange, payload, write) }
            .getOrDefault(false)
        if (locationPending === pending) {
            if (sent) update(state.copy(busy = true)) else finishLocation(pending, null)
        }
        return true
    }

    private fun finishLocation(pending: LocationPending, reply: CompanionLocationReply?) {
        locationPending = null
        update(state.copy(busy = busy))
        pending.result(reply.takeIf { groupLocationBinding(pending.target.group) == pending.target.binding })
    }

    fun readRegion(): Boolean = startRegion(CompanionRegionPayload(1))
    fun writeRegion(selectionId: Int): Boolean {
        if(RegionSelectionCatalog.find(selectionId)==null) return false
        val revision=state.regionRevision ?: return false
        if(revision==ULong.MAX_VALUE) return false
        return startRegion(CompanionRegionPayload(2,revision=revision,selectionId=selectionId))
    }
    private fun startRegion(request: CompanionRegionPayload): Boolean {
        if(closed || !isCurrent() || busy || !(minorVersion in listOf(3,CompanionPublicProfileCodec.PROFILE,CompanionConfirmationCodec.PROFILE) || allowProfile06 && minorVersion==6)) return false
        val exchange=allocate() ?: return false
        if(!isCurrent()) return false
        regionPending=RegionPending(exchange,request)
        update(clearPublic(state).copy(busy=true,regionRevision=null,notice="Waiting for your device to confirm its region."))
        return transmit(6,exchange,checkNotNull(CompanionConfigurationCodec.encodeRegion(request)))
    }
    private fun uncertainRegion() = update(state.copy(busy=false,regionRevision=null,
        notice="Couldn't confirm the radio region.\nCheck the saved region before trying again."))
    private fun clearPublic(value: BleConfigurationState) = value.copy(publicName=null, publiclyDiscoverable=null, publicRevision=null)
    private fun publicPrerequisites(): Boolean = state.available && state.nameRevision?.let { it>0uL }==true &&
        state.deviceName?.let(V1DeviceName::create)!=null && state.regionAvailable &&
        state.regionRevision?.let { it>0uL }==true && state.regionSelectionId?.let(RegionSelectionCatalog::find)!=null
    fun readPublicProfile(): Boolean = startPublic(CompanionPublicProfilePayload(1))
    fun writePublicProfile(name: String, visible: Boolean): Boolean {
        val revision=state.publicRevision ?: return false
        if(V1PublicName.create(name)==null || !CompanionPublicProfileCodec.validName(name) || revision==ULong.MAX_VALUE) return false
        return startPublic(CompanionPublicProfilePayload(2,revision=revision,name=name,visible=visible))
    }
    private fun startPublic(request: CompanionPublicProfilePayload): Boolean {
        if(closed || !isCurrent() || busy || !state.publicProfileAvailable || !publicPrerequisites()) return false
        val payload=CompanionPublicProfileCodec.encode(request) ?: return false
        val exchange=allocate() ?: return false
        if(!isCurrent()) return false
        publicPending=PublicPending(exchange,request)
        update(clearPublic(state).copy(busy=true,notice="Waiting for your device to confirm your profile."))
        return transmit(CompanionPublicProfileCodec.REQUEST,exchange,payload)
    }
    private fun uncertainPublic() = update(clearPublic(state).copy(busy=false,
        notice="Couldn't confirm your profile.\nCheck the saved profile before trying again."))
    fun readName(): Boolean = startName { names.beginRead() }
    fun writeName(name: V1DeviceName): Boolean {
        val revision=state.nameRevision ?: return false
        return startName { names.beginWrite(name,revision) }
    }
    private fun startName(create: () -> V1NameRequest?): Boolean {
        if(closed || !isCurrent() || minorVersion==6 && !allowProfile06 || busy) return false
        val request=create() ?: return false
        if(!isCurrent()) return false
        namePending=request
        update(clearPublic(state).copy(busy=true,nameRevision=null,notice="Waiting for your device to confirm its name."))
        return transmit(4,request.exchangeId,request.encodedPayload())
    }
    fun synchronizeTime(): Boolean {
        if(closed || !isCurrent() || minorVersion==6 && !allowProfile06 || busy) return false
        val id=allocate() ?: return false
        if(!isCurrent()) return false
        timeExchange=id;challenge=null
        update(state.copy(busy=true,notice="Setting your device clock."))
        return transmit(5,id,checkNotNull(CompanionConfigurationCodec.encodeTime(CompanionTimePayload(1))))
    }
    private fun transmit(kind: Int,id: UInt,payload: ByteArray, write: (ByteArray) -> Boolean = send): Boolean {
        if(closed || !isCurrent()) return false
        val wire=CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(kind,context.sessionNonce,id,payload,minorVersion),allowProfile06=allowProfile06)
            ?: return false
        if(write(wire)) return true
        if(namePending?.exchangeId==id || timeExchange==id || regionPending?.exchange==id || publicPending?.exchange==id) lost(id)
        return false
    }
    fun receive(frame: CompanionConfigurationFrame): Boolean {
        if(closed || !isCurrent() || minorVersion==6 && !allowProfile06 || frame.sessionNonce!=context.sessionNonce || frame.minorVersion!=minorVersion) return false
        if (frame.kind == CompanionLocationCodec.RESPONSE_KIND) {
            val pending = locationPending ?: return false
            if (pending.exchange != frame.exchangeId) return false
            val target = pending.target
            val reply = CompanionLocationCodec.decodeReply(frame.payload, pending.operation,
                target.binding.audience.scopeId.toULong(), target.binding.audience.generation.toULong(), pending.lease)
            if (reply == null) return false
            finishLocation(pending, reply)
            return true
        }
        if (frame.kind == CompanionGroupConfigurationCodec.RESPONSE_KIND) {
            return groups?.receive(frame.exchangeId, frame.payload) == true
        }
        val profile=publicPending
        if(frame.kind==CompanionPublicProfileCodec.RESPONSE && profile?.exchange==frame.exchangeId) {
            publicPending=null
            val result=CompanionPublicProfileCodec.decode(frame.payload)
            val request=profile.request
            val absent=result?.kind==0x81 && request.kind==1 && result.revision==0uL && result.name.isEmpty() && result.visible
            val named=result!=null && V1PublicName.create(result.name)!=null
            val snapshot=result?.kind==0x81 && request.kind==1 && named && result.revision>0uL
            val applied=result?.kind==0x82 && request.kind==2 && named && request.revision!=ULong.MAX_VALUE &&
                result.revision==request.revision+1u && result.name==request.name && result.visible==request.visible
            if(publicPrerequisites() && (absent || snapshot || applied)) {
                update(state.copy(busy=false,publicName=result!!.name.ifEmpty { null },
                    publiclyDiscoverable=result.visible,publicRevision=result.revision,
                    notice=if(applied) "Your device confirmed your profile was saved.\nSaving it doesn't make you visible yet." else "Your device confirmed your saved profile."))
            } else uncertainPublic()
            return true
        }
        val region=regionPending
        if(frame.kind==0x88 && region?.exchange==frame.exchangeId) {
            regionPending=null
            val result=CompanionConfigurationCodec.decodeRegion(frame.payload)
            if(result?.kind==0x81 && region.request.kind==1 &&
                (result.selectionId==0 || RegionSelectionCatalog.find(result.selectionId)!=null)) {
                update(state.copy(busy=false,regionSelectionId=result.selectionId.takeIf { it!=0 },regionRevision=result.revision,
                    notice=if(result.selectionId==0) "No radio region is saved." else "Your device confirmed its saved region."))
            } else if(result?.kind==0x82 && region.request.kind==2 && region.request.revision!=ULong.MAX_VALUE &&
                result.revision==region.request.revision+1u && result.selectionId==region.request.selectionId) {
                update(state.copy(busy=false,regionSelectionId=result.selectionId,regionRevision=null,
                    notice="Your device saved the region and will follow its radio rules."))
            } else uncertainRegion()
            return true
        }
        val request=namePending
        if(frame.kind==0x86 && request?.exchangeId==frame.exchangeId) {
            val result=names.receive(context,request.exchangeId,frame.payload)
            namePending=null
            when(result.code) {
                V1NameResultCode.SNAPSHOT -> {
                    val snapshot=checkNotNull(result.snapshot)
                    update(state.copy(busy=false,deviceName=snapshot.name.ifEmpty { null },nameRevision=snapshot.revision,
                        suggestedDeviceName=if(snapshot.name.isEmpty()) context.setupLabel?.value else null,
                        notice="Your device confirmed its saved name."))
                }
                V1NameResultCode.APPLIED -> {
                    val name=V1DeviceName.create(request.payload.name)
                    val receipt=result.receipt
                    if(name!=null && receipt!=null && names.consume(receipt,context.setupLabel,context.sessionToken,name)) {
                        update(state.copy(busy=false,deviceName=name.value,suggestedDeviceName=null,nameRevision=request.payload.revision+1u,
                            notice="Your device confirmed the new name was saved."))
                    } else uncertain()
                }
                else -> uncertain()
            }
            return true
        }
        if(frame.kind!=0x87 || frame.exchangeId!=timeExchange) return false
        val time=CompanionConfigurationCodec.decodeTime(frame.payload) ?: return false
        if(challenge==null && time.kind==2) {
            val sample=civilSample()
            val payload=CompanionConfigurationCodec.encodeTime(CompanionTimePayload(3,format=sample.second,
                challenge=time.challenge,localSecond=sample.first)) ?: run { lost(frame.exchangeId); return true }
            if(!isCurrent()) return false
            val next=allocate() ?: run { lost(frame.exchangeId); return true }
            if(!isCurrent()) return false
            challenge=time.challenge;timeExchange=next
            transmit(5,next,payload)
            return true
        }
        if(time.kind!=4 || time.challenge!=(challenge ?: 0uL)) return false
        timeExchange=null;challenge=null
        update(state.copy(busy=false,notice=if(time.code==0) "Your device clock is set." else "Your device refused the time change.\nTry again."))
        return true
    }
    fun lost(exchange: UInt) {
        if(closed || !isCurrent()) return
        locationPending?.takeIf { it.exchange == exchange }?.let {
            finishLocation(it, null)
            return
        }
        if (groups?.pendingExchange == exchange) {
            groups.lost(exchange)
            return
        }
        if(publicPending?.exchange==exchange) { publicPending=null;uncertainPublic();return }
        if(regionPending?.exchange==exchange) { regionPending=null;uncertainRegion();return }
        if(namePending?.exchangeId==exchange) {
            names.lostResult(context,exchange);namePending=null;uncertain()
        } else if(timeExchange==exchange) {
            timeExchange=null;challenge=null
            update(state.copy(busy=false,notice="Couldn't confirm the time change.\nTry again."))
        }
    }
    fun pendingExchange(): UInt? = namePending?.exchangeId ?: timeExchange ?: regionPending?.exchange ?:
        publicPending?.exchange ?: groups?.pendingExchange ?: locationPending?.exchange
    fun close() {
        closed = true
        groups?.close()
        val pending = locationPending
        locationPending = null
        locationCleanup.clear()
        state=clearPublic(state).copy(available=false,busy=false,publicProfileAvailable=false,suggestedDeviceName=null)
        regionPending=null;publicPending=null;namePending=null;timeExchange=null;challenge=null
        pending?.result?.invoke(null)
        names.lifecycle(context,V1NameLifecycle.DISCONNECTED)
    }
    private fun uncertain() = update(state.copy(busy=false,nameRevision=null,suggestedDeviceName=null,
        notice="Couldn't confirm the new name.\nCheck the saved name before trying again."))
    private fun update(next: BleConfigurationState) { state=next;changed(next) }
}
