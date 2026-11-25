package io.github.nbjelanovic.otprotocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

/**
 * F8 step 2e Join Progress. The vectors match the firmware codec test
 * (companion_group_owner_tests.cpp, join_progress_reports_the_board_step).
 */
class CompanionGroupJoinProgressTest {
    private val codec = CompanionGroupConfigurationCodec

    @Test
    fun commandMatchesFirmwareBytes() {
        assertContentEquals(hex("4f54474301080000"), codec.joinProgress())
        assertEquals(true, codec.validCommand(codec.joinProgress()))
        assertEquals(false, codec.validCommand(hex("4f5447430108000000")))
    }

    @Test
    fun resultsMatchFirmwareBytes() {
        val review = assertNotNull(codec.decodeResult(hex("4f54474301080000" + "03010000" + "8877665544332211")))
        assertEquals(CompanionGroupOperation.JOIN_PROGRESS, review.operation)
        assertEquals(CompanionGroupStatus.OK, review.status)
        assertEquals(
            CompanionGroupJoinProgress(CompanionGroupJoinStep.REVIEW_CODE, CompanionGroupJoinSide.JOINER, 0x1122334455667788uL),
            review.progress,
        )
        val nothing = assertNotNull(codec.decodeResult(hex("4f54474301080000" + "00".repeat(12))))
        assertEquals(CompanionGroupJoinProgress(CompanionGroupJoinStep.NONE, CompanionGroupJoinSide.NONE, 0uL), nothing.progress)
        val unavailable = assertNotNull(codec.decodeResult(hex("4f54474301080100")))
        assertEquals(CompanionGroupStatus.UNAVAILABLE, unavailable.status)
        assertNull(unavailable.progress)
    }

    @Test
    fun malformedResultsAreRefused() {
        val good = "4f54474301080000" + "03010000" + "8877665544332211"
        for (bad in listOf(
            good.dropLast(2),
            good + "00",
            "4f54474301080000" + "09010000" + "8877665544332211", // unknown step
            "4f54474301080000" + "03030000" + "8877665544332211", // unknown side
            "4f54474301080000" + "03010100" + "8877665544332211", // reserved byte
            "4f54474301080000" + "03000000" + "8877665544332211", // a step without a side
            "4f54474301080000" + "03010000" + "0000000000000000", // a step without a group
            "4f54474301080000" + "00000000" + "0100000000000000", // no step but a group
            "4f54474301080100" + "00".repeat(12),                 // a refusal with a body
            "4f54474301080500",                                   // missing is not a join answer
        )) assertNull(codec.decodeResult(hex(bad)), bad)
    }

    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
