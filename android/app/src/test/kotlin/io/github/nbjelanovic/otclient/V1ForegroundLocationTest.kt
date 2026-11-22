package io.github.nbjelanovic.otclient

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class V1ForegroundLocationTest {
    private val direct = LocationAudience(LocationAudienceKind.DIRECT, 11, 1)
    private val link = LocationBinding(direct, 5)
    private val foreground = PhoneLocationEnvironment(true, true, true, link)
    private var now = 30_000L
    private val board = BoardStub()
    private val sharing = V1ForegroundLocation(board, { now })

    @Test fun foregroundPermissionAndExplicitResumeAreRequired() {
        assertFalse(sharing.chooseShare())
        sharing.updateEnvironment(foreground.copy(precisePermission = false))
        assertTrue(sharing.chooseShare())
        assertEquals(PhoneLocationState.ALLOW_PHONE_LOCATION, sharing.state)
        sharing.phoneFix(12.0, 40.0, now)
        assertTrue(board.starts.isEmpty())
        sharing.updateEnvironment(foreground.copy(gpsEnabled = false))
        assertEquals(PhoneLocationState.TURN_ON_PHONE_LOCATION, sharing.state)
        sharing.updateEnvironment(foreground)
        assertEquals(PhoneLocationState.PAUSED, sharing.state)
        assertTrue(sharing.resume())
        assertEquals(PhoneLocationState.WAITING_FOR_FIX, sharing.state)
        sharing.phoneFix(12.34564, -40.54326, now)
        val start = board.starts.single()
        assertEquals(123456, start.fix.latitudeE4)
        assertEquals(-405433, start.fix.longitudeE4)
        assertEquals(PhoneLocationState.WAITING_FOR_FIX, sharing.state)
        start.result(7)
        assertEquals(PhoneLocationState.SHARING, sharing.state)
        sharing.updateEnvironment(foreground.copy(precisePermission = false))
        assertEquals(listOf(link to 7L), board.stops)
        assertFalse(sharing.sampling)
        sharing.updateEnvironment(foreground)
        assertEquals(PhoneLocationState.PAUSED, sharing.state)
        sharing.stopSharing()
        assertEquals(PhoneLocationState.OFF, sharing.state)
    }

    @Test fun onlyCommittedFreshPositionsRenewEveryFiveSeconds() {
        startSharing()
        assertEquals(PhoneLocationState.SHARING, sharing.state)
        now += 4_999
        sharing.tick()
        assertTrue(board.renewals.isEmpty())
        now++
        sharing.tick()
        val renewal = board.renewals.single()
        assertEquals(5_000L, renewal.fix.ageMs)
        assertEquals(30_000L, renewal.fix.capturedAtMs)
        renewal.result(true)
        now = 45_000L
        sharing.tick()
        assertEquals(PhoneLocationState.WAITING_FOR_FIX, sharing.state)
        assertEquals(listOf(link to 7L), board.stops)
        assertEquals(1, board.renewals.size)
        sharing.phoneFix(12.0, 40.0, now - PHONE_FIX_FRESH_MS)
        assertEquals(1, board.starts.size)
    }

    @Test fun lateStartAfterBackgroundStopsOnlyItsOwnLease() {
        sharing.updateEnvironment(foreground)
        assertTrue(sharing.chooseShare())
        sharing.phoneFix(12.0, 40.0, now)
        val old = board.starts.single()
        sharing.updateEnvironment(PhoneLocationEnvironment())
        assertFalse(sharing.sampling)
        assertEquals(PhoneLocationState.PAUSED, sharing.state)
        val newLink = LocationBinding(direct, 6)
        sharing.updateEnvironment(foreground.copy(binding = newLink))
        old.result(8)
        assertEquals(listOf(link to 8L), board.stops)
        assertTrue(sharing.resume())
        sharing.phoneFix(12.0, 40.0, now)
        board.starts.last().result(8)
        assertEquals(PhoneLocationState.SHARING, sharing.state)
        old.result(8)
        assertTrue(board.stops.last().first == link)
        assertEquals(PhoneLocationState.SHARING, sharing.state)
    }

    @Test fun lossOfScopeClearsIntentAndNewScopeNeedsItsOwnChoice() {
        startSharing()
        val rekeyed = LocationBinding(direct.copy(generation = 2), link.attachment)
        sharing.updateEnvironment(foreground.copy(binding = rekeyed))
        sharing.phoneFix(12.0, 40.0, now)
        assertEquals(PhoneLocationState.OFF, sharing.state)
        assertFalse(sharing.resume())
        assertEquals(1, board.starts.size)
        assertEquals(listOf(link to 7L), board.stops)
    }

    @Test fun aLateReceiptCannotUseANewerUnsentFixToClaimSharing() {
        sharing.updateEnvironment(foreground)
        assertTrue(sharing.chooseShare())
        sharing.phoneFix(12.0, 40.0, 16_000)
        val start = board.starts.single()
        now = 31_100L
        sharing.phoneFix(12.0, 40.0, now)
        start.result(7)
        assertEquals(PhoneLocationState.PAUSED, sharing.state)
        assertEquals(listOf(link to 7L), board.stops)
    }

    @Test fun noFreshFixOrUnconfirmedLeaseNeverShowsSharing() {
        sharing.updateEnvironment(foreground)
        assertTrue(sharing.chooseShare())
        sharing.phoneFix(Double.NaN, 45.0, now)
        sharing.phoneFix(95.0, 45.0, now)
        sharing.phoneFix(12.0, 45.0, now + 1)
        assertTrue(board.starts.isEmpty())
        sharing.phoneFix(12.0, 45.0, now)
        board.starts.single().result(null)
        assertEquals(PhoneLocationState.PAUSED, sharing.state)
        assertTrue(sharing.resume())
        now++
        sharing.phoneFix(12.0, 45.0, now)
        assertNotNull(board.starts.lastOrNull())
        now += LOCATION_LEASE_MS
        sharing.tick()
        board.starts.last().result(7)
        assertEquals(listOf(link to 7L), board.stops)
        assertFalse(sharing.sampling)
    }

    private fun startSharing() {
        sharing.updateEnvironment(foreground)
        assertTrue(sharing.chooseShare())
        sharing.phoneFix(12.0, 40.0, now)
        board.starts.single().result(7)
    }

    private class BoardStub : PhoneLocationBoard {
        class Start(val fix: PhoneLocationFix, val result: (Long?) -> Unit)
        class Renewal(val fix: PhoneLocationFix, val result: (Boolean) -> Unit)
        val starts = mutableListOf<Start>()
        val renewals = mutableListOf<Renewal>()
        val stops = mutableListOf<Pair<LocationBinding, Long>>()

        override fun start(binding: LocationBinding, fix: PhoneLocationFix, result: (Long?) -> Unit) {
            starts.add(Start(fix, result))
        }
        override fun renew(binding: LocationBinding, lease: Long, fix: PhoneLocationFix, result: (Boolean) -> Unit) {
            renewals.add(Renewal(fix, result))
        }
        override fun stop(binding: LocationBinding, lease: Long) {
            stops.add(binding to lease)
        }
    }
}
