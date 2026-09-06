#include "opentrail/oled_time_admission.hpp"
#include <limits>

namespace opentrail::time {
namespace {
bool identity_valid(const OledTimeContext& value) {
    return value.device != 0 && value.runtime != 0 && value.owner != 0 && value.owner_generation != 0;
}
bool same_owner(const OledTimeContext& a, const OledTimeContext& b) {
    return a.device == b.device && a.runtime == b.runtime && a.owner == b.owner &&
        a.owner_generation == b.owner_generation;
}
bool ready_context(const OledTimeContext& value) {
    return identity_valid(value) && value.transport_generation != 0 &&
        value.controller != 0 && value.session_nonce != 0;
}
bool same_response(const OledTimeResponse& a, const OledTimeResponse& b) {
    return a.context == b.context && a.challenge_id == b.challenge_id &&
        a.local_second_of_day == b.local_second_of_day && a.format == b.format;
}
bool valid_sample(const OledTimeResponse& value) {
    return value.local_second_of_day < 86'400 &&
        (value.format == OledClockFormat::hour_24 || value.format == OledClockFormat::hour_12);
}
}  // namespace

bool operator==(const OledTimeContext& a, const OledTimeContext& b) {
    return same_owner(a, b) && a.transport_generation == b.transport_generation &&
        a.controller == b.controller && a.session_nonce == b.session_nonce;
}
bool operator!=(const OledTimeContext& a, const OledTimeContext& b) { return !(a == b); }

OledTimeAdmissionOwner::OledTimeAdmissionOwner(OledTimeAuthoritySource& source,
    std::uint64_t first_challenge_id) : source_(source), next_id_(first_challenge_id) {}

void OledTimeAdmissionOwner::cancel() {
    pending_ = false;
    pending_context_ = {};
    pending_id_ = 0;
    issued_ms_ = 0;
    cached_ = false;
    cached_response_ = {};
    cached_code_ = OledTimeCode::unauthorized;
}

void OledTimeAdmissionOwner::clear_clock() { clock_ = OledClock{}; }

bool OledTimeAdmissionOwner::refresh(std::uint64_t now_ms) {
    if (observed_ && now_ms < last_ms_) contained_ = true;
    observed_ = true;
    if (contained_) {
        cancel();
        clear_clock();
        return false;
    }
    last_ms_ = now_ms;
    // Expiry/rollback cannot be postponed by sending invalid external samples.
    (void)clock_.observe(now_ms);
    const auto next = source_.current();
    // Capture loss of a usable Ready session before partial/invalid provider
    // observations overwrite its context. Absence is never proof of a new link.
    if (context_known_ && authority_.phase == OledTimePhase::ready &&
        ready_context(authority_.context) &&
        (next.phase != OledTimePhase::ready || !ready_context(next.context)) &&
        block_ != Block::owner) {
        block_ = Block::session;
        blocked_context_ = authority_.context;
    }
    const bool known_phase = next.phase == OledTimePhase::disconnected ||
        next.phase == OledTimePhase::connected || next.phase == OledTimePhase::ready ||
        next.phase == OledTimePhase::revoked;
    if (!identity_valid(next.context) || !known_phase ||
        (next.phase == OledTimePhase::ready && !ready_context(next.context))) {
        cancel();
        clear_clock();
        authority_ = next;
        context_known_ = false;
        return false;
    }
    if (!context_known_ || !same_owner(authority_.context, next.context)) {
        cancel();
        clear_clock();
    } else if (authority_.context != next.context) {
        cancel();
    }
    if (context_known_ && same_owner(authority_.context, next.context) &&
        next.phase == OledTimePhase::disconnected &&
        (authority_.phase == OledTimePhase::ready || authority_.phase == OledTimePhase::connected) &&
        ready_context(authority_.context) && block_ != Block::owner) {
        // A provider-observed disconnect has the same effect as its explicit
        // lifecycle callback, including when the provider clears live link fields.
        block_ = Block::session;
        blocked_context_ = authority_.context;
    }
    authority_ = next;
    context_known_ = true;
    if (block_ != Block::none && !same_owner(blocked_context_, next.context)) block_ = Block::none;
    // A disconnected observation may clear link fields. That absence is not a
    // new admitted session and cannot remove the old link's disconnect block.
    if (block_ == Block::session && next.phase == OledTimePhase::ready &&
        ready_context(next.context) && blocked_context_ != next.context) block_ = Block::none;
    switch (next.phase) {
        case OledTimePhase::revoked:
            block_ = Block::owner;
            blocked_context_ = next.context;
            cancel();
            clear_clock();
            return false;
        case OledTimePhase::disconnected:
        case OledTimePhase::connected:
            cancel();
            return false;
        case OledTimePhase::ready:
            if (block_ != Block::none || !ready_context(next.context)) {
                cancel();
                if (block_ == Block::owner || !ready_context(next.context)) clear_clock();
                return false;
            }
            return true;
        default:
            cancel();
            clear_clock();
            return false;
    }
}

OledTimeChallenge OledTimeAdmissionOwner::issue(std::uint64_t now_ms) {
    if (!refresh(now_ms)) return {contained_ ? OledTimeCode::contained : OledTimeCode::unauthorized, 0};
    if (pending_ && now_ms - issued_ms_ >= challenge_lifetime_ms) {
        pending_ = false;
        pending_context_ = {};
        pending_id_ = 0;
    }
    if (pending_) return {OledTimeCode::busy, 0};
    if (next_id_ == 0) return {OledTimeCode::exhausted, 0};
    pending_ = true;
    pending_context_ = authority_.context;
    pending_id_ = next_id_;
    issued_ms_ = now_ms;
    next_id_ = next_id_ == std::numeric_limits<std::uint64_t>::max() ? 0 : next_id_ + 1;
    return {OledTimeCode::accepted, pending_id_};
}

OledTimeResult OledTimeAdmissionOwner::finish(const OledTimeResponse& response, OledTimeCode code) {
    pending_ = false;
    pending_context_ = {};
    pending_id_ = 0;
    issued_ms_ = 0;
    cached_ = true;
    cached_response_ = response;
    cached_code_ = code;
    return {code, false};
}

OledTimeResult OledTimeAdmissionOwner::apply(const OledTimeResponse& response, std::uint64_t now_ms) {
    if (!refresh(now_ms)) return {contained_ ? OledTimeCode::contained : OledTimeCode::unauthorized, false};
    if (response.context != authority_.context) return {OledTimeCode::wrong_context, false};
    if (cached_ && response.challenge_id == cached_response_.challenge_id) {
        if (!same_response(response, cached_response_)) return {OledTimeCode::conflicting_duplicate, false};
        return {cached_code_, true};
    }
    if (!pending_) return {OledTimeCode::no_pending, false};
    if (response.context != pending_context_) return {OledTimeCode::wrong_context, false};
    if (response.challenge_id != pending_id_) return {OledTimeCode::wrong_challenge, false};
    if (now_ms < issued_ms_ || now_ms - issued_ms_ >= challenge_lifetime_ms)
        return finish(response, OledTimeCode::expired);
    if (!valid_sample(response)) return finish(response, OledTimeCode::invalid_sample);
    // Both timestamps are the application consumption tick, never phone elapsed
    // time or an earlier callback timestamp. Authority was checked above.
    if (!clock_.synchronize(response.local_second_of_day, response.format, now_ms, now_ms, true)) {
        contained_ = true;
        cancel();
        clear_clock();
        return {OledTimeCode::contained, false};
    }
    return finish(response, OledTimeCode::accepted);
}

OledClockReading OledTimeAdmissionOwner::observe(std::uint64_t now_ms) {
    (void)refresh(now_ms);
    return contained_ ? OledClockReading{} : clock_.observe(now_ms);
}

OledTimeCode OledTimeAdmissionOwner::lifecycle(const OledTimeContext& expected,
    OledTimeLifecycle event, std::uint64_t now_ms) {
    (void)refresh(now_ms);
    if (contained_) return OledTimeCode::contained;
    if (!context_known_ || expected != authority_.context) return OledTimeCode::stale_lifecycle;
    switch (event) {
        case OledTimeLifecycle::disconnected:
            // A stale Ready source cannot resurrect this exact disconnected link.
            if (block_ != Block::owner) {
                block_ = Block::session;
                blocked_context_ = expected;
            }
            break;
        case OledTimeLifecycle::revoked:
        case OledTimeLifecycle::reset:
            block_ = Block::owner;
            blocked_context_ = expected;
            clear_clock();
            break;
        default: return OledTimeCode::stale_lifecycle;
    }
    cancel();
    return OledTimeCode::accepted;
}
}  // namespace opentrail::time
