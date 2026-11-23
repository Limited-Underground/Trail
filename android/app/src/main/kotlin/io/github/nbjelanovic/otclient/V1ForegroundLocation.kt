package io.github.nbjelanovic.otclient

import kotlin.math.roundToInt

internal const val PHONE_FIX_FRESH_MS = 15_000L
internal const val LOCATION_LEASE_MS = 15_000L
internal const val LOCATION_RENEW_MS = 5_000L

enum class PhoneLocationState(val label: String) {
    OFF("Off"),
    ALLOW_PHONE_LOCATION("Allow phone location"),
    TURN_ON_PHONE_LOCATION("Turn on phone location"),
    WAITING_FOR_FIX("Waiting for phone GPS"),
    SHARING("Sharing"),
    PAUSED("Paused"),
}

internal enum class LocationAudienceKind { GROUP, DIRECT }

/** Only a protected roster or active direct-conversation owner supplies this identity. */
internal data class LocationAudience(
    val kind: LocationAudienceKind,
    val scopeId: Long,
    val generation: Long,
    private val rosterAuthority: Any? = null,
) {
    init { require(scopeId != 0L && generation > 0) }
    override fun toString() = "LocationAudience(redacted)"
}

internal data class LocationBinding(val audience: LocationAudience, val attachment: Long) {
    init { require(attachment > 0) }
    override fun toString() = "LocationBinding(redacted)"
}

internal data class PhoneLocationEnvironment(
    val foreground: Boolean = false,
    val precisePermission: Boolean = false,
    val gpsEnabled: Boolean = false,
    val binding: LocationBinding? = null,
) {
    val canSample: Boolean
        get() = foreground && precisePermission && gpsEnabled && binding != null
}

internal class PhoneLocationFix(
    val latitudeE4: Int,
    val longitudeE4: Int,
    val capturedAtMs: Long,
    val ageMs: Long,
) {
    override fun toString() = "PhoneLocationFix(redacted)"
}

/** Commands and replies belong to the exact protected attachment. Callbacks are serialized
 * on the session owner. A positive result is readback of the board's lease. */
internal interface PhoneLocationBoard {
    fun laneAvailable(binding: LocationBinding): Boolean
    fun consent(binding: LocationBinding, result: (accepted: Boolean) -> Unit)
    fun start(binding: LocationBinding, fix: PhoneLocationFix, result: (lease: Long?) -> Unit)
    fun renew(binding: LocationBinding, lease: Long, fix: PhoneLocationFix?, result: (accepted: Boolean) -> Unit)
    fun stop(binding: LocationBinding, lease: Long)
    fun withdraw(binding: LocationBinding)
}

/** Opaque access from an Activity to one current verified group roster. */
class V1GroupLocationAuthority internal constructor(
    internal val binding: LocationBinding,
    internal val board: PhoneLocationBoard,
    internal val isCurrent: () -> Boolean,
)

internal class V1ForegroundLocation(
    private val board: PhoneLocationBoard,
    private val nowMs: () -> Long,
    private val onStateChanged: (PhoneLocationState) -> Unit = {},
) {
    private class Fix(val latE4: Int, val lonE4: Int, val capturedAt: Long)
    private class ConsentExchange(val binding: LocationBinding, val sentAt: Long)
    private class Exchange(val binding: LocationBinding, val sentAt: Long, val capturedAt: Long)
    private class ActiveLease(val binding: LocationBinding, val id: Long, val renewedAt: Long, val capturedAt: Long)

    private var environment = PhoneLocationEnvironment()
    private var consent: LocationAudience? = null
    private var consentTarget: LocationBinding? = null
    private var consentPending: ConsentExchange? = null
    private var confirmedConsent: LocationBinding? = null
    private var needsResume = false
    private var latestFix: Fix? = null
    private var pending: Exchange? = null
    private var active: ActiveLease? = null
    private var shownState = PhoneLocationState.OFF

    val sampling: Boolean
        get() = canRequestLocation && confirmedConsent == environment.binding

    private val canRequestLocation: Boolean
        get() = consent != null && environment.canSample && !needsResume
    val state: PhoneLocationState
        get() = when {
            consent == null -> PhoneLocationState.OFF
            !environment.foreground || environment.binding == null -> PhoneLocationState.PAUSED
            !environment.precisePermission -> PhoneLocationState.ALLOW_PHONE_LOCATION
            !environment.gpsEnabled -> PhoneLocationState.TURN_ON_PHONE_LOCATION
            needsResume -> PhoneLocationState.PAUSED
            active != null -> PhoneLocationState.SHARING
            else -> PhoneLocationState.WAITING_FOR_FIX
        }

    fun updateEnvironment(next: PhoneLocationEnvironment) {
        val oldBinding = environment.binding
        environment = next
        val wanted = consent
        if (wanted != null && next.binding != null && wanted != next.binding.audience) {
            stopSharing()
            return
        }
        if (wanted != null && (!next.canSample || oldBinding != next.binding)) {
            pause()
        }
        publishState()
    }

    /** Only a new, deliberate choice in the authorized audience may call this. */
    fun chooseShare(): Boolean {
        val binding = environment.binding ?: return false
        if (!environment.foreground || consent != null) return false
        consent = binding.audience
        consentTarget = binding
        needsResume = !environment.canSample
        tick()
        publishState()
        return true
    }

    fun resume(): Boolean {
        if (consent == null || !needsResume || !environment.canSample) return false
        needsResume = false
        consentTarget = environment.binding
        tick()
        publishState()
        return true
    }

    fun stopSharing() {
        val target = consentTarget
        consent = null
        consentTarget = null
        needsResume = false
        clearBoardConsent()
        clearPosition()
        if (target != null) {
            try { board.withdraw(target) } catch (_: Exception) { }
        }
        publishState()
    }

    fun phoneFix(latitude: Double, longitude: Double, capturedAtMs: Long) {
        if (!sampling || latitude !in -90.0..90.0 || longitude !in -180.0..180.0 ||
            capturedAtMs <= (latestFix?.capturedAt ?: 0L) || !fresh(capturedAtMs, nowMs())) return
        latestFix = Fix((latitude * 10_000).roundToInt(), (longitude * 10_000).roundToInt(), capturedAtMs)
        tick()
    }

    fun tick() {
        if (!canRequestLocation) return
        val binding = environment.binding ?: return
        val now = nowMs()
        if (confirmedConsent != binding) {
            if (expired(consentPending?.sentAt, now)) {
                pause()
                publishState()
            } else if (consentPending == null && board.laneAvailable(binding)) {
                val exchange = ConsentExchange(binding, now)
                consentPending = exchange
                try {
                    board.consent(binding) { accepted -> finishConsent(exchange, accepted) }
                } catch (_: Exception) {
                    finishConsent(exchange, false)
                }
            }
            return
        }

        var lease = active
        if (expired(lease?.renewedAt, now) || expired(pending?.sentAt, now)) {
            pause()
            publishState()
            return
        }

        if (lease != null && !fresh(lease.capturedAt, now)) {
            retireLease()
            lease = null
            publishState()
        }
        val fix = latestFix
        if (fix == null || !fresh(fix.capturedAt, now)) {
            clearPosition()
            publishState()
            return
        }
        if (pending != null || (lease != null && now - lease.renewedAt < LOCATION_RENEW_MS) ||
            !board.laneAvailable(binding)) return

        val exchange = Exchange(binding, now, fix.capturedAt)
        val update = PhoneLocationFix(fix.latE4, fix.lonE4, fix.capturedAt, now - fix.capturedAt)
        pending = exchange
        try {
            if (lease == null) {
                board.start(binding, update) { id -> finish(exchange, id) }
            } else {
                board.renew(binding, lease.id, update.takeIf { it.capturedAtMs != lease.capturedAt }) { accepted ->
                    finish(exchange, lease.id.takeIf { accepted })
                }
            }
        } catch (_: Exception) {
            if (pending === exchange) finish(exchange, null)
        }
    }

    private fun finishConsent(exchange: ConsentExchange, accepted: Boolean) {
        if (consentPending !== exchange) return
        consentPending = null
        if (accepted && canRequestLocation && environment.binding == exchange.binding &&
            !expired(exchange.sentAt, nowMs())) {
            confirmedConsent = exchange.binding
        } else {
            pause()
        }
        publishState()
    }

    private fun finish(exchange: Exchange, leaseId: Long?) {
        val id = leaseId?.takeIf { it != 0L }
        if (pending !== exchange) {
            val current = active
            if (id != null && (current == null || current.binding != exchange.binding || current.id != id)) {
                stopBoard(exchange.binding, id)
            }
            return
        }
        pending = null
        val now = nowMs()
        val fix = latestFix
        if (id == null || !sampling || expired(exchange.sentAt, now) ||
            expired(active?.renewedAt, now) || !fresh(exchange.capturedAt, now) ||
            fix == null || !fresh(fix.capturedAt, now)) {
            if (id != null && active?.id != id) stopBoard(exchange.binding, id)
            pause()
        } else {
            active = ActiveLease(exchange.binding, id, exchange.sentAt, exchange.capturedAt)
        }
        publishState()
    }

    private fun pause() {
        needsResume = consent != null
        clearBoardConsent()
        clearPosition()
    }

    private fun clearBoardConsent() {
        consentPending = null
        confirmedConsent = null
    }

    private fun clearPosition() {
        latestFix = null
        retireLease()
    }

    private fun retireLease() {
        pending = null
        val lease = active ?: return
        active = null
        stopBoard(lease.binding, lease.id)
    }

    private fun stopBoard(binding: LocationBinding, id: Long) {
        // The board lease still expires if the exact protected link cannot deliver Stop.
        try { board.stop(binding, id) } catch (_: Exception) { }
    }

    private fun publishState() {
        val next = state
        if (shownState != next) {
            shownState = next
            onStateChanged(next)
        }
    }

    private fun expired(start: Long?, now: Long): Boolean =
        start != null && (now < start || now - start >= LOCATION_LEASE_MS)

    private fun fresh(capturedAt: Long, now: Long): Boolean =
        capturedAt > 0 && now >= capturedAt && now - capturedAt < PHONE_FIX_FRESH_MS
}
