package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*

data class BleConfigurationState(
    val available: Boolean = false,
    val busy: Boolean = false,
    val deviceName: String? = null,
    val nameRevision: ULong? = null,
    val regionAvailable: Boolean = false,
    val regionSelectionId: Int? = null,
    val regionRevision: ULong? = null,
    val notice: String = "Device configuration is unavailable.",
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
) {
    private val names = V1NameTransaction(V1NameAuthoritySource {
        V1NameAuthority(if(isCurrent()) V1NamePhase.READY else V1NamePhase.DISCONNECTED,context)
    }, sharedExchangeAllocator=allocate)
    private data class RegionPending(val exchange: UInt, val request: CompanionRegionPayload)
    private var regionPending: RegionPending? = null
    private var namePending: V1NameRequest? = null
    private var timeExchange: UInt? = null
    private var challenge: ULong? = null
    private var closed = false
    var state = BleConfigurationState(available=true,regionAvailable=minorVersion==3,notice="Read the device name before applying a change.")
        private set
    val busy get() = namePending != null || timeExchange != null || regionPending != null

    fun readRegion(): Boolean = startRegion(CompanionRegionPayload(1))
    fun writeRegion(selectionId: Int): Boolean {
        if(RegionSelectionCatalog.find(selectionId)==null) return false
        val revision=state.regionRevision ?: return false
        if(revision==ULong.MAX_VALUE) return false
        return startRegion(CompanionRegionPayload(2,revision=revision,selectionId=selectionId))
    }
    private fun startRegion(request: CompanionRegionPayload): Boolean {
        if(closed || !isCurrent() || busy || minorVersion != 3) return false
        val exchange=allocate() ?: return false
        regionPending=RegionPending(exchange,request)
        update(state.copy(busy=true,regionRevision=null,notice="Waiting for region readback..."))
        return transmit(6,exchange,checkNotNull(CompanionConfigurationCodec.encodeRegion(request)))
    }
    private fun uncertainRegion() = update(state.copy(busy=false,regionRevision=null,
        notice="Region was not confirmed. Read the device region before applying another choice. Radio TX remains disabled."))
    fun readName(): Boolean = startName { names.beginRead() }
    fun writeName(name: V1DeviceName): Boolean {
        val revision=state.nameRevision ?: return false
        return startName { names.beginWrite(name,revision) }
    }
    private fun startName(create: () -> V1NameRequest?): Boolean {
        if(closed || !isCurrent() || busy) return false
        val request=create() ?: return false
        namePending=request
        update(state.copy(busy=true,notice="Waiting for device readback…"))
        return transmit(4,request.exchangeId,request.encodedPayload())
    }
    fun synchronizeTime(): Boolean {
        if(closed || !isCurrent() || busy) return false
        val id=allocate() ?: return false
        timeExchange=id;challenge=null
        update(state.copy(busy=true,notice="Synchronizing the display clock…"))
        return transmit(5,id,checkNotNull(CompanionConfigurationCodec.encodeTime(CompanionTimePayload(1))))
    }
    private fun transmit(kind: Int,id: UInt,payload: ByteArray): Boolean {
        if(closed || !isCurrent()) return false
        val wire=CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(kind,context.sessionNonce,id,payload,minorVersion))
            ?: return false
        if(send(wire)) return true
        if(namePending?.exchangeId==id || timeExchange==id || regionPending?.exchange==id) lost(id)
        return false
    }
    fun receive(frame: CompanionConfigurationFrame): Boolean {
        if(closed || !isCurrent() || frame.sessionNonce!=context.sessionNonce || frame.minorVersion!=minorVersion) return false
        val region=regionPending
        if(frame.kind==0x88 && region?.exchange==frame.exchangeId) {
            regionPending=null
            val result=CompanionConfigurationCodec.decodeRegion(frame.payload)
            if(result?.kind==0x81 && region.request.kind==1 &&
                (result.selectionId==0 || RegionSelectionCatalog.find(result.selectionId)!=null)) {
                update(state.copy(busy=false,regionSelectionId=result.selectionId.takeIf { it!=0 },regionRevision=result.revision,
                    notice="Region read back. Radio TX remains disabled."))
            } else if(result?.kind==0x82 && region.request.kind==2 && region.request.revision!=ULong.MAX_VALUE &&
                result.revision==region.request.revision+1u && result.selectionId==region.request.selectionId) {
                update(state.copy(busy=false,regionSelectionId=result.selectionId,regionRevision=null,
                    notice="Region saved and read back. Radio TX remains disabled."))
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
                        notice="Device name read back."))
                }
                V1NameResultCode.APPLIED -> {
                    val name=V1DeviceName.create(request.payload.name)
                    val receipt=result.receipt
                    if(name!=null && receipt!=null && names.consume(receipt,context.setupLabel,context.sessionToken,name)) {
                        update(state.copy(busy=false,deviceName=name.value,nameRevision=request.payload.revision+1u,
                            notice="Device name saved and read back."))
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
            val next=allocate() ?: run { lost(frame.exchangeId); return true }
            challenge=time.challenge;timeExchange=next
            transmit(5,next,payload)
            return true
        }
        if(time.kind!=4 || time.challenge!=(challenge ?: 0uL)) return false
        timeExchange=null;challenge=null
        update(state.copy(busy=false,notice=if(time.code==0) "Display clock synchronized." else "Clock sync was not accepted. Try again."))
        return true
    }
    fun lost(exchange: UInt) {
        if(regionPending?.exchange==exchange) { regionPending=null;uncertainRegion();return }
        if(namePending?.exchangeId==exchange) {
            names.lostResult(context,exchange);namePending=null;uncertain()
        } else if(timeExchange==exchange) {
            timeExchange=null;challenge=null
            update(state.copy(busy=false,notice="Clock sync was not confirmed. Try again."))
        }
    }
    fun pendingExchange(): UInt? = namePending?.exchangeId ?: timeExchange ?: regionPending?.exchange
    fun close() {
        closed=true;regionPending=null;namePending=null;timeExchange=null;challenge=null
        names.lifecycle(context,V1NameLifecycle.DISCONNECTED)
    }
    private fun uncertain() = update(state.copy(busy=false,nameRevision=null,
        notice="Name change was not confirmed. Read the device before trying another change."))
    private fun update(next: BleConfigurationState) { state=next;changed(next) }
}
