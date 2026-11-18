package io.github.nbjelanovic.otprotocol

/** Provisional host-only OTE0. Values describe device observations, never authority. */
data class CompanionRadioEvent(
    val kind: Int,
    val detail: Int,
    val receipt: ULong,
    val generation: ULong,
    val state: Int,
    val reason: Int,
    val originNonce: UInt = 0u,
    val originExchange: UInt = 0u,
) {
    override fun toString() = "CompanionRadioEvent(kind=$kind, state=$state, reason=$reason, fields=redacted)"
}

data class CompanionRadioEventRecord(val sessionNonce: UInt, val eventId: UInt, val event: CompanionRadioEvent) {
    override fun toString() = "CompanionRadioEventRecord(identity=redacted, event=$event)"
}

object CompanionRadioEventCodec {
    const val PAYLOAD_BYTES = 40
    const val RECORD_BYTES = 60
    const val PROFILE = 6
    private val magic = byteArrayOf(0x4f,0x54,0x45,0x30)

    fun valid(value: CompanionRadioEvent): Boolean {
        if(value.generation == 0uL || value.kind !in 1..4) return false
        if(value.kind <= 2) {
            if(value.receipt == 0uL || value.detail !in 1..4) return false
        } else if(value.receipt != 0uL || value.detail != 0) return false
        if(value.kind == 1) {
            if(value.originNonce == 0u || value.originExchange == 0u || value.state !in listOf(1,2,3,5,6)) return false
        } else if(value.originNonce != 0u || value.originExchange != 0u) return false
        if(value.kind == 2 && value.state != 4 || value.kind == 3 && value.state !in listOf(7,8) || value.kind == 4 && value.state != 9) return false
        return when(value.state) {
            1,2,3,4 -> value.reason == 0
            5 -> value.reason in 1..9
            6 -> value.reason in listOf(1,3,5,6,7,8)
            7 -> value.reason in listOf(1,2,7,8)
            8 -> value.reason == 3
            9 -> value.reason == 7
            else -> false
        }
    }

    fun encodeEvent(value: CompanionRadioEvent): ByteArray? {
        if(!valid(value)) return null
        return ByteArray(PAYLOAD_BYTES).also {
            magic.copyInto(it);it[6]=value.kind.toByte();it[7]=value.detail.toByte()
            put(it,8,value.receipt,8);put(it,16,value.generation,8)
            it[24]=value.state.toByte();it[25]=value.reason.toByte()
            put(it,32,value.originNonce.toULong(),4);put(it,36,value.originExchange.toULong(),4)
        }
    }

    fun decodeEvent(bytes: ByteArray): CompanionRadioEvent? {
        if(bytes.size != PAYLOAD_BYTES || !magic.indices.all { bytes[it] == magic[it] } ||
            bytes[4] != 0.toByte() || bytes[5] != 0.toByte() || (26..31).any { bytes[it] != 0.toByte() }) return null
        return CompanionRadioEvent(u8(bytes,6),u8(bytes,7),get(bytes,8,8),get(bytes,16,8),
            u8(bytes,24),u8(bytes,25),get(bytes,32,4).toUInt(),get(bytes,36,4).toUInt()).takeIf(::valid)
    }

    /** OTE0 interpretation requires explicit host opt-in even when bytes name profile6. */
    fun decodeRecord(bytes: ByteArray, allowProfile06: Boolean = false): CompanionRadioEventRecord? {
        if(!allowProfile06 || bytes.size != RECORD_BYTES) return null
        val frame = CompanionConfigurationCodec.decodeFrame(bytes,PROFILE,allowProfile06=true) ?: return null
        if(frame.kind != 0x83) return null
        val event = decodeEvent(frame.payload) ?: return null
        return CompanionRadioEventRecord(frame.sessionNonce,frame.exchangeId,event)
    }

    fun encodeRecord(value: CompanionRadioEventRecord, allowProfile06: Boolean = false): ByteArray? {
        if(!allowProfile06 || value.sessionNonce == 0u || value.eventId == 0u) return null
        val payload=encodeEvent(value.event) ?: return null
        return CompanionConfigurationCodec.encodeFrame(
            CompanionConfigurationFrame(0x83,value.sessionNonce,value.eventId,payload,PROFILE),allowProfile06=true)
    }

    private fun u8(bytes: ByteArray,index: Int) = bytes[index].toInt() and 255
    private fun get(bytes: ByteArray,offset: Int,count: Int): ULong {
        var value=0uL
        repeat(count) { value=value or (u8(bytes,offset+it).toULong() shl (8*it)) }
        return value
    }
    private fun put(bytes: ByteArray,offset: Int,value: ULong,count: Int) {
        repeat(count) { bytes[offset+it]=(value shr (8*it)).toByte() }
    }
}

/**
 * Bounded receiver model only. A future protected runtime must inject its current
 * BLE fresh nonce and expected generation. The caller must supply a fresh nonce
 * for every attachment; this model rejects reuse of its immediately prior nonce.
 * New receipt IDs increase within the injected generation, including after eviction
 * and reconnect; this volatile replay fence never reconstructs historical outcomes. This class does not bootstrap generation,
 * establish Ready, authorize radio, retry commands or reconstruct durable history.
 * One active receipt plus sixteen terminal receipts; pending origin is separate.
 */
class CompanionRadioEventReceiver {
    private data class Origin(val nonce: UInt,val exchange: UInt,val detail: Int) {
        override fun toString() = "Origin(fields=redacted)"
    }
    private var nonce=0u
    private var generation=0uL
    private var lastEvent=0u
    private var lastRequest=0u
    private var previousNonce=0u
    private var receiptGeneration=0uL
    private var receiptFloor=0uL
    private var pending: Origin?=null
    private var active: CompanionRadioEvent?=null
    private val terminal=arrayOfNulls<CompanionRadioEvent>(16)
    private var head=0
    private var count=0
    private var unknown=false
    private var transport: CompanionRadioEvent?=null

    val connected: Boolean get() = nonce != 0u
    val lastEventId: UInt get() = lastEvent
    val terminalCount: Int get() = count
    val deliveryUnknown: Boolean get() = unknown
    val activeEvent: CompanionRadioEvent? get() = active
    val transportEvent: CompanionRadioEvent? get() = transport
    val pendingRequest: Boolean get() = pending != null
    fun terminalEvents(): List<CompanionRadioEvent> = (0 until count).map { terminal[(head+it)%16]!! }

    fun open(sessionNonce: UInt,expectedGeneration: ULong): Boolean {
        if(connected || sessionNonce == 0u || sessionNonce == previousNonce || expectedGeneration == 0uL) return false
        if(receiptGeneration != expectedGeneration) { receiptGeneration=expectedGeneration;receiptFloor=0uL }
        previousNonce=sessionNonce
        nonce=sessionNonce;generation=expectedGeneration;lastEvent=0u;lastRequest=0u
        return true
    }

    /** Register copied local intent, not device admission or a command resend. */
    fun registerRequest(exchangeId: UInt,detail: Int): Boolean {
        if(!connected || lastEvent == UInt.MAX_VALUE || pending != null || active != null || exchangeId == 0u || exchangeId <= lastRequest || detail !in 1..4) return false
        pending=Origin(nonce,exchangeId,detail);lastRequest=exchangeId
        return true
    }

    fun accept(bytes: ByteArray): Boolean {
        if(!connected) return false
        val record=CompanionRadioEventCodec.decodeRecord(bytes,allowProfile06=true) ?: return false
        if(record.sessionNonce != nonce || record.eventId <= lastEvent || record.event.generation != generation) return false
        val event=record.event
        when(event.kind) {
            1 -> {
                val current=active
                if(current == null) {
                    val origin=pending ?: return false
                    if(event.state != 1 || event.originNonce != origin.nonce || event.originExchange != origin.exchange || event.detail != origin.detail || event.receipt <= receiptFloor || hasReceipt(event.receipt)) return false
                    active=event;pending=null;receiptFloor=event.receipt
                } else {
                    if(event.receipt != current.receipt || event.detail != current.detail || event.originNonce != current.originNonce || event.originExchange != current.originExchange) return false
                    val allowed=current.state == 1 && event.state in listOf(2,5,6) || current.state == 2 && event.state in listOf(3,5,6)
                    if(!allowed) return false
                    if(event.state in listOf(3,5,6)) {
                        active=null;appendTerminal(event);if(event.state == 6) unknown=true
                    } else active=event
                }
            }
            2 -> {
                if(event.receipt <= receiptFloor || hasReceipt(event.receipt)) return false
                receiptFloor=event.receipt;appendTerminal(event)
            }
            3 -> {
                active?.let { appendTerminal(it.copy(state=6,reason=if(event.state==8)3 else 7));unknown=true }
                if(pending != null) unknown=true
                active=null;pending=null;transport=event
            }
            4 -> {
                active?.let { appendTerminal(it.copy(state=6,reason=7)) }
                active=null;pending=null;unknown=true
            }
        }
        lastEvent=record.eventId
        return true
    }

    /** A new connection has no per-message historical recovery evidence. */
    fun disconnect() {
        if(connected || active != null || pending != null) unknown=true
        nonce=0u;generation=0uL;lastEvent=0u;lastRequest=0u;pending=null;active=null
        terminal.fill(null);head=0;count=0;transport=null
    }

    private fun hasReceipt(receipt: ULong): Boolean = active?.receipt == receipt ||
        (0 until count).any { terminal[(head+it)%16]?.receipt == receipt }

    private fun appendTerminal(event: CompanionRadioEvent) {
        if(count == 16) {
            terminal[head]=event;head=(head+1)%16;unknown=true
        } else {
            terminal[(head+count)%16]=event;count++
        }
    }

    override fun toString() = "CompanionRadioEventReceiver(connected=$connected, terminalCount=$count, deliveryUnknown=$unknown, fields=redacted)"
}
