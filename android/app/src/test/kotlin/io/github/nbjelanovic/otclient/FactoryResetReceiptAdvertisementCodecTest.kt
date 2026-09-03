package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNull

class FactoryResetReceiptAdvertisementCodecTest {
    @Test
    fun exactOtrrV1PayloadUsesUnsignedLittleEndianReceipt() {
        val receipt = 0x1122334455667788uL
        val encoded = FactoryResetReceiptAdvertisementCodec.encode(receipt)

        assertContentEquals(
            byteArrayOf(
                0x4f, 0x54, 0x52, 0x52, 0x01,
                0x88.toByte(), 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
            ),
            encoded,
        )
        assertEquals(receipt, FactoryResetReceiptAdvertisementCodec.decode(encoded))
    }

    @Test
    fun zeroMalformedWrongVersionAndWrongMagicFailClosed() {
        assertNull(FactoryResetReceiptAdvertisementCodec.encode(0uL))
        assertNull(FactoryResetReceiptAdvertisementCodec.decode(null))
        assertNull(FactoryResetReceiptAdvertisementCodec.decode(ByteArray(12)))
        assertNull(
            FactoryResetReceiptAdvertisementCodec.decode(
                byteArrayOf(0x4f, 0x54, 0x52, 0x52, 0x01, 0, 0, 0, 0, 0, 0, 0, 0),
            ),
        )
        val valid = FactoryResetReceiptAdvertisementCodec.encode(1uL)!!
        assertNull(FactoryResetReceiptAdvertisementCodec.decode(valid.copyOf().also { it[0] = 0 }))
        assertNull(FactoryResetReceiptAdvertisementCodec.decode(valid.copyOf().also { it[4] = 2 }))
    }
}
