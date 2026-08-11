#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "opentrail/update_recovery_status.hpp"

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

template <typename T, typename = void>
struct has_hardware_id : std::false_type {};
template <typename T>
struct has_hardware_id<
    T,
    std::void_t<decltype(std::declval<T>().hardware_id)>> : std::true_type {};

template <typename T, typename = void>
struct has_candidate : std::false_type {};
template <typename T>
struct has_candidate<
    T,
    std::void_t<decltype(std::declval<T>().candidate)>> : std::true_type {};

template <typename T, typename = void>
struct has_checkpoint : std::false_type {};
template <typename T>
struct has_checkpoint<
    T,
    std::void_t<decltype(std::declval<T>().checkpoint)>> : std::true_type {};

template <typename T, typename = void>
struct has_trusted_error : std::false_type {};
template <typename T>
struct has_trusted_error<
    T,
    std::void_t<decltype(std::declval<T>().trusted_error)>> : std::true_type {};

template <typename T, typename = void>
struct has_guard_error : std::false_type {};
template <typename T>
struct has_guard_error<
    T,
    std::void_t<decltype(std::declval<T>().guard_error)>> : std::true_type {};

template <typename T, typename = void>
struct has_persistence : std::false_type {};
template <typename T>
struct has_persistence<
    T,
    std::void_t<decltype(std::declval<T>().persistence)>> : std::true_type {};

template <typename T, typename = void>
struct has_policy : std::false_type {};
template <typename T>
struct has_policy<
    T,
    std::void_t<decltype(std::declval<T>().policy)>> : std::true_type {};

template <typename T, typename = void>
struct has_key_handle : std::false_type {};
template <typename T>
struct has_key_handle<
    T,
    std::void_t<decltype(std::declval<T>().key_handle)>> : std::true_type {};

template <typename T, typename = void>
struct has_address : std::false_type {};
template <typename T>
struct has_address<
    T,
    std::void_t<decltype(std::declval<T>().address)>> : std::true_type {};

static_assert(!has_hardware_id<UpdateRecoveryStatus>::value);
static_assert(!has_candidate<UpdateRecoveryStatus>::value);
static_assert(!has_checkpoint<UpdateRecoveryStatus>::value);
static_assert(!has_trusted_error<UpdateRecoveryStatus>::value);
static_assert(!has_guard_error<UpdateRecoveryStatus>::value);
static_assert(!has_persistence<UpdateRecoveryStatus>::value);
static_assert(!has_policy<UpdateRecoveryStatus>::value);
static_assert(!has_key_handle<UpdateRecoveryStatus>::value);
static_assert(!has_address<UpdateRecoveryStatus>::value);
static_assert(std::is_trivially_copyable_v<UpdateRecoveryStatus>);
static_assert(sizeof(UpdateRecoveryStatus) <= 32);

UpdatePersistenceResult committed_persistence(
    std::uint64_t prior_generation = 4) {
    UpdatePersistenceResult result{};
    result.state = UpdatePersistenceState::committed;
    result.reason = UpdatePersistenceReason::none;
    result.trusted_error = UpdateTrustedGenerationError::none;
    result.inspection.error = UpdateCheckpointStoreError::none;
    result.inspection.source = UpdateCheckpointSource::slot_a;
    result.inspection.slot_a = UpdateCheckpointSlotState::valid;
    result.inspection.generation = prior_generation;
    result.inspection.checkpoint_available = true;
    result.save.error = UpdateCheckpointStoreError::none;
    result.save.written_slot = UpdateCheckpointSource::slot_b;
    result.save.slot_a = UpdateCheckpointSlotState::valid;
    result.save.slot_b = UpdateCheckpointSlotState::valid;
    result.save.generation = prior_generation + 1;
    result.prior_trusted_generation = prior_generation;
    result.observed_trusted_readback = prior_generation + 1;
    result.committed_generation = prior_generation + 1;
    return result;
}

UpdatePersistenceResult failed_persistence(
    UpdatePersistenceState state,
    UpdatePersistenceReason reason) {
    UpdatePersistenceResult result{};
    result.state = state;
    result.reason = reason;
    result.inspection.error = UpdateCheckpointStoreError::none;
    result.inspection.source = UpdateCheckpointSource::slot_a;
    result.inspection.slot_a = UpdateCheckpointSlotState::valid;
    result.inspection.generation = 4;
    result.inspection.checkpoint_available = true;
    result.prior_trusted_generation = 4;
    if (reason == UpdatePersistenceReason::commit_uncertain) {
        result.save.error = UpdateCheckpointStoreError::storage_failure;
        result.save.generation = 5;
        result.save.commit_uncertain = true;
        result.committed_generation = 5;
    }
    return result;
}

UpdateTransitionResult durable_transition(
    UpdateTransitionOperation operation,
    UpdateGuardError guard_error,
    UpdateState before,
    UpdateState attempted,
    const UpdatePersistenceResult& persistence) {
    UpdateTransitionResult result{};
    result.operation = operation;
    result.guard_error = guard_error;
    result.persistence = persistence;
    result.before_state = before;
    result.attempted_state = attempted;
    result.persistence_required = true;
    if (persistence.state == UpdatePersistenceState::committed) {
        result.state = UpdateTransitionState::committed;
        result.live_state = attempted;
    } else {
        result.live_state = before;
        result.live_guard_stopped = true;
        switch (persistence.state) {
            case UpdatePersistenceState::reboot_reconcile_required:
                result.state =
                    UpdateTransitionState::reboot_reconcile_required;
                break;
            case UpdatePersistenceState::safe_mode:
                result.state = UpdateTransitionState::safe_mode;
                break;
            case UpdatePersistenceState::service_required:
                result.state = UpdateTransitionState::service_required;
                break;
            case UpdatePersistenceState::committed:
                break;
        }
    }
    return result;
}

void test_boot_baseline_and_trial_are_actionable() {
    UpdateRecoveryBootResult baseline{};
    baseline.state = UpdateRecoveryBootState::baseline_ready;
    baseline.reason = UpdateRecoveryBootReason::clean_baseline;
    baseline.application_allowed = true;
    const auto ready = make_update_recovery_status(baseline);
    EXPECT(ready.operation == UpdateRecoveryStatusOperation::boot);
    EXPECT(ready.state == UpdateRecoveryOperatorState::operational);
    EXPECT(ready.reason == UpdateRecoveryOperatorReason::clean_baseline);
    EXPECT(ready.action ==
           UpdateRecoveryOperatorAction::continue_operation);
    EXPECT(ready.operation_succeeded);
    EXPECT(!ready.normal_operation_blocked);
    EXPECT(!ready.attention_required);

    UpdateRecoveryBootResult trial{};
    trial.state = UpdateRecoveryBootState::trial_ready;
    trial.reason = UpdateRecoveryBootReason::none;
    trial.application_allowed = true;
    trial.confirmation_required = true;
    trial.active_generation = 7;
    trial.trusted_generation = 7;
    const auto active = make_update_recovery_status(trial);
    EXPECT(active.state == UpdateRecoveryOperatorState::trial_active);
    EXPECT(active.reason ==
           UpdateRecoveryOperatorReason::trial_confirmation_required);
    EXPECT(active.action == UpdateRecoveryOperatorAction::continue_trial);
    EXPECT(active.confirmation_required);
    EXPECT(active.observed_generation == 7);
    EXPECT(active.trusted_generation == 7);
}

void test_boot_rollback_and_cleanup_actions_are_distinct() {
    UpdateRecoveryBootResult rollback{};
    rollback.state = UpdateRecoveryBootState::rollback_required;
    rollback.reason = UpdateRecoveryBootReason::boot_mismatch;
    rollback.reboot_to_baseline_required = true;
    rollback.active_generation = 8;
    rollback.trusted_generation = 8;
    const auto rollback_status = make_update_recovery_status(rollback);
    EXPECT(rollback_status.state ==
           UpdateRecoveryOperatorState::rollback_required);
    EXPECT(rollback_status.action ==
           UpdateRecoveryOperatorAction::reboot_to_baseline);
    EXPECT(rollback_status.operation_succeeded);
    EXPECT(rollback_status.reboot_required);
    EXPECT(rollback_status.normal_operation_blocked);

    UpdateRecoveryBootResult recovered{};
    recovered.state = UpdateRecoveryBootState::baseline_recovered;
    recovered.reason = UpdateRecoveryBootReason::none;
    recovered.application_allowed = true;
    recovered.checkpoint_cleanup_required = true;
    recovered.active_generation = 9;
    recovered.trusted_generation = 9;
    const auto recovered_status = make_update_recovery_status(recovered);
    EXPECT(recovered_status.state ==
           UpdateRecoveryOperatorState::cleanup_required);
    EXPECT(recovered_status.reason ==
           UpdateRecoveryOperatorReason::baseline_recovered);
    EXPECT(recovered_status.cleanup_required);
    EXPECT(!recovered_status.normal_operation_blocked);

    recovered.state =
        UpdateRecoveryBootState::confirmed_cleanup_required;
    const auto confirmed_status = make_update_recovery_status(recovered);
    EXPECT(confirmed_status.reason ==
           UpdateRecoveryOperatorReason::cleanup_required);
    EXPECT(confirmed_status.action ==
           UpdateRecoveryOperatorAction::cleanup_update_state);
}

void test_boot_safe_service_and_reconciliation_are_redacted() {
    UpdateRecoveryBootResult safe{};
    safe.state = UpdateRecoveryBootState::safe_mode;
    safe.reason = UpdateRecoveryBootReason::rollback_detected;
    const auto safe_status = make_update_recovery_status(safe);
    EXPECT(safe_status.state == UpdateRecoveryOperatorState::safe_mode);
    EXPECT(safe_status.reason ==
           UpdateRecoveryOperatorReason::rollback_detected);
    EXPECT(safe_status.action == UpdateRecoveryOperatorAction::service);
    EXPECT(safe_status.normal_operation_blocked);

    UpdateRecoveryBootResult service{};
    service.state = UpdateRecoveryBootState::service_required;
    service.reason = UpdateRecoveryBootReason::invalid_policy;
    service.guard_error = UpdateGuardError::invalid_configuration;
    const auto service_status = make_update_recovery_status(service);
    EXPECT(service_status.state ==
           UpdateRecoveryOperatorState::service_required);
    EXPECT(service_status.reason ==
           UpdateRecoveryOperatorReason::invalid_configuration);

    UpdateRecoveryBootResult reconcile{};
    reconcile.state = UpdateRecoveryBootState::service_required;
    reconcile.reason =
        UpdateRecoveryBootReason::checkpoint_commit_uncertain;
    reconcile.save.error = UpdateCheckpointStoreError::storage_failure;
    reconcile.save.commit_uncertain = true;
    reconcile.reconciliation_required = true;
    const auto reconcile_status = make_update_recovery_status(reconcile);
    EXPECT(reconcile_status.state ==
           UpdateRecoveryOperatorState::reboot_reconcile_required);
    EXPECT(reconcile_status.reason ==
           UpdateRecoveryOperatorReason::commit_uncertain);
    EXPECT(reconcile_status.action ==
           UpdateRecoveryOperatorAction::reboot_and_reconcile);
    EXPECT(reconcile_status.reboot_required);
}

void test_save_outcomes_have_fixed_operator_actions() {
    const auto committed =
        make_update_recovery_status(committed_persistence());
    EXPECT(committed.operation == UpdateRecoveryStatusOperation::save);
    EXPECT(committed.state ==
           UpdateRecoveryOperatorState::persistence_committed);
    EXPECT(committed.action == UpdateRecoveryOperatorAction::none);
    EXPECT(committed.operation_succeeded);
    EXPECT(!committed.normal_operation_blocked);
    EXPECT(committed.observed_generation == 5);
    EXPECT(committed.trusted_generation == 5);

    const auto reboot = make_update_recovery_status(failed_persistence(
        UpdatePersistenceState::reboot_reconcile_required,
        UpdatePersistenceReason::commit_uncertain));
    EXPECT(reboot.state ==
           UpdateRecoveryOperatorState::reboot_reconcile_required);
    EXPECT(reboot.action ==
           UpdateRecoveryOperatorAction::reboot_and_reconcile);
    EXPECT(reboot.reboot_required);

    const auto safe = make_update_recovery_status(failed_persistence(
        UpdatePersistenceState::safe_mode,
        UpdatePersistenceReason::rollback_detected));
    EXPECT(safe.state == UpdateRecoveryOperatorState::safe_mode);
    EXPECT(safe.reason == UpdateRecoveryOperatorReason::rollback_detected);

    const auto service = make_update_recovery_status(failed_persistence(
        UpdatePersistenceState::service_required,
        UpdatePersistenceReason::storage_failure));
    EXPECT(service.state ==
           UpdateRecoveryOperatorState::service_required);
    EXPECT(service.reason ==
           UpdateRecoveryOperatorReason::storage_unavailable);
}

void test_volatile_and_rejected_transitions_do_not_claim_commit() {
    UpdateTransitionResult rejected{};
    rejected.operation = UpdateTransitionOperation::confirm;
    rejected.state = UpdateTransitionState::rejected;
    rejected.guard_error = UpdateGuardError::insufficient_health;
    rejected.before_state = UpdateState::trial;
    rejected.attempted_state = UpdateState::trial;
    rejected.live_state = UpdateState::trial;
    const auto rejected_status = make_update_recovery_status(rejected);
    EXPECT(rejected_status.state ==
           UpdateRecoveryOperatorState::transition_rejected);
    EXPECT(rejected_status.action == UpdateRecoveryOperatorAction::none);
    EXPECT(!rejected_status.operation_succeeded);
    EXPECT(!rejected_status.normal_operation_blocked);

    UpdateTransitionResult volatile_result{};
    volatile_result.operation = UpdateTransitionOperation::tick;
    volatile_result.state = UpdateTransitionState::applied_volatile;
    volatile_result.guard_error = UpdateGuardError::none;
    volatile_result.before_state = UpdateState::trial;
    volatile_result.attempted_state = UpdateState::trial;
    volatile_result.live_state = UpdateState::trial;
    const auto volatile_status =
        make_update_recovery_status(volatile_result);
    EXPECT(volatile_status.state ==
           UpdateRecoveryOperatorState::trial_active);
    EXPECT(volatile_status.operation_succeeded);
    EXPECT(volatile_status.confirmation_required);
    EXPECT(!volatile_status.normal_operation_blocked);
}

void test_committed_transitions_identify_confirmation_and_rollback() {
    const auto confirmation = make_update_recovery_status(durable_transition(
        UpdateTransitionOperation::confirm,
        UpdateGuardError::none,
        UpdateState::trial,
        UpdateState::confirmed,
        committed_persistence()));
    EXPECT(confirmation.state == UpdateRecoveryOperatorState::operational);
    EXPECT(confirmation.reason ==
           UpdateRecoveryOperatorReason::confirmation_committed);
    EXPECT(confirmation.action ==
           UpdateRecoveryOperatorAction::continue_operation);
    EXPECT(confirmation.operation_succeeded);
    EXPECT(!confirmation.normal_operation_blocked);

    const auto explicit_rollback =
        make_update_recovery_status(durable_transition(
            UpdateTransitionOperation::request_rollback,
            UpdateGuardError::none,
            UpdateState::trial,
            UpdateState::rollback_required,
            committed_persistence()));
    EXPECT(explicit_rollback.state ==
           UpdateRecoveryOperatorState::rollback_required);
    EXPECT(explicit_rollback.reason ==
           UpdateRecoveryOperatorReason::explicit_rollback);
    EXPECT(explicit_rollback.reboot_required);

    const auto deadline_rollback =
        make_update_recovery_status(durable_transition(
            UpdateTransitionOperation::tick,
            UpdateGuardError::confirmation_timeout,
            UpdateState::trial,
            UpdateState::rollback_required,
            committed_persistence()));
    EXPECT(deadline_rollback.reason ==
           UpdateRecoveryOperatorReason::confirmation_timeout);
    EXPECT(deadline_rollback.action ==
           UpdateRecoveryOperatorAction::reboot_to_baseline);
}

void test_transition_persistence_failures_stop_normal_operation() {
    const auto reboot = make_update_recovery_status(durable_transition(
        UpdateTransitionOperation::confirm,
        UpdateGuardError::none,
        UpdateState::trial,
        UpdateState::confirmed,
        failed_persistence(
            UpdatePersistenceState::reboot_reconcile_required,
            UpdatePersistenceReason::commit_uncertain)));
    EXPECT(reboot.state ==
           UpdateRecoveryOperatorState::reboot_reconcile_required);
    EXPECT(reboot.reason == UpdateRecoveryOperatorReason::commit_uncertain);
    EXPECT(reboot.reboot_required);
    EXPECT(reboot.normal_operation_blocked);

    const auto safe = make_update_recovery_status(durable_transition(
        UpdateTransitionOperation::request_rollback,
        UpdateGuardError::none,
        UpdateState::trial,
        UpdateState::rollback_required,
        failed_persistence(
            UpdatePersistenceState::safe_mode,
            UpdatePersistenceReason::generation_conflict)));
    EXPECT(safe.state == UpdateRecoveryOperatorState::safe_mode);
    EXPECT(safe.reason ==
           UpdateRecoveryOperatorReason::generation_conflict);

    const auto service = make_update_recovery_status(durable_transition(
        UpdateTransitionOperation::confirm,
        UpdateGuardError::none,
        UpdateState::trial,
        UpdateState::confirmed,
        failed_persistence(
            UpdatePersistenceState::service_required,
            UpdatePersistenceReason::storage_failure)));
    EXPECT(service.state ==
           UpdateRecoveryOperatorState::service_required);
    EXPECT(service.action == UpdateRecoveryOperatorAction::service);
}

void test_unknown_and_incoherent_results_fail_closed() {
    const auto default_boot =
        make_update_recovery_status(UpdateRecoveryBootResult{});
    EXPECT(default_boot.state ==
           UpdateRecoveryOperatorState::service_required);
    EXPECT(default_boot.reason ==
           UpdateRecoveryOperatorReason::invalid_result);
    EXPECT(default_boot.action == UpdateRecoveryOperatorAction::service);
    EXPECT(default_boot.normal_operation_blocked);
    EXPECT(default_boot.attention_required);

    UpdateRecoveryBootResult mismatched_trial{};
    mismatched_trial.state = UpdateRecoveryBootState::trial_ready;
    mismatched_trial.reason = UpdateRecoveryBootReason::none;
    mismatched_trial.application_allowed = true;
    mismatched_trial.confirmation_required = true;
    mismatched_trial.active_generation = 6;
    mismatched_trial.trusted_generation = 5;
    EXPECT(make_update_recovery_status(mismatched_trial).reason ==
           UpdateRecoveryOperatorReason::invalid_result);

    auto incomplete_commit = committed_persistence();
    incomplete_commit.inspection.checkpoint_available = false;
    EXPECT(make_update_recovery_status(incomplete_commit).reason ==
           UpdateRecoveryOperatorReason::invalid_result);

    auto contradictory = durable_transition(
        UpdateTransitionOperation::confirm,
        UpdateGuardError::none,
        UpdateState::trial,
        UpdateState::confirmed,
        committed_persistence());
    contradictory.operation = UpdateTransitionOperation::tick;
    const auto contradiction = make_update_recovery_status(contradictory);
    EXPECT(contradiction.state ==
           UpdateRecoveryOperatorState::service_required);
    EXPECT(contradiction.reason ==
           UpdateRecoveryOperatorReason::invalid_result);
    EXPECT(!contradiction.operation_succeeded);

    UpdateTransitionResult unknown{};
    unknown.state = static_cast<UpdateTransitionState>(255);
    EXPECT(make_update_recovery_status(unknown).reason ==
           UpdateRecoveryOperatorReason::invalid_result);
}

}  // namespace

int main() {
    test_boot_baseline_and_trial_are_actionable();
    test_boot_rollback_and_cleanup_actions_are_distinct();
    test_boot_safe_service_and_reconciliation_are_redacted();
    test_save_outcomes_have_fixed_operator_actions();
    test_volatile_and_rejected_transitions_do_not_claim_commit();
    test_committed_transitions_identify_confirmation_and_rollback();
    test_transition_persistence_failures_stop_normal_operation();
    test_unknown_and_incoherent_results_fail_closed();

    if (failures != 0) {
        std::cerr << failures <<
            " update recovery status assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 redacted update recovery status scenario groups\n";
    return EXIT_SUCCESS;
}
