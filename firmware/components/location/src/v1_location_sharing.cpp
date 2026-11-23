#include "opentrail/v1_location_sharing.hpp"

#include <array>
#include <limits>

namespace opentrail::location::v1 {
namespace {

bool valid_audience(const Audience& audience) {
    if (!valid_scope(audience.scope) || audience.generation == 0) return false;
    if (audience.scope.kind == ScopeKind::direct) return audience.rule == GroupRule::off;
    return audience.rule == GroupRule::off || audience.rule == GroupRule::preferred ||
           audience.rule == GroupRule::required;
}

bool sharing_allowed(const Audience& audience) {
    return valid_audience(audience) &&
           (audience.scope.kind == ScopeKind::direct || audience.rule != GroupRule::off);
}

bool eligible_route(const Route& route, const RouteReadback& current, std::uint64_t now) {
    if (!(route == current.route) || !sharing_allowed(current.route.audience) ||
        route.pair == 0 || route.epoch == 0) return false;
    if (route.audience.scope.kind == ScopeKind::direct) return true;
    return current.last_contact && now >= *current.last_contact &&
           now - *current.last_contact < kCurrentRosterContactMs;
}

bool fresh_fix(const PhoneFix& fix) {
    return fix.sample != 0 && fix.age_ms < kFreshFixMs && valid_coordinates(fix.position);
}

}  // namespace

bool Audience::operator==(const Audience& other) const {
    return scope == other.scope && generation == other.generation && rule == other.rule;
}

bool Route::operator==(const Route& other) const {
    return audience == other.audience && pair == other.pair && epoch == other.epoch;
}

bool LeaseId::operator==(const LeaseId& other) const {
    return attachment == other.attachment && serial == other.serial;
}

bool allows_group_join(const GroupJoin& current, const GroupJoin& accepted, JoinChoice choice,
                       std::optional<std::uint32_t> fix_age_ms) {
    if (!valid_audience(current.offer) || current.offer.scope.kind != ScopeKind::group ||
        current.attempt == 0 || current.attempt != accepted.attempt ||
        !(current.offer == accepted.offer)) return false;
    switch (current.offer.rule) {
        case GroupRule::off: return choice == JoinChoice::join;
        case GroupRule::preferred:
            return choice == JoinChoice::share_and_join || choice == JoinChoice::join_without_sharing;
        case GroupRule::required:
            return choice == JoinChoice::share_and_join && fix_age_ms && *fix_age_ms < kFreshFixMs;
    }
    return false;
}

void SharingLease::set_audience(const Audience& current) {
    if (valid_audience(current) && audience_ == current) return;
    clear_fix();
    consent_ = false;
    audience_ = valid_audience(current) ? current : Audience{};
}

bool SharingLease::consent(const Audience& current) {
    if (!(audience_ == current) || !sharing_allowed(current)) return false;
    consent_ = true;
    return true;
}

void SharingLease::withdraw(const Audience& current) {
    if (!(audience_ == current)) return;
    consent_ = false;
    clear_fix();
}

bool SharingLease::participation_paused(const Audience& current) const {
    return valid_audience(current) && current.rule == GroupRule::required &&
           (!(audience_ == current) || !consent_);
}

std::optional<LeaseId> SharingLease::start(const Audience& current, std::uint64_t attachment,
                                           const PhoneFix& fix, std::uint64_t now) {
    if (!(audience_ == current) || !consent_ || attachment == 0 || !fresh_fix(fix) ||
        this->current(lease_, current, now) || serial_ == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    lease_ = {attachment, ++serial_};
    renewed_at_ = admitted_at_ = now;
    fix_ = fix;
    return lease_;
}

bool SharingLease::update_fix(const Audience& audience, LeaseId id, const PhoneFix& fix, std::uint64_t now) {
    if (!current(id, audience, now) || !fresh_fix(fix) || fix.sample < fix_.sample) return false;
    if (fix.sample == fix_.sample) {
        // A repeat cannot alter coordinates or make the original fix younger.
        return fix.position.latitude_e4 == fix_.position.latitude_e4 &&
               fix.position.longitude_e4 == fix_.position.longitude_e4 && fix.age_ms >= fix_age(now);
    }
    fix_ = fix;
    admitted_at_ = now;
    return true;
}

bool SharingLease::renew(const Audience& audience, LeaseId id, std::uint64_t now) {
    if (!current(id, audience, now)) return false;
    renewed_at_ = now;
    return true;
}

void SharingLease::stop(LeaseId id) {
    if (lease_ == id) clear_fix();
}

void SharingLease::detach(std::uint64_t attachment) {
    if (lease_.attachment == attachment) clear_fix();
}

LeaseId SharingLease::active(const Audience& audience, std::uint64_t now) {
    return current(lease_, audience, now) ? lease_ : LeaseId{};
}

std::optional<Frame> SharingLease::position(const Audience& audience, std::uint64_t now) {
    if (!current(lease_, audience, now)) return std::nullopt;
    return Frame{audience.scope, Event::position, fix_.position, static_cast<std::uint32_t>(fix_age(now))};
}

void SharingLease::clear_fix() {
    lease_ = {};
    fix_ = {};
    renewed_at_ = admitted_at_ = 0;
}

bool SharingLease::current(LeaseId id, const Audience& audience, std::uint64_t now) {
    if (lease_.serial == 0 || !(lease_ == id) || !(audience_ == audience) || !consent_) return false;
    if (now < renewed_at_ || now < admitted_at_ || now - renewed_at_ >= kLeaseMs ||
        now - admitted_at_ >= kFreshFixMs - fix_.age_ms) {
        clear_fix();
        return false;
    }
    return true;
}

std::uint64_t SharingLease::fix_age(std::uint64_t now) const {
    return fix_.age_ms + (now - admitted_at_);
}

bool LocationSender::launch(SharingLease& lease, const RouteReadback& current, bool radio_budget,
                            std::uint64_t now, PositionRadio& radio) {
    if (!radio_budget || !eligible_route(route_, current, now)) return false;
    const auto active = lease.active(route_.audience, now);
    const bool stop_due = sent_under_.serial != 0 && !(sent_under_ == active);
    if (!stop_due && (active.serial == 0 ||
        (last_position_at_ && (now < *last_position_at_ || now - *last_position_at_ < kPositionIntervalMs)))) {
        return false;
    }

    const auto frame = stop_due ? std::optional<Frame>{Frame{route_.audience.scope, Event::stop, {}, 0}}
                                : lease.position(route_.audience, now);
    if (!frame) return false;
    // The largest plaintext is a group position. The encoder checks exact length.
    std::array<std::uint8_t, kMaximumLocationFrameBytes> bytes{};
    const auto size = encode_frame(*frame, bytes.data(), bytes.size());
    if (size == 0 || !radio.launch(route_, bytes.data(), size)) return false;
    sent_under_ = stop_due ? LeaseId{} : active;
    if (!stop_due) last_position_at_ = now;
    return true;
}

bool LocationReceiver::receive(const RouteReadback& current, const ReceiveAdmission& admission,
                               const std::uint8_t* plaintext, std::size_t size, std::uint64_t now) {
    if (!eligible_route(route_, current, now)) {
        pin_.reset();
        return false;
    }
    Frame frame{};
    if (admission.admitted_counter <= newest_counter_ || !decode_frame(plaintext, size, frame) ||
        !(frame.scope == route_.audience.scope)) return false;
    if (frame.event == Event::stop) {
        newest_counter_ = admission.admitted_counter;
        pin_.reset();
        return true;
    }
    if (!admission.additional_age_ms || *admission.additional_age_ms >= kStalePinMs - frame.fix_age_ms) {
        return false;
    }
    newest_counter_ = admission.admitted_counter;
    received_at_ = now;
    pin_ = LivePin{frame.position, PinState::current, frame.fix_age_ms + *admission.additional_age_ms};
    return true;
}

std::optional<LivePin> LocationReceiver::pin(const RouteReadback& current, std::uint64_t now) {
    if (!pin_) return std::nullopt;
    if (!eligible_route(route_, current, now) || now < received_at_ ||
        now - received_at_ >= kRemovePinMs - pin_->age_ms) {
        pin_.reset();
        return std::nullopt;
    }
    auto displayed = *pin_;
    displayed.age_ms += static_cast<std::uint32_t>(now - received_at_);
    if (displayed.age_ms >= kStalePinMs) displayed.state = PinState::stale;
    return displayed;
}

}  // namespace opentrail::location::v1
