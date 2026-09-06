package io.github.nbjelanovic.otprotocol

/** Isolated candidate 0.2 profile; no advertisement, authority or dispatch. */
data class CompanionConfigurationInfo(
    val capabilities: Int,
    val role: Int = 1,
    val maxPayloadBytes: Int = 128,
    val minimumAttMtu: Int = 151,
    val fragmentCount: Int = 1,
    val controllerCount: Int = 1,
)

class CompanionConfigurationFrame(
    val kind: Int,
    val sessionNonce: UInt,
    val exchangeId: UInt,
    payload: ByteArray,
) {
    private val ownedPayload = payload.copyOf()
    val payload: ByteArray get() = ownedPayload.copyOf()
    override fun toString() = "CompanionConfigurationFrame(kind=$kind, identity=redacted, payload=redacted)"
}

data class CompanionTimePayload(
    val kind: Int,
    val code: Int = 0,
    val format: Int = 0,
    val challenge: ULong = 0u,
    val localSecond: UInt = 0u,
) {
    override fun toString() = "CompanionTimePayload(kind=$kind, code=$code, fields=redacted)"
}

object CompanionConfigurationCodec {
    const val NAME_CAPABILITY = 0x40
    const val TIME_CAPABILITY = 0x80
    const val KNOWN_CAPABILITIES = 0xef
    const val MAX_PAYLOAD_BYTES = 128
    const val MAX_RECORD_BYTES = 148
    const val MINIMUM_MTU = 151
    private val infoMagic = byteArrayOf(0x4f,0x54,0x42,0x30)
    private val frameMagic = byteArrayOf(0x4f,0x54,0x43,0x30)
    private val timeMagic = byteArrayOf(0x4f,0x54,0x54,0x43)

    private fun validInfo(value: CompanionConfigurationInfo) = value.role == 1 &&
        value.capabilities in 0..255 && value.capabilities and KNOWN_CAPABILITIES.inv() == 0 &&
        value.capabilities and (NAME_CAPABILITY or TIME_CAPABILITY) != 0 &&
        value.maxPayloadBytes == MAX_PAYLOAD_BYTES && value.minimumAttMtu == MINIMUM_MTU &&
        value.fragmentCount == 1 && value.controllerCount == 1

    /** Capacity/feature compatibility only; this does not authenticate or establish Ready. */
    fun transportCompatible(info: CompanionConfigurationInfo, requiredCapability: Int, mtu: Int,
                            sendCapacity: Int, receiveCapacity: Int, indications: Boolean): Boolean =
        validInfo(info) && requiredCapability in listOf(NAME_CAPABILITY,TIME_CAPABILITY) &&
            info.capabilities and requiredCapability != 0 && mtu in MINIMUM_MTU..65535 &&
            sendCapacity >= MAX_RECORD_BYTES && receiveCapacity >= MAX_RECORD_BYTES && indications

    fun encodeInfo(value: CompanionConfigurationInfo): ByteArray? {
        if (!validInfo(value)) return null
        return ByteArray(16).also {
            infoMagic.copyInto(it); it[5]=2; it[6]=1; it[7]=value.capabilities.toByte()
            put(it,8,128u,2); put(it,10,151u,2); it[12]=1; it[13]=1
        }
    }

    fun decodeInfo(bytes: ByteArray): CompanionConfigurationInfo? {
        if(bytes.size != 16 || !magic(bytes,infoMagic) || u8(bytes,4)!=0 || u8(bytes,5)!=2 ||
            u8(bytes,14)!=0 || u8(bytes,15)!=0) return null
        return CompanionConfigurationInfo(u8(bytes,7),u8(bytes,6),get(bytes,8,2).toInt(),
            get(bytes,10,2).toInt(),u8(bytes,12),u8(bytes,13)).takeIf(::validInfo)
    }

    private fun validTime(value: CompanionTimePayload): Boolean {
        if(value.code !in 0..8) return false
        return when(value.kind) {
            1 -> value.code==0 && value.format==0 && value.challenge==0uL && value.localSecond==0u
            2 -> value.code==0 && value.format==0 && value.challenge!=0uL && value.localSecond==0u
            3 -> value.code==0 && value.format in 1..2 && value.challenge!=0uL && value.localSecond<86400u
            4 -> value.format==0 && value.localSecond==0u && (value.code!=0 || value.challenge!=0uL)
            else -> false
        }
    }

    fun encodeTime(value: CompanionTimePayload): ByteArray? {
        if(!validTime(value)) return null
        return ByteArray(24).also {
            timeMagic.copyInto(it);it[4]=1;it[5]=value.kind.toByte();it[6]=value.code.toByte();it[7]=value.format.toByte()
            put(it,8,value.challenge,8);put(it,16,value.localSecond.toULong(),4)
        }
    }

    fun decodeTime(bytes: ByteArray): CompanionTimePayload? {
        if(bytes.size!=24 || !magic(bytes,timeMagic) || u8(bytes,4)!=1 || (20..23).any { u8(bytes,it)!=0 }) return null
        return CompanionTimePayload(u8(bytes,5),u8(bytes,6),u8(bytes,7),get(bytes,8,8),get(bytes,16,4).toUInt())
            .takeIf(::validTime)
    }

    private fun validPayload(kind: Int, payload: ByteArray): Boolean {
        if(payload.size>MAX_PAYLOAD_BYTES) return false
        return when(kind) {
            1,2,0x81,0x82,0x83 -> true // Existing semantic validation remains a separate dispatch gate.
            4 -> CompanionNamePayloadCodec.decode(payload)?.kind in listOf(CompanionNameKind.READ,CompanionNameKind.WRITE)
            0x86 -> CompanionNamePayloadCodec.decode(payload)?.kind in listOf(CompanionNameKind.SNAPSHOT,
                CompanionNameKind.APPLIED,CompanionNameKind.REJECTED,CompanionNameKind.UNCERTAIN)
            5 -> decodeTime(payload)?.kind in listOf(1,3)
            0x87 -> decodeTime(payload)?.kind in listOf(2,4)
            else -> false
        }
    }

    fun encodeFrame(value: CompanionConfigurationFrame): ByteArray? {
        val payload=value.payload
        if(value.sessionNonce==0u || value.exchangeId==0u || !validPayload(value.kind,payload)) return null
        return ByteArray(20+payload.size).also {
            frameMagic.copyInto(it);it[5]=2;it[6]=value.kind.toByte();it[17]=1
            put(it,8,value.sessionNonce.toULong(),4);put(it,12,value.exchangeId.toULong(),4)
            put(it,18,payload.size.toULong(),2);payload.copyInto(it,20)
        }
    }

    fun decodeFrame(bytes: ByteArray): CompanionConfigurationFrame? {
        if(bytes.size !in 20..MAX_RECORD_BYTES || !magic(bytes,frameMagic) || u8(bytes,4)!=0 ||
            u8(bytes,5)!=2 || u8(bytes,7)!=0 || u8(bytes,16)!=0 || u8(bytes,17)!=1 ||
            get(bytes,18,2).toInt()!=bytes.size-20) return null
        val session=get(bytes,8,4).toUInt();val exchange=get(bytes,12,4).toUInt()
        val payload=bytes.copyOfRange(20,bytes.size);val kind=u8(bytes,6)
        if(session==0u || exchange==0u || !validPayload(kind,payload)) return null
        return CompanionConfigurationFrame(kind,session,exchange,payload)
    }

    private fun magic(bytes: ByteArray, expected: ByteArray) = expected.indices.all { bytes[it]==expected[it] }
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
