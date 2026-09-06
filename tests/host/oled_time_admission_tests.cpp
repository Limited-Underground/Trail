#include "opentrail/oled_time_admission.hpp"

#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace opentrail::time;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) { ++failures; std::cerr << "FAIL line " << line << ": " << expression << '\n'; }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

struct Authority final : OledTimeAuthoritySource {
    OledTimeAuthority value{OledTimePhase::ready, {1, 2, 3, 4, 5, 6, 7}};
    OledTimeAuthority current() noexcept override { return value; }
};
struct Harness {
    Authority source;
    OledTimeAdmissionOwner owner{source};
    OledTimeResponse response(std::uint64_t id, std::uint32_t seconds = 3600,
                              OledClockFormat format = OledClockFormat::hour_24) {
        return {source.value.context, id, seconds, format};
    }
    OledTimeResponse seed(std::uint64_t tick = 0, std::uint32_t seconds = 3600) {
        const auto challenge = owner.issue(tick);
        EXPECT(challenge.code == OledTimeCode::accepted);
        auto sample = response(challenge.id, seconds);
        EXPECT(owner.apply(sample, tick).code == OledTimeCode::accepted);
        EXPECT(owner.observe(tick).valid);
        return sample;
    }
};

void change_field(OledTimeContext& context, unsigned field) {
    switch (field) {
        case 0: ++context.device; break;
        case 1: ++context.runtime; break;
        case 2: ++context.owner; break;
        case 3: ++context.owner_generation; break;
        case 4: ++context.transport_generation; break;
        case 5: ++context.controller; break;
        case 6: ++context.session_nonce; break;
        default: break;
    }
}

void exact_context_required_without_consuming_good_pending() {
    for (unsigned field = 0; field < 7; ++field) {
        Harness h; h.seed();
        const auto challenge = h.owner.issue(10);
        auto wrong = h.response(challenge.id, 7200);
        change_field(wrong.context, field);
        EXPECT(h.owner.apply(wrong, 11).code == OledTimeCode::wrong_context);
        EXPECT(h.owner.observe(11).local_second_of_day == 3600);
        EXPECT(h.owner.apply(h.response(challenge.id, 7200), 12).code == OledTimeCode::accepted);
    }
}

void ready_and_complete_trusted_context_required() {
    for (const auto phase : {OledTimePhase::unavailable, OledTimePhase::disconnected,
                            OledTimePhase::connected, OledTimePhase::revoked}) {
        Harness h; h.source.value.phase = phase;
        EXPECT(h.owner.issue(0).code == OledTimeCode::unauthorized);
        EXPECT(!h.owner.observe(0).valid);
    }
    for (unsigned field = 0; field < 7; ++field) {
        Harness h;
        auto& c = h.source.value.context;
        switch (field) {
            case 0: c.device = 0; break;
            case 1: c.runtime = 0; break;
            case 2: c.owner = 0; break;
            case 3: c.owner_generation = 0; break;
            case 4: c.transport_generation = 0; break;
            case 5: c.controller = 0; break;
            case 6: c.session_nonce = 0; break;
        }
        EXPECT(h.owner.issue(0).code == OledTimeCode::unauthorized);
    }
}

void one_pending_and_nonwrapping_ids() {
    Harness h;
    const auto first = h.owner.issue(0);
    EXPECT(first.code == OledTimeCode::accepted && first.id != 0);
    EXPECT(h.owner.issue(1).code == OledTimeCode::busy);
    EXPECT(h.owner.apply(h.response(first.id), 2).code == OledTimeCode::accepted);
    const auto next = h.owner.issue(3);
    EXPECT(next.code == OledTimeCode::accepted && next.id > first.id);
    Authority source;
    OledTimeAdmissionOwner exhausted(source, 0);
    EXPECT(exhausted.issue(0).code == OledTimeCode::exhausted);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    OledTimeAdmissionOwner last(source, maximum);
    const auto final_id = last.issue(0);
    EXPECT(final_id.code == OledTimeCode::accepted && final_id.id == maximum);
    EXPECT(last.apply({source.value.context, maximum, 0, OledClockFormat::hour_24}, 1).code == OledTimeCode::accepted);
    EXPECT(last.issue(2).code == OledTimeCode::exhausted);
}

void wrong_or_absent_challenge_does_not_publish() {
    Harness h;
    EXPECT(h.owner.apply(h.response(123), 0).code == OledTimeCode::no_pending);
    const auto challenge = h.owner.issue(1);
    EXPECT(h.owner.apply(h.response(challenge.id + 1), 2).code == OledTimeCode::wrong_challenge);
    EXPECT(!h.owner.observe(2).valid);
    EXPECT(h.owner.apply(h.response(challenge.id), 3).code == OledTimeCode::accepted);
}

void exact_duplicate_cannot_extend_clock_expiry() {
    Harness h; const auto sample = h.seed(100);
    const auto replay = h.owner.apply(sample, 1100);
    EXPECT(replay.code == OledTimeCode::accepted && replay.duplicate);
    EXPECT(h.owner.observe(1100).local_second_of_day == 3601);
    EXPECT(h.owner.observe(100 + OledClock::max_sync_age_ms - 1).valid);
    const auto expired_replay = h.owner.apply(sample, 100 + OledClock::max_sync_age_ms);
    EXPECT(expired_replay.code == OledTimeCode::accepted && expired_replay.duplicate);
    EXPECT(!h.owner.observe(100 + OledClock::max_sync_age_ms).valid);
}

void changed_duplicate_conflicts_without_resync() {
    Harness h; const auto original = h.seed();
    auto changed = original; changed.local_second_of_day = 7000;
    EXPECT(h.owner.apply(changed, 1).code == OledTimeCode::conflicting_duplicate);
    changed = original; changed.format = OledClockFormat::hour_12;
    EXPECT(h.owner.apply(changed, 2).code == OledTimeCode::conflicting_duplicate);
    EXPECT(h.owner.observe(2).local_second_of_day == 3600);
    EXPECT(h.owner.apply(original, 3).duplicate);
}

void exact_deadlines_and_uint64_boundary() {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    for (const auto origin : {std::uint64_t{10}, maximum - 2000}) {
        for (const auto age : {std::uint64_t{1999}, std::uint64_t{2000}}) {
            Harness h; h.seed(origin - 1, 10);
            const auto challenge = h.owner.issue(origin);
            const auto result = h.owner.apply(h.response(challenge.id, 600), origin + age);
            EXPECT(result.code == (age == 1999 ? OledTimeCode::accepted : OledTimeCode::expired));
            EXPECT(h.owner.observe(origin + age).local_second_of_day == (age == 1999 ? 600U : 12U));
        }
    }
    Harness h;
    const auto first = h.owner.issue(0);
    const auto fresh = h.owner.issue(2000);
    EXPECT(fresh.code == OledTimeCode::accepted && fresh.id > first.id);
}

void invalid_samples_preserve_previous_clock_and_cache_failure() {
    for (const bool bad_format : {false, true}) {
        Harness h; h.seed();
        const auto challenge = h.owner.issue(1);
        auto bad = h.response(challenge.id);
        if (bad_format) bad.format = static_cast<OledClockFormat>(255);
        else bad.local_second_of_day = 86400;
        const auto result = h.owner.apply(bad, 2);
        EXPECT(result.code == OledTimeCode::invalid_sample && !result.duplicate);
        EXPECT(h.owner.observe(2).local_second_of_day == 3600);
        const auto replay = h.owner.apply(bad, 3);
        EXPECT(replay.code == OledTimeCode::invalid_sample && replay.duplicate);
        EXPECT(h.owner.apply(h.response(challenge.id), 4).code == OledTimeCode::conflicting_duplicate);
    }
}

void rendering_between_issue_and_consumption_is_allowed() {
    Harness h; h.seed();
    const auto challenge = h.owner.issue(100);
    EXPECT(h.owner.observe(800).valid);
    EXPECT(h.owner.observe(1200).valid);
    EXPECT(h.owner.apply(h.response(challenge.id, 7200), 1500).code == OledTimeCode::accepted);
    EXPECT(h.owner.observe(1500).local_second_of_day == 7200);
    EXPECT(h.owner.observe(2499).local_second_of_day == 7200);
    EXPECT(h.owner.observe(2500).local_second_of_day == 7201);
}

void civil_time_can_move_back_and_change_format() {
    Harness h; h.seed(0, 70000);
    const auto challenge = h.owner.issue(1);
    EXPECT(h.owner.apply(h.response(challenge.id, 0, OledClockFormat::hour_12), 2).code == OledTimeCode::accepted);
    EXPECT(std::strcmp(h.owner.observe(2).text.data(), "12:00 AM") == 0);
    const auto noon = h.owner.issue(3);
    EXPECT(h.owner.apply(h.response(noon.id, 43200, OledClockFormat::hour_12), 4).code == OledTimeCode::accepted);
    EXPECT(std::strcmp(h.owner.observe(4).text.data(), "12:00 PM") == 0);
}

void disconnect_retains_time_but_invalidates_old_work() {
    Harness h; h.seed();
    const auto pending = h.owner.issue(1);
    const auto old_context = h.source.value.context;
    EXPECT(h.owner.lifecycle(old_context, OledTimeLifecycle::disconnected, 2) == OledTimeCode::accepted);
    EXPECT(h.owner.issue(3).code == OledTimeCode::unauthorized);
    EXPECT(h.owner.observe(1000).valid);
    ++h.source.value.context.transport_generation;
    const auto fresh = h.owner.issue(1001);
    EXPECT(fresh.code == OledTimeCode::accepted && fresh.id > pending.id);
    EXPECT(h.owner.observe(1001).local_second_of_day == 3601);
    OledTimeResponse stale{old_context, pending.id, 0, OledClockFormat::hour_24};
    EXPECT(h.owner.apply(stale, 1002).code == OledTimeCode::wrong_context);
    EXPECT(h.owner.apply(h.response(fresh.id), 1003).code == OledTimeCode::accepted);
}

void revoked_owner_epoch_cannot_reopen_via_new_transport_or_disconnect() {
    for (const auto event : {OledTimeLifecycle::revoked, OledTimeLifecycle::reset}) {
        Harness h; h.seed();
        const auto old_context = h.source.value.context;
        const auto old_pending = h.owner.issue(1);
        EXPECT(h.owner.lifecycle(old_context, event, 2) == OledTimeCode::accepted);
        EXPECT(!h.owner.observe(2).valid);
        (void)h.owner.lifecycle(old_context, OledTimeLifecycle::disconnected, 3);
        ++h.source.value.context.transport_generation;
        ++h.source.value.context.controller;
        ++h.source.value.context.session_nonce;
        EXPECT(h.owner.issue(4).code == OledTimeCode::unauthorized);
        EXPECT(!h.owner.observe(4).valid);
        const auto blocked = h.source.value.context;
        h.source.value = {};
        EXPECT(!h.owner.observe(5).valid);
        h.source.value = {OledTimePhase::ready, blocked};
        EXPECT(h.owner.issue(6).code == OledTimeCode::unauthorized);
        ++h.source.value.context.owner_generation;
        const auto fresh = h.owner.issue(7);
        EXPECT(fresh.code == OledTimeCode::accepted && fresh.id > old_pending.id);
        EXPECT(h.owner.apply(h.response(fresh.id), 8).code == OledTimeCode::accepted);
    }
}

void stale_lifecycle_cannot_clobber_new_ready_session() {
    for (const auto event : {OledTimeLifecycle::disconnected, OledTimeLifecycle::revoked, OledTimeLifecycle::reset}) {
        Harness h; h.seed();
        const auto old = h.source.value.context;
        ++h.source.value.context.transport_generation;
        const auto fresh = h.owner.issue(1);
        EXPECT(h.owner.lifecycle(old, event, 2) == OledTimeCode::stale_lifecycle);
        EXPECT(h.owner.observe(2).valid);
        EXPECT(h.owner.apply(h.response(fresh.id), 3).code == OledTimeCode::accepted);
    }
}

void rollback_is_permanent_even_on_rejected_external_calls() {
    for (unsigned operation = 0; operation < 4; ++operation) {
        Harness h; const auto sample = h.seed(100);
        if (operation == 0) EXPECT(h.owner.issue(99).code == OledTimeCode::contained);
        if (operation == 1) {
            auto invalid = sample; ++invalid.context.owner;
            EXPECT(h.owner.apply(invalid, 99).code == OledTimeCode::contained);
        }
        if (operation == 2) {
            auto stale = h.source.value.context; ++stale.transport_generation;
            EXPECT(h.owner.lifecycle(stale, OledTimeLifecycle::reset, 99) == OledTimeCode::contained);
        }
        if (operation == 3) EXPECT(!h.owner.observe(99).valid);
        EXPECT(!h.owner.observe(100).valid);
        ++h.source.value.context.owner_generation;
        EXPECT(h.owner.issue(101).code == OledTimeCode::contained);
        EXPECT(h.owner.apply(h.response(99), 102).code == OledTimeCode::contained);
        EXPECT(h.owner.lifecycle(h.source.value.context, OledTimeLifecycle::reset, 103) == OledTimeCode::contained);
        EXPECT(!h.owner.observe(104).valid);
    }
}

void source_phase_changes_and_midnight_keep_truthful_clock() {
    Harness h; h.seed(0, 86399);
    EXPECT(std::strcmp(h.owner.observe(1000).text.data(), "00:00") == 0);
    h.source.value.phase = OledTimePhase::disconnected;
    EXPECT(h.owner.observe(1001).valid);
    EXPECT(h.owner.issue(1001).code == OledTimeCode::unauthorized);
    h.source.value.phase = OledTimePhase::unavailable;
    EXPECT(!h.owner.observe(1002).valid);
    EXPECT(std::strcmp(h.owner.observe(1002).text.data(), "--:--") == 0);
}

void invalid_authority_and_lifecycle_enums_cannot_grant_or_clear() {
    Harness h; h.seed();
    EXPECT(h.owner.lifecycle(h.source.value.context,
        static_cast<OledTimeLifecycle>(255), 1) != OledTimeCode::accepted);
    EXPECT(h.owner.observe(1).valid);
    h.source.value.phase = static_cast<OledTimePhase>(255);
    EXPECT(h.owner.issue(2).code == OledTimeCode::unauthorized);
}

void every_invalid_format_and_large_second_preserves_previous_clock() {
    for (unsigned format = 2; format <= 255; ++format) {
        Harness h; h.seed();
        const auto challenge = h.owner.issue(1);
        EXPECT(h.owner.apply(h.response(challenge.id, 0,
            static_cast<OledClockFormat>(format)), 2).code == OledTimeCode::invalid_sample);
        EXPECT(h.owner.observe(2).valid && h.owner.observe(2).local_second_of_day == 3600);
    }
    for (const auto second : {86400U, 86401U, std::numeric_limits<std::uint32_t>::max()}) {
        Harness h; h.seed();
        const auto challenge = h.owner.issue(1);
        EXPECT(h.owner.apply(h.response(challenge.id, second), 2).code == OledTimeCode::invalid_sample);
        EXPECT(h.owner.observe(2).valid && h.owner.observe(2).local_second_of_day == 3600);
    }
}
void disconnected_zero_link_authority_cannot_resurrect_old_session() {
    Harness h; h.seed();
    const auto old = h.source.value.context;
    EXPECT(h.owner.lifecycle(old, OledTimeLifecycle::disconnected, 1) == OledTimeCode::accepted);
    h.source.value.phase = OledTimePhase::disconnected;
    h.source.value.context.transport_generation = 0;
    h.source.value.context.controller = 0;
    h.source.value.context.session_nonce = 0;
    EXPECT(h.owner.observe(1000).valid);
    h.source.value = {OledTimePhase::ready, old};
    EXPECT(h.owner.issue(1001).code == OledTimeCode::unauthorized);
    EXPECT(h.owner.observe(1001).local_second_of_day == 3601);
    ++h.source.value.context.transport_generation;
    EXPECT(h.owner.issue(1002).code == OledTimeCode::accepted);
}

void malformed_phase_cannot_advance_revoked_owner_epoch() {
    for (const auto event : {OledTimeLifecycle::revoked, OledTimeLifecycle::reset}) {
        Harness h; h.seed();
        const auto old = h.source.value.context;
        EXPECT(h.owner.lifecycle(old, event, 1) == OledTimeCode::accepted);
        h.source.value.phase = static_cast<OledTimePhase>(255);
        ++h.source.value.context.owner;
        ++h.source.value.context.owner_generation;
        EXPECT(!h.owner.observe(2).valid);
        EXPECT(h.owner.issue(2).code == OledTimeCode::unauthorized);
        h.source.value = {OledTimePhase::ready, old};
        EXPECT(h.owner.issue(3).code == OledTimeCode::unauthorized);
        EXPECT(!h.owner.observe(3).valid);
        ++h.source.value.context.owner;
        ++h.source.value.context.owner_generation;
        h.source.value.context.transport_generation = 0;
        h.source.value.context.controller = 0;
        h.source.value.context.session_nonce = 0;
        EXPECT(!h.owner.observe(4).valid);
        EXPECT(h.owner.issue(4).code == OledTimeCode::unauthorized);
        h.source.value = {OledTimePhase::ready, old};
        EXPECT(h.owner.issue(5).code == OledTimeCode::unauthorized);
        EXPECT(!h.owner.observe(5).valid);
        ++h.source.value.context.owner_generation;
        EXPECT(h.owner.issue(6).code == OledTimeCode::accepted);
    }
}

void passive_disconnect_cannot_resurrect_old_session() {
    Harness h; h.seed();
    const auto old = h.source.value.context;
    h.source.value.phase = OledTimePhase::disconnected;
    h.source.value.context.transport_generation = 0;
    h.source.value.context.controller = 0;
    h.source.value.context.session_nonce = 0;
    EXPECT(h.owner.observe(1000).valid);
    h.source.value = {OledTimePhase::ready, old};
    EXPECT(h.owner.issue(1001).code == OledTimeCode::unauthorized);
    EXPECT(h.owner.observe(1001).local_second_of_day == 3601);
    ++h.source.value.context.transport_generation;
    const auto fresh = h.owner.issue(1002);
    EXPECT(fresh.code == OledTimeCode::accepted);
    EXPECT(h.owner.observe(1002).valid);
    EXPECT(h.owner.apply(h.response(fresh.id, 7200), 1003).code == OledTimeCode::accepted);
}
void ready_loss_preserves_session_block_through_partial_observations() {
    for (const auto phase : {OledTimePhase::connected, OledTimePhase::disconnected,
                            OledTimePhase::unavailable, OledTimePhase::ready,
                            static_cast<OledTimePhase>(255)}) {
        for (const bool intermediate_disconnect : {false, true}) {
            Harness h; h.seed();
            const auto old = h.source.value.context;
            h.source.value.phase = phase;
            h.source.value.context.transport_generation = 0;
            h.source.value.context.controller = 0;
            h.source.value.context.session_nonce = 0;
            const bool retain = phase == OledTimePhase::connected || phase == OledTimePhase::disconnected;
            EXPECT(h.owner.observe(1000).valid == retain);
            if (intermediate_disconnect) {
                h.source.value.phase = OledTimePhase::disconnected;
                EXPECT(h.owner.observe(1001).valid == retain);
            }
            h.source.value = {OledTimePhase::ready, old};
            EXPECT(h.owner.issue(1002).code == OledTimeCode::unauthorized);
            EXPECT(h.owner.observe(1002).valid == retain);
            ++h.source.value.context.transport_generation;
            const auto fresh = h.owner.issue(1003);
            EXPECT(fresh.code == OledTimeCode::accepted);
            EXPECT(h.owner.observe(1003).valid == retain);
            EXPECT(h.owner.apply(h.response(fresh.id, 7200), 1004).code == OledTimeCode::accepted);
        }
    }
}
}  // namespace

int main() {
    exact_context_required_without_consuming_good_pending();
    ready_and_complete_trusted_context_required();
    one_pending_and_nonwrapping_ids();
    wrong_or_absent_challenge_does_not_publish();
    exact_duplicate_cannot_extend_clock_expiry();
    changed_duplicate_conflicts_without_resync();
    exact_deadlines_and_uint64_boundary();
    invalid_samples_preserve_previous_clock_and_cache_failure();
    rendering_between_issue_and_consumption_is_allowed();
    civil_time_can_move_back_and_change_format();
    disconnect_retains_time_but_invalidates_old_work();
    revoked_owner_epoch_cannot_reopen_via_new_transport_or_disconnect();
    stale_lifecycle_cannot_clobber_new_ready_session();
    rollback_is_permanent_even_on_rejected_external_calls();
    source_phase_changes_and_midnight_keep_truthful_clock();
    invalid_authority_and_lifecycle_enums_cannot_grant_or_clear();
    every_invalid_format_and_large_second_preserves_previous_clock();
    disconnected_zero_link_authority_cannot_resurrect_old_session();
    malformed_phase_cannot_advance_revoked_owner_epoch();
    passive_disconnect_cannot_resurrect_old_session();
    ready_loss_preserves_session_block_through_partial_observations();
    if (failures != 0) return 1;
    std::cout << "PASS OLED time admission: 21 groups\n";
    return 0;
}
