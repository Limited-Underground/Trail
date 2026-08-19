package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PublicLinkProbeContractTest {
    private val expected = byteArrayOf(
        0x4f, 0x54, 0x42, 0x30,
        0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x18, 0x00,
        0x01, 0x01, 0x00, 0x00,
    )

    @Test
    fun exactValueOnlyIsAccepted() {
        assertTrue(CompanionGattV0Contract.acceptsPublicLinkInfo(expected))
        assertFalse(CompanionGattV0Contract.acceptsPublicLinkInfo(byteArrayOf()))
        assertFalse(CompanionGattV0Contract.acceptsPublicLinkInfo(expected.copyOf(15)))
        assertFalse(CompanionGattV0Contract.acceptsPublicLinkInfo(expected + 0x00))
    }

    @Test
    fun everyBitMutationIsRejectedAndInputIsNotRetained() {
        repeat(expected.size * 8) { bit ->
            val changed = expected.copyOf()
            val index = bit / 8
            changed[index] = (changed[index].toInt() xor (1 shl (bit % 8))).toByte()
            assertFalse(CompanionGattV0Contract.acceptsPublicLinkInfo(changed))
        }
        val mutable = expected.copyOf()
        assertTrue(CompanionGattV0Contract.acceptsPublicLinkInfo(mutable))
        mutable.fill(0)
        assertTrue(CompanionGattV0Contract.acceptsPublicLinkInfo(expected))
    }
}
