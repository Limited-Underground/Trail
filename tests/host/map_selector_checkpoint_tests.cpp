#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"

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
    return {8U * 1024U * 1024U, 500, 3, 3};
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

MapPackageEvidence candidate() {
    return package(MapSlot::slot_b, 11);
}

MapActivationGuard active_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(), {MapSelectorState::valid, package()}) ==
           MapActivationError::none);
    return guard;
}

void activate_trial(MapActivationGuard& guard) {
    guard = active_guard();
    EXPECT(guard.stage(candidate()) == MapActivationError::none);
    EXPECT(guard.mark_selector_committed(MapSlot::slot_b, 11, 100) ==
           MapActivationError::none);
}

bool equal(
    const MapSelectorCheckpoint& left,
    const MapSelectorCheckpoint& right) {
    return left.version == right.version && left.state == right.state &&
           left.reason == right.reason &&
           left.active_slot == right.active_slot &&
           left.previous_slot == right.previous_slot &&
           left.trial_boots == right.trial_boots &&
           left.maximum_trial_boots == right.maximum_trial_boots &&
           left.required_healthy_reads == right.required_healthy_reads &&
           left.active_generation == right.active_generation &&
           left.previous_generation == right.previous_generation &&
           left.trial_deadline_ms == right.trial_deadline_ms &&
           left.maximum_package_bytes == right.maximum_package_bytes &&
           left.record_generation == right.record_generation;
}

MapSelectorCheckpoint stable_checkpoint(std::uint64_t generation = 1) {
    auto guard = active_guard();
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(generation, checkpoint) ==
           MapActivationError::none);
    return checkpoint;
}

void test_stable_round_trip_is_exact_and_deterministic() {
    const auto checkpoint = stable_checkpoint(7);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> first{};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> second{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, first.data(), first.size()).succeeded());
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, second.data(), second.size()).succeeded());
    EXPECT(first == second);
    EXPECT(first[0] == 'O' && first[1] == 'T' &&
           first[2] == 'M' && first[3] == '0');

    MapSelectorCheckpoint decoded{};
    EXPECT(decode_map_selector_checkpoint(
               first.data(), first.size(), decoded).succeeded());
    EXPECT(equal(decoded, checkpoint));

    MapActivationGuard restored{};
    EXPECT(restored.start_from_checkpoint(
               policy(), decoded, package(), {}, 1000) ==
           MapActivationError::none);
    EXPECT(restored.status().state == MapActivationState::active);
    EXPECT(restored.status().map_available);
}

void test_trial_restart_resets_volatile_health_and_keeps_prior() {
    MapActivationGuard original{};
    activate_trial(original);
    EXPECT(original.report_trial_read(true, 110) == MapActivationError::none);
    EXPECT(original.status().healthy_trial_reads == 1);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(original.export_checkpoint(8, checkpoint) ==
           MapActivationError::none);
    EXPECT(checkpoint.state == MapActivationState::trial);
    EXPECT(checkpoint.trial_boots == 1);

    MapActivationGuard restored{};
    EXPECT(restored.start_from_checkpoint(
               policy(), checkpoint, candidate(), package(), 500) ==
           MapActivationError::none);
    EXPECT(restored.status().state == MapActivationState::trial);
    EXPECT(restored.status().healthy_trial_reads == 0);
    EXPECT(restored.status().trial_started_ms == 500);
    EXPECT(restored.status().previous_slot == MapSlot::slot_a);
    EXPECT(restored.status().trial_boots == 2);
    EXPECT(!restored.status().previous_cleanup_permitted);

    MapSelectorCheckpoint second_boot{};
    EXPECT(restored.export_checkpoint(9, second_boot) ==
           MapActivationError::none);
    MapActivationGuard final_allowed_boot{};
    EXPECT(final_allowed_boot.start_from_checkpoint(
               policy(), second_boot, candidate(), package(), 600) ==
           MapActivationError::none);
    EXPECT(final_allowed_boot.status().trial_boots == 3);
    MapSelectorCheckpoint exhausted{};
    EXPECT(final_allowed_boot.export_checkpoint(10, exhausted) ==
           MapActivationError::none);
    MapActivationGuard limited{};
    EXPECT(limited.start_from_checkpoint(
               policy(), exhausted, candidate(), package(), 700) ==
           MapActivationError::trial_boot_limit_reached);
    EXPECT(limited.status().state ==
           MapActivationState::fallback_required);
    EXPECT(limited.status().reason ==
           MapActivationReason::trial_boot_limit_reached);

    EXPECT(restored.report_trial_read(true, 510) == MapActivationError::none);
    EXPECT(restored.report_trial_read(true, 520) == MapActivationError::none);
    EXPECT(restored.report_trial_read(true, 530) == MapActivationError::none);
    EXPECT(restored.status().state == MapActivationState::active);
}

void test_fallback_required_survives_restart() {
    MapActivationGuard original{};
    activate_trial(original);
    EXPECT(original.report_trial_read(false, 110) ==
           MapActivationError::trial_health_failed);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(original.export_checkpoint(9, checkpoint) ==
           MapActivationError::none);
    EXPECT(checkpoint.state == MapActivationState::fallback_required);
    EXPECT(checkpoint.reason == MapActivationReason::trial_read_failed);

    MapActivationGuard restored{};
    EXPECT(restored.start_from_checkpoint(
               policy(), checkpoint, candidate(), package(), 1000) ==
           MapActivationError::none);
    EXPECT(restored.status().state == MapActivationState::fallback_required);
    EXPECT(!restored.status().map_available);
    EXPECT(restored.status().unavailable_notice_required);
    EXPECT(restored.complete_fallback(package()) == MapActivationError::none);
    EXPECT(restored.status().active_slot == MapSlot::slot_a);
}

void test_confirmed_candidate_restart_preserves_cleanup_boundary() {
    MapActivationGuard original{};
    activate_trial(original);
    EXPECT(original.report_trial_read(true, 110) == MapActivationError::none);
    EXPECT(original.report_trial_read(true, 120) == MapActivationError::none);
    EXPECT(original.report_trial_read(true, 130) == MapActivationError::none);
    EXPECT(original.status().previous_cleanup_permitted);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(original.export_checkpoint(10, checkpoint) ==
           MapActivationError::none);
    EXPECT(checkpoint.state == MapActivationState::active);
    EXPECT(checkpoint.previous_slot == MapSlot::slot_a);

    MapActivationGuard restored{};
    EXPECT(restored.start_from_checkpoint(
               policy(), checkpoint, candidate(), package(), 1000) ==
           MapActivationError::none);
    EXPECT(restored.status().previous_cleanup_permitted);
    EXPECT(restored.mark_previous_removed(MapSlot::slot_a, 10) ==
           MapActivationError::none);

    MapActivationGuard prior_missing{};
    EXPECT(prior_missing.start_from_checkpoint(
               policy(), checkpoint, candidate(), {}, 1000) ==
           MapActivationError::none);
    EXPECT(prior_missing.status().state == MapActivationState::active);
    EXPECT(prior_missing.status().map_available);
    EXPECT(!prior_missing.status().previous_cleanup_permitted);
    EXPECT(prior_missing.status().reason ==
           MapActivationReason::fallback_unavailable);
}

void test_corruption_magic_version_and_reserved_bytes_fail() {
    const auto checkpoint = stable_checkpoint(11);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, encoded.data(), encoded.size()).succeeded());
    MapSelectorCheckpoint output = checkpoint;

    auto corrupted = encoded;
    corrupted[20] ^= 0x01U;
    EXPECT(decode_map_selector_checkpoint(
               corrupted.data(), corrupted.size(), output).error ==
           MapSelectorCheckpointError::integrity_failure);
    EXPECT(equal(output, checkpoint));

    auto bad_magic = encoded;
    bad_magic[0] = 'X';
    EXPECT(decode_map_selector_checkpoint(
               bad_magic.data(), bad_magic.size(), output).error ==
           MapSelectorCheckpointError::bad_magic);
    auto future = encoded;
    future[4] = 1;
    EXPECT(decode_map_selector_checkpoint(
               future.data(), future.size(), output).error ==
           MapSelectorCheckpointError::unsupported_version);
    auto reserved = encoded;
    reserved[53] = 1;
    EXPECT(decode_map_selector_checkpoint(
               reserved.data(), reserved.size(), output).error ==
           MapSelectorCheckpointError::noncanonical_record);
}

void test_state_and_previous_coherence_fail_closed() {
    auto checkpoint = stable_checkpoint();
    checkpoint.state = MapActivationState::trial;
    EXPECT(validate_map_selector_checkpoint(checkpoint) ==
           MapSelectorCheckpointError::invalid_checkpoint);
    checkpoint = stable_checkpoint();
    checkpoint.previous_slot = MapSlot::slot_a;
    checkpoint.previous_generation = 9;
    EXPECT(validate_map_selector_checkpoint(checkpoint) ==
           MapSelectorCheckpointError::invalid_checkpoint);
    checkpoint = stable_checkpoint();
    checkpoint.state = MapActivationState::fallback_required;
    checkpoint.previous_slot = MapSlot::slot_b;
    checkpoint.previous_generation = 9;
    checkpoint.reason = MapActivationReason::none;
    EXPECT(validate_map_selector_checkpoint(checkpoint) ==
           MapSelectorCheckpointError::invalid_checkpoint);
    checkpoint = stable_checkpoint();
    checkpoint.state = static_cast<MapActivationState>(99);
    EXPECT(validate_map_selector_checkpoint(checkpoint) ==
           MapSelectorCheckpointError::invalid_checkpoint);
}

void test_arguments_and_failed_decode_preserve_output() {
    const auto checkpoint = stable_checkpoint(12);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, nullptr, encoded.size()).error ==
           MapSelectorCheckpointError::invalid_argument);
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, encoded.data(), encoded.size() - 1).error ==
           MapSelectorCheckpointError::invalid_argument);
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, encoded.data(), encoded.size()).succeeded());

    MapSelectorCheckpoint output = checkpoint;
    EXPECT(decode_map_selector_checkpoint(
               nullptr, encoded.size(), output).error ==
           MapSelectorCheckpointError::invalid_argument);
    EXPECT(equal(output, checkpoint));
    EXPECT(decode_map_selector_checkpoint(
               encoded.data(), encoded.size() - 1, output).error ==
           MapSelectorCheckpointError::invalid_argument);
    EXPECT(equal(output, checkpoint));
}

void test_policy_and_selected_evidence_mismatch_enter_mapless() {
    const auto checkpoint = stable_checkpoint(13);
    auto changed = policy();
    changed.trial_deadline_ms += 1;
    MapActivationGuard policy_mismatch{};
    EXPECT(policy_mismatch.start_from_checkpoint(
               changed, checkpoint, package(), {}, 1) ==
           MapActivationError::checkpoint_mismatch);
    EXPECT(policy_mismatch.status().state == MapActivationState::mapless);
    EXPECT(policy_mismatch.status().reason ==
           MapActivationReason::checkpoint_policy_mismatch);

    auto wrong = package();
    wrong.generation = 99;
    MapActivationGuard evidence_mismatch{};
    EXPECT(evidence_mismatch.start_from_checkpoint(
               policy(), checkpoint, wrong, {}, 1) ==
           MapActivationError::verification_required);
    EXPECT(evidence_mismatch.status().state == MapActivationState::mapless);
}

void test_trial_restart_without_prior_fails_mapless() {
    MapActivationGuard original{};
    activate_trial(original);
    MapSelectorCheckpoint checkpoint{};
    EXPECT(original.export_checkpoint(14, checkpoint) ==
           MapActivationError::none);

    MapActivationGuard restored{};
    EXPECT(restored.start_from_checkpoint(
               policy(), checkpoint, candidate(), {}, 1000) ==
           MapActivationError::fallback_unavailable);
    EXPECT(restored.status().state == MapActivationState::mapless);
    EXPECT(restored.status().reason == MapActivationReason::fallback_unavailable);
}

void test_export_preconditions_and_generation() {
    MapActivationGuard stopped{};
    MapSelectorCheckpoint untouched{};
    untouched.record_generation = 99;
    EXPECT(stopped.export_checkpoint(1, untouched) ==
           MapActivationError::invalid_state);
    EXPECT(untouched.record_generation == 99);

    EXPECT(stopped.start(policy(), {}) == MapActivationError::none);
    EXPECT(stopped.export_checkpoint(1, untouched) ==
           MapActivationError::invalid_state);
    auto guard = active_guard();
    EXPECT(guard.export_checkpoint(0, untouched) ==
           MapActivationError::invalid_state);
    EXPECT(untouched.record_generation == 99);
}

}  // namespace

int main() {
    test_stable_round_trip_is_exact_and_deterministic();
    test_trial_restart_resets_volatile_health_and_keeps_prior();
    test_fallback_required_survives_restart();
    test_confirmed_candidate_restart_preserves_cleanup_boundary();
    test_corruption_magic_version_and_reserved_bytes_fail();
    test_state_and_previous_coherence_fail_closed();
    test_arguments_and_failed_decode_preserve_output();
    test_policy_and_selected_evidence_mismatch_enter_mapless();
    test_trial_restart_without_prior_fails_mapless();
    test_export_preconditions_and_generation();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
