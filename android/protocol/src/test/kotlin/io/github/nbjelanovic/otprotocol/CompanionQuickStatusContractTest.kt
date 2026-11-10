package io.github.nbjelanovic.otprotocol

import kotlin.test.*

/** Existing companion_semantics.cpp and OTE0 host ingress contract, not a new text format. */
class CompanionQuickStatusContractTest {
    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    @Test fun allFourActionsAreByteExactAgainstFirmwareOta0Layout() {
        for (status in CompanionQuickStatus.entries) {
            val payload=assertNotNull(CompanionSemanticCodec.encodeActionRequest(CompanionActionRequest(CompanionActionKind.QUICK_STATUS,status)).value)
            val expected=hex("4f5441300000010"+status.wireValue+"00".repeat(12))
            assertContentEquals(expected,payload);assertEquals(20,payload.size)
            val record=assertNotNull(CompanionConfigurationCodec.encodeFrame(CompanionConfigurationFrame(2,17u,1u,payload,6),allowProfile06=true))
            assertContentEquals(hex("4f54433000060200110000000100000000011400")+expected,record)
            assertEquals(status,CompanionSemanticCodec.decodeActionRequest(payload).value?.quickStatus)
            for(size in listOf(19,21)) assertNull(CompanionSemanticCodec.decodeActionRequest(payload.copyOf(size)).value)
            for(size in listOf(39,41)) assertNull(CompanionConfigurationCodec.decodeFrame(record.copyOf(size),6,allowProfile06=true))
            for(offset in listOf(0,4,5,6,7,16,17,18,19)) {
                val bad=payload.copyOf();bad[offset]=0x7f
                assertNull(CompanionSemanticCodec.decodeActionRequest(bad).value,"offset $offset")
            }
        }
    }
    @Test fun receivedStatusEventsUseExactFirmwareOte0SixtyByteRecords() {
        for(status in CompanionQuickStatus.entries) {
            val event=CompanionRadioEvent(2,status.wireValue,100u,3u,4,0)
            val payload=hex("4f5445300000020"+status.wireValue+"6400000000000000030000000000000004000000000000000000000000000000")
            assertEquals(40,payload.size);assertContentEquals(payload,CompanionRadioEventCodec.encodeEvent(event))
            val record=assertNotNull(CompanionRadioEventCodec.encodeRecord(CompanionRadioEventRecord(17u,1u,event),allowProfile06=true))
            assertContentEquals(hex("4f54433000068300110000000100000000012800")+payload,record)
            assertNotNull(CompanionRadioEventCodec.decodeRecord(record,allowProfile06=true));assertNull(CompanionRadioEventCodec.decodeRecord(record))
            for(size in listOf(59,61)) assertNull(CompanionRadioEventCodec.decodeRecord(record.copyOf(size),allowProfile06=true))
            for(offset in listOf(0,4,5,6,7,26,27,28,29,30,31)) {
                val bad=payload.copyOf();bad[offset]=0x7f
                assertNull(CompanionRadioEventCodec.decodeEvent(bad),"offset $offset")
            }
        }
    }
}
