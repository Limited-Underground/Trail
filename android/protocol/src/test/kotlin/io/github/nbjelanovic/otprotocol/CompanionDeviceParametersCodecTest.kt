package io.github.nbjelanovic.otprotocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class CompanionDeviceParametersCodecTest {
    @Test
    fun firmwareFullRecordDecodesEveryGroupAndAge() {
        val bytes = fixture()
        val record = assertNotNull(CompanionDeviceParametersCodec.decode(bytes))
        assertEquals(212, CompanionDeviceParametersCodec.RECORD_BYTES)
        assertEquals(0x01020304L, record.sequence)
        assertEquals(42L, record.snapshotAgeMillis)
        assertEquals(1_000_000uL, record.collectionUptimeMillis)
        assertEquals(DeviceParameterSample(1, DeviceParameterBattery(3700, 56)), record.battery)
        assertEquals(DeviceParameterSample(2, DeviceParameterPowerSource.BATTERY_ONLY), record.power)
        assertEquals(DeviceParameterSample(3, DeviceParameterLastPacket(-84, -13, 915_000_000)), record.lastPacket)
        assertEquals(DeviceParameterSample(4, DeviceParameterRadioCounters(1, 2, 3, 4, 5, 6, 7, 8)), record.radioCounters)
        assertEquals(DeviceParameterSample(5, 9L), record.cadDeferrals)
        assertEquals(DeviceParameterSample(6, DeviceParameterGnss(DeviceParameterGnssState.FIX, 12)), record.gnss)
        assertEquals(DeviceParameterSample(7, DeviceParameterResetReason.SOFTWARE), record.reset)
        assertEquals(DeviceParameterSample(8, DeviceParameterHeap(200_000, 100_000)), record.heap)
        assertEquals(DeviceParameterSample(9, 12_000L), record.mainStack)
        assertEquals(DeviceParameterSample(10, 5_000L), record.bleStack)
        assertEquals(DeviceParameterSample(11, DeviceParameterStorage(6, 4, 10)), record.storage)
        assertEquals(DeviceParameterSample(12, DeviceParameterFirmware(7, 0x10000, "v1.2.3-test+4")), record.firmware)
        assertEquals(54L, record.firmware?.ageMillis(record.snapshotAgeMillis))
        bytes.fill(0)
        assertEquals("v1.2.3-test+4", record.firmware?.value?.version)
    }

    @Test
    fun sparseGroupsRejectEveryHiddenByte() {
        val starts = intArrayOf(32, 40, 48, 60, 96, 104, 112, 120, 132, 140, 148, 164, 212)
        for (bit in 0..11) {
            val sparse = fixture().apply {
                littleEndian().putInt(24, 0xfff xor (1 shl bit))
                fill(0, starts[bit], starts[bit + 1])
            }
            assertNotNull(CompanionDeviceParametersCodec.decode(sparse))
            for (offset in starts[bit] until starts[bit + 1]) {
                assertNull(CompanionDeviceParametersCodec.decode(sparse.copyOf().apply { this[offset] = 1 }))
            }
        }
        val sparse = fixture().apply { fill(0, 24) }
        val record = assertNotNull(CompanionDeviceParametersCodec.decode(sparse))
        assertNull(record.battery)
        assertNull(record.lastPacket)
        assertNull(record.gnss)
        assertNull(record.firmware)
    }

    @Test
    fun rejectsWrongHeaderLengthReservedBitsAndImpossibleAges() {
        val input = fixture()
        for (length in 0..213) {
            if (length != 212) assertNull(CompanionDeviceParametersCodec.decode(input.copyOf(length)), "length $length")
        }
        for (version in 0..255) {
            if (version != 1) bad { this[4] = version.toByte() }
        }
        for (offset in listOf(0, 1, 2, 3, 5, 28, 29, 30, 31, 39, 45, 46, 47, 111, 117, 118, 119, 173, 174, 175, 211)) {
            bad { this[offset] = (this[offset].toInt() xor 1).toByte() }
        }
        for (bit in 12..31) bad { littleEndian().putInt(24, 0xfff or (1 shl bit)) }
        bad { littleEndian().putShort(6, 211) }
        bad { littleEndian().putInt(8, 0) }
        bad { littleEndian().putLong(16, -1L) }
        for (offset in listOf(32, 40, 48, 60, 96, 104, 112, 120, 132, 140, 148, 164)) {
            bad { littleEndian().putInt(offset, 1_000_001) }
        }
    }

    @Test
    fun rejectsMalformedValuesAndFirmwareToken() {
        bad { littleEndian().putShort(36, 2799) }
        bad { littleEndian().putShort(36, 4401) }
        bad { this[38] = 101 }
        bad { this[44] = 5 }
        bad { littleEndian().putShort(52, -201) }
        bad { littleEndian().putShort(52, 1) }
        bad { littleEndian().putShort(54, -129) }
        bad { littleEndian().putShort(54, 128) }
        bad { littleEndian().putInt(56, 99_999_999) }
        bad { littleEndian().putInt(56, 1_000_000_001) }
        bad { this[108] = 6 }
        bad { this[109] = 100 }
        bad { this[110] = 0 }
        bad { this[110] = 2 }
        bad { this[116] = 9 }
        bad { littleEndian().putInt(124, 64 * 1024 * 1024 + 1) }
        bad { littleEndian().putInt(128, 200_001) }
        bad { littleEndian().putInt(136, 1024 * 1024 + 1) }
        bad { littleEndian().putInt(144, 1024 * 1024 + 1) }
        bad { littleEndian().putInt(160, 0) }
        bad { littleEndian().putInt(160, 1_000_001) }
        bad { littleEndian().putInt(152, -1).putInt(156, 2).putInt(160, 1) }
        bad { littleEndian().putInt(168, 1_000_000_000) }
        bad { this[172] = 0 }
        bad { this[172] = 33 }
        bad { littleEndian().putInt(176, 0x10001) }
        bad { littleEndian().putInt(176, 0x1000000) }
        for (value in listOf(0, 10, 13, 32, 47, 58, 92, 127, 255)) {
            bad { this[180] = value.toByte() }
        }
    }

    @Test
    fun preservesKnownZeroUnsignedCountersAndSaturatingAges() {
        val input = fixture().apply {
            littleEndian().putInt(8, -1).putInt(12, -1).putLong(16, Long.MIN_VALUE)
                .putInt(48, -1).putShort(52, 0).putShort(54, 0)
                .putInt(64, -1).putInt(68, 0).putInt(136, 0)
            this[109] = 0
        }
        val record = assertNotNull(CompanionDeviceParametersCodec.decode(input))
        assertEquals(0xffff_ffffL, record.sequence)
        assertEquals(0x8000_0000_0000_0000uL, record.collectionUptimeMillis)
        assertEquals(0, record.lastPacket?.value?.rssiDbm)
        assertEquals(0, record.lastPacket?.value?.snrQuarterDb)
        assertEquals(0xffff_ffffL, record.lastPacket?.ageMillis(record.snapshotAgeMillis))
        assertEquals(0xffff_ffffL, record.radioCounters?.value?.txAttempts)
        assertEquals(0L, record.radioCounters?.value?.txCompleted)
        assertEquals(0L, record.mainStack?.value)
        assertEquals(0, record.gnss?.value?.satellites)
        input[110] = 0
        assertNull(assertNotNull(CompanionDeviceParametersCodec.decode(input)).gnss?.value?.satellites)
    }

    private fun bad(edit: ByteArray.() -> Unit) {
        assertNull(CompanionDeviceParametersCodec.decode(fixture().apply(edit)))
    }

    private fun ByteArray.littleEndian(): ByteBuffer = ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN)

    // Fixed wire vector from the firmware codec test's sample(), in record order.
    private fun fixture(): ByteArray = (
        "4f5444500100d400040302012a00000040420f0000000000ff0f000000000000" +
        "01000000740e3800" +
        "0200000001000000" +
        "03000000acfff3ffc0ca8936" +
        "040000000100000002000000030000000400000005000000060000000700000008000000" +
        "0500000009000000" +
        "06000000030c0100" +
        "0700000003000000" +
        "08000000400d0300a0860100" +
        "09000000e02e0000" +
        "0a00000088130000" +
        "0b00000006000000040000000a000000" +
        "0c000000070000000d0000000000010076312e322e332d746573742b3400000000000000000000000000000000000000"
    ).chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
