#include "opentrail/v1_location_sharing.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace opentrail::location::v1;

namespace {

Audience group(GroupRule rule, std::uint32_t version = 1) {
    Scope scope{ScopeKind::group, 7, version, {}};
    scope.roster_digest[0] = static_cast<std::uint8_t>(version);
    return {scope, version, rule};
}

Audience direct() {
    return {{ScopeKind::direct, 8, 0, {}}, 1, GroupRule::off};
}

PhoneFix fix(std::uint64_t sample = 1, std::uint32_t age = 0) {
    return {{123456, -345678}, sample, age};
}

void group_choices_are_bound_to_the_offer() {
    const GroupJoin off{group(GroupRule::off), 11};
    const GroupJoin preferred{group(GroupRule::preferred), 12};
    const GroupJoin required{group(GroupRule::required), 13};
    assert(allows_group_join(off, off, JoinChoice::join, std::nullopt));
    assert(!allows_group_join(off, off, JoinChoice::share_and_join, 0));
    assert(allows_group_join(preferred, preferred, JoinChoice::share_and_join, std::nullopt));
    assert(allows_group_join(preferred, preferred, JoinChoice::join_without_sharing, std::nullopt));
    assert(!allows_group_join(required, required, JoinChoice::join_without_sharing, 0));
    assert(!allows_group_join(required, required, JoinChoice::cancel, 0));
    assert(!allows_group_join(required, required, JoinChoice::share_and_join, std::nullopt));
    assert(!allows_group_join(required, required, JoinChoice::share_and_join, kFreshFixMs));
    assert(allows_group_join(required, required, JoinChoice::share_and_join, kFreshFixMs - 1));
    assert(!allows_group_join(required, {required.offer, 12}, JoinChoice::share_and_join, 0));
    assert(!allows_group_join(required, {group(GroupRule::required, 2), 13}, JoinChoice::share_and_join, 0));
}

void consent_is_exact_and_survives_only_temporary_pauses() {
    SharingLease sender;
    SharingLease recipient;
    const auto pending = Audience{{ScopeKind::direct, 8, 0, {}}, 0, GroupRule::off};
    sender.set_audience(pending);
    assert(!sender.consent(pending));
    assert(!sender.start(pending, 1, fix(), 0));
    const auto conversation = direct();
    const bool choices[]{false, true};
    for (bool share : choices) {
        for (bool share_back : choices) {
            SharingLease inviter, invitee;
            inviter.set_audience(conversation);
            invitee.set_audience(conversation);
            if (share) assert(inviter.consent(conversation));
            if (share_back) assert(invitee.consent(conversation));
            assert(inviter.start(conversation, 1, fix(), 0).has_value() == share);
            assert(invitee.start(conversation, 2, fix(), 0).has_value() == share_back);
            inviter.withdraw(conversation);
            assert(!inviter.position(conversation, 1));
            assert(invitee.position(conversation, 1).has_value() == share_back);
        }
    }

    const auto required = group(GroupRule::required);
    sender.set_audience(required);
    assert(sender.participation_paused(required));
    assert(sender.consent(required));
    const auto lease = sender.start(required, 1, fix(), 0);
    assert(lease && !sender.participation_paused(required));
    sender.detach(1);
    assert(!sender.position(required, 1) && !sender.participation_paused(required));
    const auto resumed = sender.start(required, 2, fix(), 2);
    assert(resumed);
    sender.stop(*lease);
    assert(sender.position(required, 2));
    sender.withdraw(required);
    assert(sender.participation_paused(required));
    assert(!sender.position(required, 3));
    assert(sender.consent(required));
    auto changed = group(GroupRule::required, 2);
    sender.set_audience(changed);
    assert(sender.participation_paused(changed));
    assert(!sender.start(changed, 2, fix(), 4));
    sender.set_audience(group(GroupRule::off, 3));
    assert(!sender.consent(group(GroupRule::off, 3)));
}

void renewal_keeps_the_fix_age_and_expires_at_the_boundary() {
    SharingLease lease;
    const auto audience = direct();
    lease.set_audience(audience);
    assert(lease.consent(audience));
    assert(!lease.start(audience, 2, fix(1, kFreshFixMs), 5));
    const auto id = lease.start(audience, 2, fix(1, 5000), 5);
    assert(id);
    assert(!lease.renew(audience, {3, id->serial}, 1005));
    assert(lease.renew(audience, *id, 5005));
    assert(!lease.update_fix(audience, *id, fix(1), 5005));
    assert(lease.position(audience, 10004)->fix_age_ms == 14999);
    assert(!lease.renew(audience, *id, 10005));
    assert(!lease.position(audience, 10005));
    const auto next = lease.start(audience, 2, fix(2), 10005);
    assert(next && !(*id == *next));
    assert(lease.update_fix(audience, *next, fix(3), 20005));
    assert(lease.position(audience, 25004));
    assert(!lease.position(audience, 25005));  // Fresh fix cannot outlive its lease.
    assert(!lease.renew(audience, *next, 25005));
}

class Radio final : public PositionRadio {
public:
    bool launch(const Route&, const std::uint8_t* bytes, std::size_t size) override {
        assert(decode_frame(bytes, size, latest));
        ++attempts;
        return available;
    }

    Frame latest{};
    int attempts{};
    bool available{true};
};

void launch_checks_current_recipient_cadence_and_stops_first() {
    const auto audience = group(GroupRule::preferred);
    const Route route{audience, 9, 2};
    RouteReadback current{route, 0};
    LocationSender sender{route};
    SharingLease lease;
    Radio radio;
    lease.set_audience(audience);
    assert(!sender.launch(lease, current, true, 0, radio));
    assert(lease.consent(audience));
    const auto id = lease.start(audience, 1, fix(), 0);
    assert(id);
    assert(!sender.launch(lease, {{audience, 8, 2}, 0}, true, 0, radio));
    assert(!sender.launch(lease, {{audience, 9, 3}, 0}, true, 0, radio));
    assert(!sender.launch(lease, {route, std::nullopt}, true, 0, radio));
    assert(!sender.launch(lease, current, false, 0, radio));
    assert(radio.attempts == 0);
    assert(sender.launch(lease, current, true, 0, radio));
    assert(radio.latest.event == Event::position);
    assert(!sender.launch(lease, current, true, 1, radio));
    lease.stop(*id);
    const auto resumed = lease.start(audience, 1, fix(2), 2);
    assert(resumed);
    radio.available = false;
    assert(!sender.launch(lease, current, true, 2, radio));
    assert(radio.latest.event == Event::stop && radio.latest.fix_age_ms == 0);
    radio.available = true;
    assert(sender.launch(lease, current, true, 3, radio));
    assert(radio.latest.event == Event::stop);
    assert(!sender.launch(lease, current, true, 4, radio));
    lease.stop(*resumed);
    const auto later = lease.start(audience, 1, fix(3), kPositionIntervalMs - 1);
    assert(later);
    assert(!sender.launch(lease, current, true, kPositionIntervalMs - 1, radio));
    assert(sender.launch(lease, current, true, kPositionIntervalMs, radio));
    LocationSender peer{route};
    assert(!peer.launch(lease, current, true, kCurrentRosterContactMs, radio));
    assert(!peer.launch(lease, {{group(GroupRule::off, 2), 9, 2}, 0}, true, kPositionIntervalMs, radio));
}

void pins_require_authenticated_replay_and_freshness_admission() {
    const auto audience = direct();
    const Route route{audience, 9, 2};
    const RouteReadback current{route, std::nullopt};
    LocationReceiver receiver{route};
    std::array<std::uint8_t, kMaximumLocationFrameBytes> bytes{};
    auto size = encode_frame({audience.scope, Event::position, fix().position, 1000},
                             bytes.data(), bytes.size());
    assert(!receiver.receive(current, {}, bytes.data(), size, 2000));
    assert(!receiver.receive(current, {1, std::nullopt}, bytes.data(), size, 2000));
    assert(!receiver.receive(current, {1, 149000}, bytes.data(), size, 2000));
    assert(receiver.receive(current, {1, 500}, bytes.data(), size, 2000));
    assert(receiver.pin(current, 150499)->state == PinState::current);
    assert(receiver.pin(current, 150500)->state == PinState::stale);
    assert(receiver.pin(current, 240499));
    assert(!receiver.pin(current, 240500));
    assert(!receiver.receive(current, {1, 500}, bytes.data(), size, 240500));
    assert(receiver.receive(current, {2, 0}, bytes.data(), size, 240501));
    std::array<std::uint8_t, kMaximumLocationFrameBytes> stop{};
    const auto stop_size = encode_frame({audience.scope, Event::stop, {}, 0}, stop.data(), stop.size());
    assert(receiver.receive(current, {4, std::nullopt}, stop.data(), stop_size, 240502));
    assert(!receiver.pin(current, 240502));
    assert(!receiver.receive(current, {3, 0}, bytes.data(), size, 240503));
    assert(receiver.receive(current, {5, 0}, bytes.data(), size, 240504));
    assert(!receiver.pin({{audience, 9, 3}, {}}, 240504));
    assert(!receiver.pin(current, 240504));
}

}  // namespace

int main() {
    group_choices_are_bound_to_the_offer();
    consent_is_exact_and_survives_only_temporary_pauses();
    renewal_keeps_the_fix_age_and_expires_at_the_boundary();
    launch_checks_current_recipient_cadence_and_stops_first();
    pins_require_authenticated_replay_and_freshness_admission();
    std::puts("5 location sharing test groups passed");
}
