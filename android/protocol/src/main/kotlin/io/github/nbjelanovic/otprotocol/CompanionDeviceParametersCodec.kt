package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

@ConsistentCopyVisibility
data class DeviceParameterSample<T> internal constructor(val sourceAgeMillis: Long, val value: T) {
    /** An age of MAX_AGE_MILLIS means at least that old. */
    fun ageMillis(snapshotAgeMillis: Long): Long {
        require(snapshotAgeMillis >= 0)
        val limit = CompanionDeviceParametersCodec.MAX_AGE_MILLIS
        return (sourceAgeMillis + snapshotAgeMillis.coerceAtMost(limit)).coerceAtMost(limit)
    }
}

data class DeviceParameterBattery(val millivolts: Int, val percent: Int)
enum class DeviceParameterPowerSource { UNKNOWN, BATTERY_ONLY, EXTERNAL, CHARGING, FAULT }
data class DeviceParameterLastPacket(val rssiDbm: Int, val snrQuarterDb: Int, val frequencyHz: Long)
data class DeviceParameterRadioCounters(
    val txAttempts: Long,
    val txCompleted: Long,
    val rxFrames: Long,
    val rxErrors: Long,
    val rxDropped: Long,
    val txRefused: Long,
    val txBusyDropped: Long,
    val txPrelaunchDropped: Long,
)
enum class DeviceParameterGnssState { UNKNOWN, UNAVAILABLE, SEARCHING, FIX, STALE, INVALID }
data class DeviceParameterGnss(val state: DeviceParameterGnssState, val satellites: Int?)
enum class DeviceParameterResetReason {
    UNKNOWN, POWER_ON, EXTERNAL, SOFTWARE, PANIC, WATCHDOG, DEEP_SLEEP, BROWNOUT, OTHER,
}
data class DeviceParameterHeap(val freeBytes: Long, val minimumFreeBytes: Long)
data class DeviceParameterStorage(val usedEntries: Long, val freeEntries: Long, val totalEntries: Long)
data class DeviceParameterFirmware(
    val releaseGeneration: Long,
    val applicationPartitionOffset: Long,
    val version: String,
)

/** Null groups are unavailable. All counters are unsigned 32-bit values and may wrap. */
class CompanionDeviceParameters internal constructor(
    val sequence: Long,
    val snapshotAgeMillis: Long,
    val collectionUptimeMillis: ULong,
    val battery: DeviceParameterSample<DeviceParameterBattery>?,
    val power: DeviceParameterSample<DeviceParameterPowerSource>?,
    val lastPacket: DeviceParameterSample<DeviceParameterLastPacket>?,
    val radioCounters: DeviceParameterSample<DeviceParameterRadioCounters>?,
    val cadDeferrals: DeviceParameterSample<Long>?,
    val gnss: DeviceParameterSample<DeviceParameterGnss>?,
    val reset: DeviceParameterSample<DeviceParameterResetReason>?,
    val heap: DeviceParameterSample<DeviceParameterHeap>?,
    val mainStack: DeviceParameterSample<Long>?,
    val bleStack: DeviceParameterSample<Long>?,
    val storage: DeviceParameterSample<DeviceParameterStorage>?,
    val firmware: DeviceParameterSample<DeviceParameterFirmware>?,
)

/** Version 1 OTDP record from the firmware F11 codec. Protected transport must admit the bytes first. */
object CompanionDeviceParametersCodec {
    const val RECORD_BYTES = 212
    const val MAX_AGE_MILLIS = 0xffff_ffffL

    // In validity-mask bit order, with the final offset marking the end of the last group.
    private val offsets = intArrayOf(32, 40, 48, 60, 96, 104, 112, 120, 132, 140, 148, 164, RECORD_BYTES)
    private val reserved = intArrayOf(5, 28, 29, 30, 31, 39, 45, 46, 47, 111, 117, 118, 119, 173, 174, 175)

    fun decode(bytes: ByteArray): CompanionDeviceParameters? {
        if (bytes.size != RECORD_BYTES) return null
        val wire = LittleEndianRecord(bytes.copyOf())
        if (wire.u8(0) != 'O'.code || wire.u8(1) != 'T'.code ||
            wire.u8(2) != 'D'.code || wire.u8(3) != 'P'.code || wire.u8(4) != 1 ||
            wire.u16(6) != RECORD_BYTES || reserved.any { wire.u8(it) != 0 }) return null

        val sequence = wire.u32(8)
        val age = wire.u32(12)
        val uptime = wire.u64(16)
        val mask = wire.u32(24)
        if (sequence == 0L || mask > 0xfffL || uptime > ULong.MAX_VALUE - age.toULong()) return null

        fun present(bit: Int): Boolean = (mask.toInt() and (1 shl bit)) != 0
        for (bit in 0..11) {
            val start = offsets[bit]
            if (present(bit)) {
                if (wire.u32(start).toULong() > uptime) return null
            } else if (!wire.zeros(start, offsets[bit + 1])) return null
        }

        fun <T> sample(bit: Int, value: T): DeviceParameterSample<T>? =
            if (present(bit)) DeviceParameterSample(wire.u32(offsets[bit]), value) else null

        val battery = DeviceParameterBattery(wire.u16(36), wire.u8(38))
        if (present(0) && (battery.millivolts !in 2800..4400 || battery.percent > 100)) return null
        val power = DeviceParameterPowerSource.entries.getOrNull(wire.u8(44)) ?: return null
        val packet = DeviceParameterLastPacket(wire.i16(52), wire.i16(54), wire.u32(56))
        if (present(2) && (packet.rssiDbm !in -200..0 || packet.snrQuarterDb !in -128..127 ||
                packet.frequencyHz !in 100_000_000L..1_000_000_000L)) return null
        val radio = DeviceParameterRadioCounters(
            wire.u32(64), wire.u32(68), wire.u32(72), wire.u32(76),
            wire.u32(80), wire.u32(84), wire.u32(88), wire.u32(92),
        )

        val gnssState = DeviceParameterGnssState.entries.getOrNull(wire.u8(108)) ?: return null
        val count = wire.u8(109)
        val knownCount = wire.u8(110)
        if (knownCount > 1 || count > 99 || (knownCount == 0 && count != 0)) return null
        val gnss = DeviceParameterGnss(gnssState, count.takeIf { knownCount == 1 })
        val reset = DeviceParameterResetReason.entries.getOrNull(wire.u8(116)) ?: return null
        val heap = DeviceParameterHeap(wire.u32(124), wire.u32(128))
        if (heap.freeBytes > 64 * 1024 * 1024 || heap.minimumFreeBytes > heap.freeBytes) return null
        val mainStack = wire.u32(136)
        val bleStack = wire.u32(144)
        if (mainStack > 1024 * 1024 || bleStack > 1024 * 1024) return null
        val storage = DeviceParameterStorage(wire.u32(152), wire.u32(156), wire.u32(160))
        if (present(10) && (storage.totalEntries !in 1L..1_000_000L ||
                storage.usedEntries + storage.freeEntries != storage.totalEntries)) return null

        val firmware = if (present(11)) {
            val generation = wire.u32(168)
            val length = wire.u8(172)
            val partition = wire.u32(176)
            if (generation >= 1_000_000_000 || length !in 1..32 ||
                (partition != 0L && (partition !in 0x10000L..0xff0000L || partition % 0x10000L != 0L))) return null

            val tokenEnd = 180 + length
            if (!(180 until tokenEnd).all { isVersionCharacter(wire.u8(it).toChar()) } ||
                !wire.zeros(tokenEnd, RECORD_BYTES)) return null
            sample(11, DeviceParameterFirmware(generation, partition, wire.ascii(180, length)))
        } else null

        return CompanionDeviceParameters(
            sequence, age, uptime, sample(0, battery), sample(1, power), sample(2, packet),
            sample(3, radio), sample(4, wire.u32(100)), sample(5, gnss), sample(6, reset),
            sample(7, heap), sample(8, mainStack), sample(9, bleStack), sample(10, storage), firmware,
        )
    }

    private fun isVersionCharacter(c: Char): Boolean =
        c in 'a'..'z' || c in 'A'..'Z' || c in '0'..'9' || c in "._-+"

    private class LittleEndianRecord(private val bytes: ByteArray) {
        private val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)

        fun u8(offset: Int): Int = bytes[offset].toInt() and 0xff
        fun i16(offset: Int): Int = buffer.getShort(offset).toInt()
        fun u16(offset: Int): Int = i16(offset) and 0xffff
        fun u32(offset: Int): Long = buffer.getInt(offset).toLong() and 0xffff_ffffL
        fun u64(offset: Int): ULong = buffer.getLong(offset).toULong()
        fun zeros(start: Int, end: Int): Boolean = (start until end).all { bytes[it] == 0.toByte() }
        fun ascii(start: Int, length: Int): String = String(bytes, start, length, Charsets.US_ASCII)
    }
}
