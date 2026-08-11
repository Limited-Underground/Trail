#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"

namespace {

using namespace opentrail::maps;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

MapActivationPolicy policy() {
    return {8U * 1024U * 1024U, 500, 3};
}

MapPackageEvidence package(
    MapSlot slot = MapSlot::slot_a,
    std::uint64_t generation = 10) {
    return {
        slot,
        generation,
        1024U * 1024U,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true};
}

MapBootSelection active_boot() {
    return {MapSelectorState::valid, package()};
}

MapPackageEvidence candidate() {
    return package(MapSlot::slot_b, 11);
}

void stage_and_activate(MapActivationGuard& guard, bool with_prior = true) {
    const auto boot = with_prior
                          ? active_boot()
                          : MapBootSelection{MapSelectorState::missing, {}};
    EXPECT(guard.start(policy(), boot) == MapActivationError::none);
    EXPECT(guard.stage(with_prior ? candidate() : package(MapSlot::slot_a, 1)) ==
           MapActivationError::none);
    const auto staged = guard.status();
    EXPECT(guard.mark_selector_committed(
               staged.staged_slot, staged.staged_generation, 1000) ==
           MapActivationError::none);
}

void test_policy_and_fail_safe_boot_selection() {
    MapActivationGuard guard{};
    auto invalid = policy();
    invalid.required_healthy_reads = 0;
    EXPECT(guard.start(invalid, {}) == MapActivationError::invalid_policy);

    EXPECT(guard.start(policy(), {}) == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::mapless);
    EXPECT(guard.status().reason == MapActivationReason::no_selector);
    EXPECT(!guard.status().map_available);
    EXPECT(guard.status().unavailable_notice_required);
    guard.stop();

    EXPECT(guard.start(
               policy(), {MapSelectorState::ambiguous, package()}) ==
           MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::mapless);
    EXPECT(guard.status().reason == MapActivationReason::selector_ambiguous);
    guard.stop();

    EXPECT(guard.start(
               policy(), {MapSelectorState::unreadable, package()}) ==
           MapActivationError::none);
    EXPECT(guard.status().reason == MapActivationReason::selector_unreadable);
}

void test_valid_and_invalid_selected_package_boot() {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), active_boot()) == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::active);
    EXPECT(guard.status().active_slot == MapSlot::slot_a);
    EXPECT(guard.status().active_generation == 10);
    EXPECT(guard.status().map_available);
    guard.stop();

    auto invalid = package();
    invalid.read_only_capable = false;
    EXPECT(guard.start(
               policy(), {MapSelectorState::valid, invalid}) ==
           MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::mapless);
    EXPECT(guard.status().reason ==
           MapActivationReason::selected_package_invalid);
}

void test_stage_is_non_destructive_and_cancelable() {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), active_boot()) == MapActivationError::none);
    auto invalid = candidate();
    invalid.integrity_verified = false;
    EXPECT(guard.stage(invalid) == MapActivationError::verification_required);
    invalid = candidate();
    invalid.rights_permitted = false;
    EXPECT(guard.stage(invalid) == MapActivationError::verification_required);
    invalid = candidate();
    invalid.attribution_available = false;
    EXPECT(guard.stage(invalid) == MapActivationError::verification_required);
    invalid = candidate();
    invalid.read_only_capable = false;
    EXPECT(guard.stage(invalid) == MapActivationError::verification_required);
    invalid = candidate();
    invalid.package_bytes = policy().maximum_package_bytes + 1;
    EXPECT(guard.stage(invalid) == MapActivationError::verification_required);
    invalid = candidate();
    invalid.slot = MapSlot::slot_a;
    EXPECT(guard.stage(invalid) == MapActivationError::invalid_package);
    EXPECT(guard.status().state == MapActivationState::active);

    EXPECT(guard.stage(candidate()) == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::staged);
    EXPECT(guard.status().active_slot == MapSlot::slot_a);
    EXPECT(guard.status().map_available);
    EXPECT(guard.cancel_staged() == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::active);
    EXPECT(guard.status().staged_slot == MapSlot::none);
}

void test_selector_commit_must_match_and_retains_prior() {
    MapActivationGuard guard{};
    EXPECT(guard.start(policy(), active_boot()) == MapActivationError::none);
    EXPECT(guard.stage(candidate()) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(MapSlot::slot_b, 99, 1000) ==
           MapActivationError::selector_mismatch);
    EXPECT(guard.status().state == MapActivationState::staged);
    EXPECT(guard.status().active_slot == MapSlot::slot_a);

    EXPECT(guard.mark_selector_committed(MapSlot::slot_b, 11, 1000) ==
           MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::trial);
    EXPECT(guard.status().active_slot == MapSlot::slot_b);
    EXPECT(guard.status().previous_slot == MapSlot::slot_a);
    EXPECT(!guard.status().previous_cleanup_permitted);
}

void test_bounded_trial_promotes_before_cleanup() {
    MapActivationGuard guard{};
    stage_and_activate(guard);
    EXPECT(guard.report_trial_read(true, 1010) == MapActivationError::none);
    EXPECT(guard.report_trial_read(true, 1020) == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::trial);
    EXPECT(!guard.status().previous_cleanup_permitted);
    EXPECT(guard.report_trial_read(true, 1030) == MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::active);
    EXPECT(guard.status().previous_cleanup_permitted);
    EXPECT(guard.mark_previous_removed(MapSlot::slot_a, 99) ==
           MapActivationError::invalid_package);
    EXPECT(guard.mark_previous_removed(MapSlot::slot_a, 10) ==
           MapActivationError::none);
    EXPECT(guard.status().previous_slot == MapSlot::none);
}

void test_trial_failure_requires_explicit_verified_fallback() {
    MapActivationGuard guard{};
    stage_and_activate(guard);
    EXPECT(guard.report_trial_read(false, 1010) ==
           MapActivationError::trial_health_failed);
    EXPECT(guard.status().state == MapActivationState::fallback_required);
    EXPECT(!guard.status().map_available);
    EXPECT(guard.status().unavailable_notice_required);

    auto wrong = package(MapSlot::slot_a, 99);
    EXPECT(guard.complete_fallback(wrong) ==
           MapActivationError::fallback_unavailable);
    EXPECT(guard.status().state == MapActivationState::mapless);

    MapActivationGuard recovered{};
    stage_and_activate(recovered);
    EXPECT(recovered.report_trial_read(false, 1010) ==
           MapActivationError::trial_health_failed);
    EXPECT(recovered.complete_fallback(package()) == MapActivationError::none);
    EXPECT(recovered.status().state == MapActivationState::active);
    EXPECT(recovered.status().active_slot == MapSlot::slot_a);
    EXPECT(recovered.status().reason == MapActivationReason::trial_read_failed);
    EXPECT(recovered.status().map_available);
}

void test_trial_failure_without_prior_enters_mapless() {
    MapActivationGuard guard{};
    stage_and_activate(guard, false);
    EXPECT(guard.report_trial_read(false, 1010) ==
           MapActivationError::trial_health_failed);
    EXPECT(guard.status().state == MapActivationState::mapless);
    EXPECT(guard.status().active_slot == MapSlot::none);
    EXPECT(guard.status().reason == MapActivationReason::trial_read_failed);
}

void test_deadline_and_clock_regression_fail_closed() {
    MapActivationGuard deadline{};
    stage_and_activate(deadline);
    EXPECT(deadline.tick(1499) == MapActivationError::none);
    EXPECT(deadline.tick(1500) ==
           MapActivationError::trial_deadline_reached);
    EXPECT(deadline.status().state == MapActivationState::fallback_required);
    EXPECT(deadline.status().reason ==
           MapActivationReason::trial_deadline_reached);

    MapActivationGuard clock{};
    stage_and_activate(clock);
    EXPECT(clock.report_trial_read(true, 1010) == MapActivationError::none);
    EXPECT(clock.tick(1009) == MapActivationError::clock_regression);
    EXPECT(clock.status().state == MapActivationState::fallback_required);
    EXPECT(clock.status().reason == MapActivationReason::clock_regression);
}

void test_media_removal_degrades_only_map_authority() {
    MapActivationGuard staged{};
    EXPECT(staged.start(policy(), active_boot()) == MapActivationError::none);
    EXPECT(staged.stage(candidate()) == MapActivationError::none);
    EXPECT(staged.report_media_removed(MapSlot::slot_b) ==
           MapActivationError::none);
    EXPECT(staged.status().state == MapActivationState::active);
    EXPECT(staged.status().active_slot == MapSlot::slot_a);
    EXPECT(staged.status().map_available);
    EXPECT(staged.status().reason == MapActivationReason::candidate_removed);

    MapActivationGuard trial{};
    stage_and_activate(trial);
    EXPECT(trial.report_media_removed(MapSlot::slot_b) ==
           MapActivationError::none);
    EXPECT(trial.status().state == MapActivationState::fallback_required);

    MapActivationGuard lost_fallback{};
    stage_and_activate(lost_fallback);
    EXPECT(lost_fallback.report_trial_read(false, 1010) ==
           MapActivationError::trial_health_failed);
    EXPECT(lost_fallback.report_media_removed(MapSlot::slot_a) ==
           MapActivationError::fallback_unavailable);
    EXPECT(lost_fallback.status().state == MapActivationState::mapless);

    MapActivationGuard active{};
    EXPECT(active.start(policy(), active_boot()) == MapActivationError::none);
    EXPECT(active.report_media_removed(MapSlot::slot_a) ==
           MapActivationError::none);
    EXPECT(active.status().state == MapActivationState::mapless);
    EXPECT(active.status().reason ==
           MapActivationReason::active_media_removed);
    EXPECT(active.status().unavailable_notice_required);
}

void test_removed_prior_cannot_be_guessed_as_fallback() {
    MapActivationGuard guard{};
    stage_and_activate(guard);
    EXPECT(guard.report_media_removed(MapSlot::slot_a) ==
           MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::trial);
    EXPECT(guard.status().previous_slot == MapSlot::none);
    EXPECT(guard.report_trial_read(false, 1010) ==
           MapActivationError::trial_health_failed);
    EXPECT(guard.status().state == MapActivationState::mapless);
    EXPECT(guard.complete_fallback(package()) ==
           MapActivationError::invalid_state);
}

}  // namespace

int main() {
    test_policy_and_fail_safe_boot_selection();
    test_valid_and_invalid_selected_package_boot();
    test_stage_is_non_destructive_and_cancelable();
    test_selector_commit_must_match_and_retains_prior();
    test_bounded_trial_promotes_before_cleanup();
    test_trial_failure_requires_explicit_verified_fallback();
    test_trial_failure_without_prior_enters_mapless();
    test_deadline_and_clock_regression_fail_closed();
    test_media_removal_degrades_only_map_authority();
    test_removed_prior_cannot_be_guessed_as_fallback();

    if (failures != 0) {
        std::cerr << failures
                  << " map activation guard assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map activation guard scenario groups\n";
    return EXIT_SUCCESS;
}
