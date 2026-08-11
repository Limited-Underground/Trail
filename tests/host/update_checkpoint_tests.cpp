#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/update_boot_guard.hpp"
#include "opentrail/update_checkpoint.hpp"

namespace {

using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

UpdateGuardPolicy policy(std::uint8_t maximum_boots = 3) {
    return {
        0x1234U,
        10,
        ImageSlot::slot_a,
        health_bit(TrialHealth::runtime_started) |
            health_bit(TrialHealth::watchdog_healthy) |
            health_bit(TrialHealth::configuration_loaded),
        100,
        500,
        maximum_boots,
        4U * 1024U * 1024U};
}

VerifiedUpdateCandidate candidate() {
    return {
        0x1234U,
        11,
        ImageSlot::slot_b,
        1024U * 1024U,
        true,
        true,
        true,
        true};
}

void stage_and_write(UpdateBootGuard& guard) {
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written({11, ImageSlot::slot_b, true, true}) ==
           UpdateGuardError::none);
}

bool equal(const UpdateGuardCheckpoint& left,
           const UpdateGuardCheckpoint& right) {
    return left.version == right.version &&
           left.state == right.state &&
           left.rollback_reason == right.rollback_reason &&
           left.baseline_slot == right.baseline_slot &&
           left.candidate_slot == right.candidate_slot &&
           left.trial_boots == right.trial_boots &&
           left.maximum_trial_boots == right.maximum_trial_boots &&
           left.hardware_id == right.hardware_id &&
           left.baseline_version == right.baseline_version &&
           left.candidate_version == right.candidate_version &&
           left.image_bytes == right.image_bytes &&
           left.required_health_mask == right.required_health_mask &&
           left.minimum_stable_ms == right.minimum_stable_ms &&
           left.confirmation_deadline_ms ==
               right.confirmation_deadline_ms &&
           left.maximum_image_bytes == right.maximum_image_bytes &&
           left.generation == right.generation;
}

UpdateGuardCheckpoint pending_checkpoint(std::uint64_t generation = 1) {
    UpdateBootGuard guard{};
    stage_and_write(guard);
    UpdateGuardCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(generation, checkpoint) ==
           UpdateGuardError::none);
    return checkpoint;
}

void test_pending_round_trip_is_exact_and_deterministic() {
    const auto checkpoint = pending_checkpoint(7);
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> first{};
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> second{};
    const auto first_result = encode_update_checkpoint(
        checkpoint, first.data(), first.size());
    const auto second_result = encode_update_checkpoint(
        checkpoint, second.data(), second.size());
    EXPECT(first_result.succeeded());
    EXPECT(first_result.bytes == kUpdateCheckpointRecordBytes);
    EXPECT(second_result.succeeded());
    EXPECT(first == second);
    EXPECT(first[0] == 'O' && first[1] == 'T' &&
           first[2] == 'U' && first[3] == '0');

    UpdateGuardCheckpoint decoded{};
    const auto decode = decode_update_checkpoint(
        first.data(), first.size(), decoded);
    EXPECT(decode.succeeded());
    EXPECT(equal(decoded, checkpoint));
}

void test_trial_restore_forgets_boot_local_evidence() {
    UpdateBootGuard original{};
    stage_and_write(original);
    EXPECT(original.begin_boot({41, 11, ImageSlot::slot_b, 1000}) ==
           UpdateGuardError::none);
    EXPECT(original.report_health(
               41, health_bit(TrialHealth::runtime_started), 1050) ==
           UpdateGuardError::none);
    UpdateGuardCheckpoint checkpoint{};
    EXPECT(original.export_checkpoint(8, checkpoint) ==
           UpdateGuardError::none);
    EXPECT(checkpoint.state == UpdateState::trial);
    EXPECT(checkpoint.trial_boots == 1);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    EXPECT(restored.restore_checkpoint(checkpoint) ==
           UpdateGuardError::none);
    auto status = restored.status();
    EXPECT(status.state == UpdateState::trial);
    EXPECT(status.trial_boots == 1);
    EXPECT(status.trial_boot_session_id == 0);
    EXPECT(status.trial_started_ms == 0);
    EXPECT(status.last_monotonic_ms == 0);
    EXPECT(status.observed_health_mask == 0);

    EXPECT(restored.begin_boot({42, 11, ImageSlot::slot_b, 5}) ==
           UpdateGuardError::none);
    status = restored.status();
    EXPECT(status.trial_boots == 2);
    EXPECT(status.trial_boot_session_id == 42);
    EXPECT(status.trial_started_ms == 5);
}

void test_rollback_required_survives_restart() {
    UpdateBootGuard original{};
    stage_and_write(original);
    EXPECT(original.begin_boot({1, 11, ImageSlot::slot_b, 10}) ==
           UpdateGuardError::none);
    EXPECT(original.request_rollback(
               RollbackReason::explicit_health_failure) ==
           UpdateGuardError::none);
    UpdateGuardCheckpoint checkpoint{};
    EXPECT(original.export_checkpoint(9, checkpoint) ==
           UpdateGuardError::none);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    EXPECT(restored.restore_checkpoint(checkpoint) ==
           UpdateGuardError::none);
    EXPECT(restored.status().state == UpdateState::rollback_required);
    EXPECT(restored.status().rollback_reason ==
           RollbackReason::explicit_health_failure);
    EXPECT(restored.complete_rollback({2, 10, ImageSlot::slot_a, 1}) ==
           UpdateGuardError::none);
    EXPECT(restored.status().state == UpdateState::rolled_back);
}

void test_policy_mismatch_is_atomic() {
    const auto checkpoint = pending_checkpoint(10);
    auto changed_policy = policy();
    changed_policy.confirmation_deadline_ms = 501;
    UpdateBootGuard restored{};
    EXPECT(restored.start(changed_policy) == UpdateGuardError::none);
    EXPECT(restored.restore_checkpoint(checkpoint) ==
           UpdateGuardError::checkpoint_mismatch);
    const auto status = restored.status();
    EXPECT(status.state == UpdateState::idle);
    EXPECT(status.candidate.version == 0);
    EXPECT(status.trial_boots == 0);
}

void test_export_and_restore_preconditions() {
    UpdateBootGuard guard{};
    UpdateGuardCheckpoint untouched{};
    untouched.generation = 99;
    EXPECT(guard.export_checkpoint(1, untouched) ==
           UpdateGuardError::invalid_state);
    EXPECT(untouched.generation == 99);
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.export_checkpoint(1, untouched) ==
           UpdateGuardError::invalid_state);
    EXPECT(guard.restore_checkpoint(UpdateGuardCheckpoint{}) ==
           UpdateGuardError::invalid_checkpoint);

    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.restore_checkpoint(pending_checkpoint()) ==
           UpdateGuardError::invalid_state);
}

void test_corruption_magic_version_and_reserved_bytes_fail() {
    const auto checkpoint = pending_checkpoint(11);
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> encoded{};
    EXPECT(encode_update_checkpoint(
               checkpoint, encoded.data(), encoded.size()).succeeded());
    UpdateGuardCheckpoint output = checkpoint;

    auto corrupted = encoded;
    corrupted[20] ^= 0x01U;
    EXPECT(decode_update_checkpoint(
               corrupted.data(), corrupted.size(), output).error ==
           UpdateCheckpointCodecError::integrity_failure);
    EXPECT(equal(output, checkpoint));

    auto bad_magic = encoded;
    bad_magic[0] = 'X';
    EXPECT(decode_update_checkpoint(
               bad_magic.data(), bad_magic.size(), output).error ==
           UpdateCheckpointCodecError::bad_magic);
    auto bad_version = encoded;
    bad_version[4] = 1;
    EXPECT(decode_update_checkpoint(
               bad_version.data(), bad_version.size(), output).error ==
           UpdateCheckpointCodecError::unsupported_version);
    auto noncanonical = encoded;
    noncanonical[11] = 1;
    EXPECT(decode_update_checkpoint(
               noncanonical.data(), noncanonical.size(), output).error ==
           UpdateCheckpointCodecError::noncanonical_record);
}

void test_invalid_shapes_are_rejected_before_encoding() {
    auto checkpoint = pending_checkpoint(12);
    checkpoint.generation = 0;
    EXPECT(validate_update_checkpoint(checkpoint) ==
           UpdateCheckpointCodecError::invalid_checkpoint);
    checkpoint = pending_checkpoint(12);
    checkpoint.state = UpdateState::trial;
    EXPECT(validate_update_checkpoint(checkpoint) ==
           UpdateCheckpointCodecError::invalid_checkpoint);
    checkpoint = pending_checkpoint(12);
    checkpoint.state = UpdateState::rollback_required;
    EXPECT(validate_update_checkpoint(checkpoint) ==
           UpdateCheckpointCodecError::invalid_checkpoint);
    checkpoint.rollback_reason = RollbackReason::boot_mismatch;
    EXPECT(validate_update_checkpoint(checkpoint) ==
           UpdateCheckpointCodecError::none);
    checkpoint.candidate_slot = checkpoint.baseline_slot;
    EXPECT(validate_update_checkpoint(checkpoint) ==
           UpdateCheckpointCodecError::invalid_checkpoint);
}

void test_buffer_contract_and_decode_output_preservation() {
    const auto checkpoint = pending_checkpoint(13);
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> encoded{};
    encoded.fill(0xA5U);
    EXPECT(encode_update_checkpoint(
               checkpoint, nullptr, encoded.size()).error ==
           UpdateCheckpointCodecError::invalid_argument);
    EXPECT(encode_update_checkpoint(
               checkpoint, encoded.data(), encoded.size() - 1).error ==
           UpdateCheckpointCodecError::invalid_argument);
    EXPECT(encoded[0] == 0xA5U);

    UpdateGuardCheckpoint output = checkpoint;
    output.generation = 77;
    EXPECT(decode_update_checkpoint(
               encoded.data(), encoded.size() - 1, output).error ==
           UpdateCheckpointCodecError::invalid_argument);
    EXPECT(output.generation == 77);
}

}  // namespace

int main() {
    test_pending_round_trip_is_exact_and_deterministic();
    test_trial_restore_forgets_boot_local_evidence();
    test_rollback_required_survives_restart();
    test_policy_mismatch_is_atomic();
    test_export_and_restore_preconditions();
    test_corruption_magic_version_and_reserved_bytes_fail();
    test_invalid_shapes_are_rejected_before_encoding();
    test_buffer_contract_and_decode_output_preservation();

    if (failures != 0) {
        std::cerr << failures << " update checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 update checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
