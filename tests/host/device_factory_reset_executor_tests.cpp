#include <cstdint>
#include <iostream>
#include <type_traits>

#include "opentrail/device_factory_reset_executor.hpp"

namespace {
using namespace opentrail::companion;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(value) expect((value), #value, __LINE__)

struct Trace {
    std::uint8_t next{1};
    bool ordered{true};

    void observe(std::uint8_t value) {
        if (value != next) ordered = false;
        ++next;
    }
};

class Marker final : public DeviceFactoryResetMarkerPort {
public:
    DeviceFactoryResetMarkerState state{
        DeviceFactoryResetMarkerState::absent};
    std::uint64_t receipt{0};
    DeviceFactoryResetPortError load_error{DeviceFactoryResetPortError::none};
    DeviceFactoryResetPortError commit_error{
        DeviceFactoryResetPortError::none};
    DeviceFactoryResetPortError clear_error{
        DeviceFactoryResetPortError::none};
    bool commit_applies_on_error{false};
    bool clear_applies_on_error{false};
    bool wrong_commit_readback{false};
    bool wrong_clear_readback{false};
    DeviceFactoryResetPortError consume_error{
        DeviceFactoryResetPortError::none};
    bool consume_applies_on_error{false};
    bool wrong_consume_readback{false};
    bool reenter_on_commit{false};
    bool reenter_on_clear{false};
    DeviceFactoryResetExecutor* executor{nullptr};
    DeviceFactoryResetResult reentry_result{};
    Trace* trace{nullptr};
    std::uint32_t loads{0};
    std::uint32_t commits{0};
    std::uint32_t clears{0};

    DeviceFactoryResetMarkerSnapshot load() override {
        ++loads;
        return {load_error, state, receipt};
    }

    DeviceFactoryResetMarkerSnapshot commit_intent_and_readback(
        std::uint64_t requested_receipt) override {
        ++commits;
        if (reenter_on_commit && executor != nullptr) {
            reenter_on_commit = false;
            reentry_result = executor->begin();
        }
        if (commit_error != DeviceFactoryResetPortError::none) {
            if (commit_applies_on_error) {
                state = DeviceFactoryResetMarkerState::intent_committed;
                receipt = requested_receipt;
            }
            return {commit_error, state, receipt};
        }
        state = DeviceFactoryResetMarkerState::intent_committed;
        receipt = requested_receipt;
        return {DeviceFactoryResetPortError::none,
                wrong_commit_readback
                    ? DeviceFactoryResetMarkerState::absent
                    : state,
                wrong_commit_readback ? 0 : receipt};
    }

    DeviceFactoryResetMarkerSnapshot complete_cleanup_and_readback() override {
        ++clears;
        if (trace != nullptr) trace->observe(3);
        if (reenter_on_clear && executor != nullptr) {
            reenter_on_clear = false;
            reentry_result = executor->continue_cleanup();
        }
        if (clear_error != DeviceFactoryResetPortError::none) {
            if (clear_applies_on_error) {
                state = receipt == 0
                            ? DeviceFactoryResetMarkerState::absent
                            : DeviceFactoryResetMarkerState::receipt_pending;
                if (state == DeviceFactoryResetMarkerState::absent) receipt = 0;
            }
            return {clear_error, state, receipt};
        }
        state = receipt == 0
                    ? DeviceFactoryResetMarkerState::absent
                    : DeviceFactoryResetMarkerState::receipt_pending;
        return {DeviceFactoryResetPortError::none,
                wrong_clear_readback
                    ? DeviceFactoryResetMarkerState::intent_committed
                    : state,
                receipt};
    }

    DeviceFactoryResetReceiptConsumeSnapshot
    consume_completion_receipt_and_readback() override {
        if (state != DeviceFactoryResetMarkerState::receipt_pending ||
            receipt == 0) {
            return {DeviceFactoryResetPortError::failed, 0, false};
        }
        const auto consumed = receipt;
        if (consume_error != DeviceFactoryResetPortError::none) {
            if (consume_applies_on_error) {
                state = DeviceFactoryResetMarkerState::absent;
                receipt = 0;
            }
            return {consume_error, 0, false};
        }
        state = DeviceFactoryResetMarkerState::absent;
        receipt = 0;
        return {DeviceFactoryResetPortError::none,
                wrong_consume_readback ? consumed + 1 : consumed,
                !wrong_consume_readback};
    }
};

class UserDomain final : public DeviceFactoryResetUserDomainPort {
public:
    bool absent{false};
    DeviceFactoryResetPortError inspect_error{
        DeviceFactoryResetPortError::none};
    DeviceFactoryResetPortError erase_error{
        DeviceFactoryResetPortError::none};
    bool erase_applies_on_error{false};
    bool wrong_verification{false};
    bool reenter_on_erase{false};
    DeviceFactoryResetExecutor* executor{nullptr};
    DeviceFactoryResetResult reentry_result{};
    Trace* trace{nullptr};
    std::uint32_t inspections{0};
    std::uint32_t erases{0};

    DeviceFactoryResetAbsenceSnapshot inspect_absence() override {
        ++inspections;
        return {inspect_error, absent};
    }

    DeviceFactoryResetAbsenceSnapshot erase_all_and_verify_absent() override {
        ++erases;
        if (trace != nullptr) trace->observe(1);
        if (reenter_on_erase && executor != nullptr) {
            reenter_on_erase = false;
            reentry_result = executor->continue_cleanup();
        }
        if (erase_error != DeviceFactoryResetPortError::none) {
            if (erase_applies_on_error) absent = true;
            return {erase_error, absent};
        }
        absent = true;
        return {DeviceFactoryResetPortError::none,
                wrong_verification ? false : absent};
    }
};

class BondDomain final : public DeviceFactoryResetBondDomainPort {
public:
    bool empty{false};
    DeviceFactoryResetPortError inspect_error{
        DeviceFactoryResetPortError::none};
    DeviceFactoryResetPortError erase_error{
        DeviceFactoryResetPortError::none};
    bool erase_applies_on_error{false};
    bool wrong_verification{false};
    Trace* trace{nullptr};
    std::uint32_t inspections{0};
    std::uint32_t erases{0};

    DeviceFactoryResetAbsenceSnapshot inspect_empty() override {
        ++inspections;
        return {inspect_error, empty};
    }

    DeviceFactoryResetAbsenceSnapshot erase_all_and_verify_empty() override {
        ++erases;
        if (trace != nullptr) trace->observe(2);
        if (erase_error != DeviceFactoryResetPortError::none) {
            if (erase_applies_on_error) empty = true;
            return {erase_error, empty};
        }
        empty = true;
        return {DeviceFactoryResetPortError::none,
                wrong_verification ? false : empty};
    }
};

void test_restore_distinguishes_old_unowned_and_incoherent_state() {
    Marker old_marker{};
    UserDomain old_user{};
    BondDomain old_bonds{};
    DeviceFactoryResetExecutor old{old_marker, old_user, old_bonds};
    EXPECT(old.restore().accepted());
    EXPECT(old.status().phase == DeviceFactoryResetPhase::idle_old_state);
    EXPECT(old.status().old_state_preserved);
    EXPECT(!old.status().intent_verified);

    Marker empty_marker{};
    UserDomain empty_user{};
    empty_user.absent = true;
    BondDomain empty_bonds{};
    empty_bonds.empty = true;
    DeviceFactoryResetExecutor empty{empty_marker, empty_user, empty_bonds};
    EXPECT(empty.restore().accepted());
    EXPECT(empty.status().phase == DeviceFactoryResetPhase::idle_unowned);
    EXPECT(!empty.status().reboot_unowned_permitted);
    EXPECT(empty.begin().error == DeviceFactoryResetError::invalid_state);

    Marker mixed_marker{};
    UserDomain mixed_user{};
    mixed_user.absent = true;
    BondDomain mixed_bonds{};
    DeviceFactoryResetExecutor mixed{mixed_marker, mixed_user, mixed_bonds};
    EXPECT(mixed.restore().error ==
           DeviceFactoryResetError::initial_state_incoherent);
    EXPECT(mixed.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);

    Marker unavailable_marker{};
    UserDomain unavailable_user{};
    unavailable_user.inspect_error = DeviceFactoryResetPortError::not_ready;
    BondDomain unavailable_bonds{};
    DeviceFactoryResetExecutor unavailable{
        unavailable_marker, unavailable_user, unavailable_bonds};
    EXPECT(unavailable.restore().error ==
           DeviceFactoryResetError::initial_state_unavailable);
    EXPECT(unavailable_bonds.inspections == 1);
    EXPECT(unavailable.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);
}

void test_known_no_change_commit_preserves_old_state() {
    Marker marker{};
    marker.commit_error = DeviceFactoryResetPortError::known_no_change;
    UserDomain user{};
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    const auto result = executor.begin();
    EXPECT(result.error ==
           DeviceFactoryResetError::marker_commit_known_no_change);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::idle_old_state);
    EXPECT(executor.status().old_state_preserved);
    EXPECT(marker.state == DeviceFactoryResetMarkerState::absent);
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);
    EXPECT(marker.clears == 0);
}

void test_confirmed_recovery_is_containment_only_and_marker_gated() {
    Marker normal_marker{};
    UserDomain normal_user{};
    BondDomain normal_bonds{};
    DeviceFactoryResetExecutor normal{
        normal_marker, normal_user, normal_bonds};
    EXPECT(normal.restore().accepted());
    EXPECT(normal.begin_confirmed_recovery().error ==
           DeviceFactoryResetError::invalid_state);
    EXPECT(normal_marker.commits == 0);

    // A runtime-start failure can occur before the executor reaches restore().
    // The physical-confirmation-only entry may commit intent from this exact
    // state, but still performs no erase until the verified marker is present.
    Marker early_marker{};
    UserDomain early_user{};
    BondDomain early_bonds{};
    DeviceFactoryResetExecutor early{
        early_marker, early_user, early_bonds};
    const auto early_begun = early.begin_confirmed_recovery();
    EXPECT(early_begun.accepted());
    EXPECT(early_begun.phase ==
           DeviceFactoryResetPhase::cleanup_required);
    EXPECT(early_marker.commits == 1);
    EXPECT(early_marker.state ==
           DeviceFactoryResetMarkerState::intent_committed);
    EXPECT(early_user.erases == 0);
    EXPECT(early_bonds.erases == 0);

    Marker marker{};
    UserDomain user{};
    user.absent = true;
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().error ==
           DeviceFactoryResetError::initial_state_incoherent);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);

    const auto begun = executor.begin_confirmed_recovery();
    EXPECT(begun.accepted());
    EXPECT(begun.phase == DeviceFactoryResetPhase::cleanup_required);
    EXPECT(marker.commits == 1);
    EXPECT(marker.state ==
           DeviceFactoryResetMarkerState::intent_committed);
    EXPECT(executor.status().intent_verified);
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);

    EXPECT(executor.continue_cleanup().accepted());
    EXPECT(user.erases == 1);
    EXPECT(bonds.erases == 1);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::reboot_unowned_permitted);
}

void test_confirmed_recovery_known_no_change_stays_contained() {
    Marker marker{};
    marker.commit_error = DeviceFactoryResetPortError::known_no_change;
    UserDomain user{};
    user.absent = true;
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().error ==
           DeviceFactoryResetError::initial_state_incoherent);
    const auto result = executor.begin_confirmed_recovery();
    EXPECT(result.error ==
           DeviceFactoryResetError::marker_commit_known_no_change);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);
    EXPECT(!executor.status().intent_verified);
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);
}

void test_verified_intent_orders_cleanup_and_alone_permits_reboot() {
    Marker marker{};
    UserDomain user{};
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    EXPECT(executor.begin().accepted());
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::cleanup_required);
    EXPECT(executor.status().intent_verified);
    EXPECT(!executor.status().old_state_preserved);
    EXPECT(!executor.status().reboot_unowned_permitted);
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);

    Trace trace{};
    marker.trace = &trace;
    user.trace = &trace;
    bonds.trace = &trace;
    EXPECT(executor.continue_cleanup().accepted());
    EXPECT(trace.ordered);
    EXPECT(trace.next == 4);
    EXPECT(user.absent);
    EXPECT(bonds.empty);
    EXPECT(marker.state == DeviceFactoryResetMarkerState::absent);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::reboot_unowned_permitted);
    EXPECT(executor.status().reboot_unowned_permitted);
    EXPECT(!executor.status().cleanup_required);
    EXPECT(executor.continue_cleanup().error ==
           DeviceFactoryResetError::invalid_state);
}

void test_uncertain_commit_never_erases_and_reboot_reconciles() {
    for (std::uint8_t applied = 0; applied < 2; ++applied) {
        Marker marker{};
        marker.commit_error = DeviceFactoryResetPortError::uncertain;
        marker.commit_applies_on_error = applied != 0;
        UserDomain user{};
        BondDomain bonds{};
        DeviceFactoryResetExecutor executor{marker, user, bonds};
        EXPECT(executor.restore().accepted());
        EXPECT(executor.begin().error ==
               DeviceFactoryResetError::marker_commit_uncertain);
        EXPECT(executor.status().phase ==
               DeviceFactoryResetPhase::reconciliation_required);
        EXPECT(user.erases == 0);
        EXPECT(bonds.erases == 0);

        marker.commit_error = DeviceFactoryResetPortError::none;
        DeviceFactoryResetExecutor rebooted{marker, user, bonds};
        EXPECT(rebooted.restore().accepted());
        if (applied == 0) {
            EXPECT(rebooted.status().phase ==
                   DeviceFactoryResetPhase::idle_old_state);
            EXPECT(rebooted.status().old_state_preserved);
        } else {
            EXPECT(rebooted.status().phase ==
                   DeviceFactoryResetPhase::cleanup_required);
            EXPECT(rebooted.status().intent_verified);
            EXPECT(rebooted.continue_cleanup().accepted());
            EXPECT(rebooted.status().reboot_unowned_permitted);
        }
    }
}

void test_boot_with_verified_intent_resumes_cleanup() {
    Marker marker{};
    marker.state = DeviceFactoryResetMarkerState::intent_committed;
    UserDomain user{};
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::cleanup_required);
    EXPECT(user.inspections == 0);
    EXPECT(bonds.inspections == 0);
    EXPECT(executor.continue_cleanup().accepted());
    EXPECT(executor.status().reboot_unowned_permitted);
}

void test_power_loss_after_marker_commit_resumes_before_domain_access() {
    Marker marker{};
    UserDomain user{};
    BondDomain bonds{};
    {
        DeviceFactoryResetExecutor interrupted{marker, user, bonds};
        EXPECT(interrupted.restore().accepted());
        EXPECT(interrupted.begin().accepted());
        EXPECT(interrupted.status().cleanup_required);
        EXPECT(marker.state ==
               DeviceFactoryResetMarkerState::intent_committed);
        EXPECT(user.erases == 0);
        EXPECT(bonds.erases == 0);
        EXPECT(marker.clears == 0);
    }

    DeviceFactoryResetExecutor rebooted{marker, user, bonds};
    EXPECT(rebooted.restore().accepted());
    EXPECT(rebooted.status().cleanup_required);
    EXPECT(rebooted.status().intent_verified);
    EXPECT(user.inspections == 1);
    EXPECT(bonds.inspections == 1);
    EXPECT(rebooted.continue_cleanup().accepted());
    EXPECT(rebooted.status().reboot_unowned_permitted);
}

void test_power_loss_during_user_erase_resumes_idempotently() {
    for (std::uint8_t applied = 0; applied < 2; ++applied) {
        Marker marker{};
        UserDomain user{};
        user.erase_error = DeviceFactoryResetPortError::uncertain;
        user.erase_applies_on_error = applied != 0;
        BondDomain bonds{};
        {
            DeviceFactoryResetExecutor interrupted{marker, user, bonds};
            EXPECT(interrupted.restore().accepted());
            EXPECT(interrupted.begin().accepted());
            EXPECT(interrupted.continue_cleanup().error ==
                   DeviceFactoryResetError::user_domain_erase_failed);
            EXPECT(interrupted.status().cleanup_required);
            EXPECT(marker.state ==
                   DeviceFactoryResetMarkerState::intent_committed);
            EXPECT(user.absent == (applied != 0));
            EXPECT(bonds.erases == 0);
            EXPECT(marker.clears == 0);
        }

        user.erase_error = DeviceFactoryResetPortError::none;
        DeviceFactoryResetExecutor rebooted{marker, user, bonds};
        EXPECT(rebooted.restore().accepted());
        EXPECT(rebooted.status().cleanup_required);
        EXPECT(rebooted.continue_cleanup().accepted());
        EXPECT(user.erases == 2);
        EXPECT(bonds.erases == 1);
        EXPECT(marker.clears == 1);
        EXPECT(rebooted.status().reboot_unowned_permitted);
    }
}

void test_power_loss_during_bond_erase_resumes_idempotently() {
    for (std::uint8_t applied = 0; applied < 2; ++applied) {
        Marker marker{};
        UserDomain user{};
        BondDomain bonds{};
        bonds.erase_error = DeviceFactoryResetPortError::uncertain;
        bonds.erase_applies_on_error = applied != 0;
        {
            DeviceFactoryResetExecutor interrupted{marker, user, bonds};
            EXPECT(interrupted.restore().accepted());
            EXPECT(interrupted.begin().accepted());
            EXPECT(interrupted.continue_cleanup().error ==
                   DeviceFactoryResetError::bond_erase_failed);
            EXPECT(interrupted.status().cleanup_required);
            EXPECT(marker.state ==
                   DeviceFactoryResetMarkerState::intent_committed);
            EXPECT(user.absent);
            EXPECT(bonds.empty == (applied != 0));
            EXPECT(marker.clears == 0);
        }

        bonds.erase_error = DeviceFactoryResetPortError::none;
        DeviceFactoryResetExecutor rebooted{marker, user, bonds};
        EXPECT(rebooted.restore().accepted());
        EXPECT(rebooted.status().cleanup_required);
        EXPECT(rebooted.continue_cleanup().accepted());
        EXPECT(user.erases == 2);
        EXPECT(bonds.erases == 2);
        EXPECT(marker.clears == 1);
        EXPECT(rebooted.status().reboot_unowned_permitted);
    }
}

void test_power_loss_during_marker_clear_restores_exact_outcome() {
    for (std::uint8_t applied = 0; applied < 2; ++applied) {
        Marker marker{};
        marker.clear_error = DeviceFactoryResetPortError::uncertain;
        marker.clear_applies_on_error = applied != 0;
        UserDomain user{};
        BondDomain bonds{};
        {
            DeviceFactoryResetExecutor interrupted{marker, user, bonds};
            EXPECT(interrupted.restore().accepted());
            EXPECT(interrupted.begin().accepted());
            EXPECT(interrupted.continue_cleanup().error ==
                   DeviceFactoryResetError::marker_completion_failed);
            EXPECT(interrupted.status().cleanup_required);
            EXPECT(user.absent);
            EXPECT(bonds.empty);
            EXPECT(marker.state ==
                   (applied != 0
                        ? DeviceFactoryResetMarkerState::absent
                        : DeviceFactoryResetMarkerState::intent_committed));
        }

        marker.clear_error = DeviceFactoryResetPortError::none;
        DeviceFactoryResetExecutor rebooted{marker, user, bonds};
        EXPECT(rebooted.restore().accepted());
        if (applied == 0) {
            EXPECT(rebooted.status().cleanup_required);
            EXPECT(rebooted.continue_cleanup().accepted());
            EXPECT(rebooted.status().reboot_unowned_permitted);
        } else {
            EXPECT(rebooted.status().phase ==
                   DeviceFactoryResetPhase::idle_unowned);
            EXPECT(!rebooted.status().cleanup_required);
            EXPECT(rebooted.continue_cleanup().error ==
                   DeviceFactoryResetError::invalid_state);
        }
    }
}

void test_every_user_failure_remains_retryable_cleanup() {
    const DeviceFactoryResetPortError errors[] = {
        DeviceFactoryResetPortError::not_ready,
        DeviceFactoryResetPortError::failed,
        DeviceFactoryResetPortError::known_no_change,
        DeviceFactoryResetPortError::uncertain,
    };
    for (const auto error : errors) {
        Marker marker{};
        UserDomain user{};
        user.erase_error = error;
        BondDomain bonds{};
        DeviceFactoryResetExecutor executor{marker, user, bonds};
        EXPECT(executor.restore().accepted());
        EXPECT(executor.begin().accepted());
        EXPECT(executor.continue_cleanup().error ==
               DeviceFactoryResetError::user_domain_erase_failed);
        EXPECT(executor.status().phase ==
               DeviceFactoryResetPhase::cleanup_required);
        EXPECT(executor.status().intent_verified);
        EXPECT(marker.state ==
               DeviceFactoryResetMarkerState::intent_committed);
        EXPECT(bonds.erases == 0);
        EXPECT(marker.clears == 0);
        user.erase_error = DeviceFactoryResetPortError::none;
        EXPECT(executor.continue_cleanup().accepted());
    }

    Marker marker{};
    UserDomain user{};
    user.wrong_verification = true;
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    EXPECT(executor.begin().accepted());
    EXPECT(executor.continue_cleanup().error ==
           DeviceFactoryResetError::user_domain_not_absent);
    EXPECT(executor.status().cleanup_required);
    EXPECT(bonds.erases == 0);
    user.wrong_verification = false;
    EXPECT(executor.continue_cleanup().accepted());
}

void test_every_bond_failure_remains_retryable_cleanup() {
    const DeviceFactoryResetPortError errors[] = {
        DeviceFactoryResetPortError::not_ready,
        DeviceFactoryResetPortError::failed,
        DeviceFactoryResetPortError::known_no_change,
        DeviceFactoryResetPortError::uncertain,
    };
    for (const auto error : errors) {
        Marker marker{};
        UserDomain user{};
        BondDomain bonds{};
        bonds.erase_error = error;
        DeviceFactoryResetExecutor executor{marker, user, bonds};
        EXPECT(executor.restore().accepted());
        EXPECT(executor.begin().accepted());
        EXPECT(executor.continue_cleanup().error ==
               DeviceFactoryResetError::bond_erase_failed);
        EXPECT(executor.status().cleanup_required);
        EXPECT(user.absent);
        EXPECT(marker.state ==
               DeviceFactoryResetMarkerState::intent_committed);
        EXPECT(marker.clears == 0);
        bonds.erase_error = DeviceFactoryResetPortError::none;
        EXPECT(executor.continue_cleanup().accepted());
        EXPECT(user.erases == 2);
    }

    Marker marker{};
    UserDomain user{};
    BondDomain bonds{};
    bonds.wrong_verification = true;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    EXPECT(executor.begin().accepted());
    EXPECT(executor.continue_cleanup().error ==
           DeviceFactoryResetError::bond_inventory_not_empty);
    EXPECT(executor.status().cleanup_required);
    EXPECT(marker.clears == 0);
    bonds.wrong_verification = false;
    EXPECT(executor.continue_cleanup().accepted());
}

void test_marker_clear_failures_never_publish_success() {
    const DeviceFactoryResetPortError errors[] = {
        DeviceFactoryResetPortError::not_ready,
        DeviceFactoryResetPortError::failed,
        DeviceFactoryResetPortError::known_no_change,
        DeviceFactoryResetPortError::uncertain,
    };
    for (const auto error : errors) {
        Marker marker{};
        marker.clear_error = error;
        UserDomain user{};
        BondDomain bonds{};
        DeviceFactoryResetExecutor executor{marker, user, bonds};
        EXPECT(executor.restore().accepted());
        EXPECT(executor.begin().accepted());
        EXPECT(executor.continue_cleanup().error ==
               DeviceFactoryResetError::marker_completion_failed);
        EXPECT(executor.status().cleanup_required);
        EXPECT(!executor.status().reboot_unowned_permitted);
        marker.clear_error = DeviceFactoryResetPortError::none;
        EXPECT(executor.continue_cleanup().accepted());
        EXPECT(executor.status().reboot_unowned_permitted);
    }

    Marker wrong_marker{};
    wrong_marker.wrong_clear_readback = true;
    UserDomain wrong_user{};
    BondDomain wrong_bonds{};
    DeviceFactoryResetExecutor wrong{
        wrong_marker, wrong_user, wrong_bonds};
    EXPECT(wrong.restore().accepted());
    EXPECT(wrong.begin().accepted());
    EXPECT(wrong.continue_cleanup().error ==
           DeviceFactoryResetError::marker_completion_invalid_readback);
    EXPECT(wrong.status().cleanup_required);
    wrong_marker.wrong_clear_readback = false;
    EXPECT(wrong.continue_cleanup().accepted());
}

void test_clear_uncertainty_applied_resolves_unowned_on_boot() {
    Marker marker{};
    marker.clear_error = DeviceFactoryResetPortError::uncertain;
    marker.clear_applies_on_error = true;
    UserDomain user{};
    BondDomain bonds{};
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    EXPECT(executor.begin().accepted());
    EXPECT(executor.continue_cleanup().error ==
           DeviceFactoryResetError::marker_completion_failed);
    EXPECT(executor.status().cleanup_required);
    EXPECT(marker.state == DeviceFactoryResetMarkerState::absent);
    EXPECT(user.absent);
    EXPECT(bonds.empty);

    marker.clear_error = DeviceFactoryResetPortError::none;
    DeviceFactoryResetExecutor rebooted{marker, user, bonds};
    EXPECT(rebooted.restore().accepted());
    EXPECT(rebooted.status().phase == DeviceFactoryResetPhase::idle_unowned);
    EXPECT(!rebooted.status().cleanup_required);
    EXPECT(!rebooted.status().reboot_unowned_permitted);
}

void test_invalid_marker_results_and_reentry_fail_closed() {
    Marker bad_load{};
    bad_load.state = DeviceFactoryResetMarkerState::invalid;
    UserDomain load_user{};
    BondDomain load_bonds{};
    DeviceFactoryResetExecutor load_executor{
        bad_load, load_user, load_bonds};
    EXPECT(load_executor.restore().error ==
           DeviceFactoryResetError::marker_load_failed);
    EXPECT(load_executor.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);

    Marker bad_commit{};
    bad_commit.wrong_commit_readback = true;
    UserDomain commit_user{};
    BondDomain commit_bonds{};
    DeviceFactoryResetExecutor commit_executor{
        bad_commit, commit_user, commit_bonds};
    EXPECT(commit_executor.restore().accepted());
    EXPECT(commit_executor.begin().error ==
           DeviceFactoryResetError::marker_commit_invalid_readback);
    EXPECT(commit_executor.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);
    EXPECT(commit_user.erases == 0);

    Marker reentrant_commit{};
    UserDomain reentrant_user{};
    BondDomain reentrant_bonds{};
    DeviceFactoryResetExecutor commit_outer{
        reentrant_commit, reentrant_user, reentrant_bonds};
    reentrant_commit.executor = &commit_outer;
    EXPECT(commit_outer.restore().accepted());
    reentrant_commit.reenter_on_commit = true;
    EXPECT(commit_outer.begin().error ==
           DeviceFactoryResetError::reentrant_call);
    EXPECT(reentrant_commit.reentry_result.error ==
           DeviceFactoryResetError::reentrant_call);
    EXPECT(commit_outer.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);

    Marker reentrant_cleanup{};
    UserDomain cleanup_user{};
    BondDomain cleanup_bonds{};
    DeviceFactoryResetExecutor cleanup_outer{
        reentrant_cleanup, cleanup_user, cleanup_bonds};
    cleanup_user.executor = &cleanup_outer;
    EXPECT(cleanup_outer.restore().accepted());
    EXPECT(cleanup_outer.begin().accepted());
    cleanup_user.reenter_on_erase = true;
    EXPECT(cleanup_outer.continue_cleanup().error ==
           DeviceFactoryResetError::reentrant_call);
    EXPECT(cleanup_user.reentry_result.error ==
           DeviceFactoryResetError::reentrant_call);
    EXPECT(cleanup_outer.status().phase ==
           DeviceFactoryResetPhase::cleanup_required);
    EXPECT(cleanup_outer.status().intent_verified);
}

void test_app_receipt_survives_power_loss_and_is_consumed_once() {
    constexpr std::uint64_t receipt = UINT64_C(0x1122334455667788);
    Marker marker{};
    UserDomain user{};
    BondDomain bonds{};
    {
        DeviceFactoryResetExecutor interrupted{marker, user, bonds};
        EXPECT(interrupted.restore().accepted());
        const auto begun = interrupted.begin(receipt);
        EXPECT(begun.accepted());
        EXPECT(begun.reset_receipt == receipt);
        EXPECT(marker.state ==
               DeviceFactoryResetMarkerState::intent_committed);
        EXPECT(marker.receipt == receipt);
    }

    {
        DeviceFactoryResetExecutor cleanup_boot{marker, user, bonds};
        EXPECT(cleanup_boot.restore().accepted());
        const auto cleaned = cleanup_boot.continue_cleanup();
        EXPECT(cleaned.accepted());
        EXPECT(cleaned.phase ==
               DeviceFactoryResetPhase::reboot_unowned_permitted);
        EXPECT(marker.state ==
               DeviceFactoryResetMarkerState::receipt_pending);
        EXPECT(marker.receipt == receipt);
    }

    DeviceFactoryResetExecutor receipt_boot{marker, user, bonds};
    const auto restored = receipt_boot.restore();
    EXPECT(restored.accepted());
    EXPECT(restored.phase ==
           DeviceFactoryResetPhase::completion_receipt_pending);
    EXPECT(restored.reset_receipt == receipt);
    const auto consumed = receipt_boot.consume_completion_receipt();
    EXPECT(consumed.accepted());
    EXPECT(consumed.phase == DeviceFactoryResetPhase::idle_unowned);
    EXPECT(consumed.reset_receipt == receipt);
    EXPECT(marker.state == DeviceFactoryResetMarkerState::absent);
    EXPECT(marker.receipt == 0);
    EXPECT(receipt_boot.consume_completion_receipt().error ==
           DeviceFactoryResetError::invalid_state);

    Marker physical_marker{};
    UserDomain physical_user{};
    BondDomain physical_bonds{};
    DeviceFactoryResetExecutor physical{
        physical_marker, physical_user, physical_bonds};
    EXPECT(physical.restore().accepted());
    EXPECT(physical.begin().accepted());
    EXPECT(physical.continue_cleanup().accepted());
    EXPECT(physical_marker.state == DeviceFactoryResetMarkerState::absent);
    EXPECT(physical_marker.receipt == 0);
}

void test_uncertain_receipt_consume_never_publishes_receipt() {
    constexpr std::uint64_t receipt = UINT64_C(0x8877665544332211);
    for (std::uint8_t applied = 0; applied < 2; ++applied) {
        Marker marker{};
        marker.state = DeviceFactoryResetMarkerState::receipt_pending;
        marker.receipt = receipt;
        marker.consume_error = DeviceFactoryResetPortError::uncertain;
        marker.consume_applies_on_error = applied != 0;
        UserDomain user{};
        user.absent = true;
        BondDomain bonds{};
        bonds.empty = true;
        DeviceFactoryResetExecutor executor{marker, user, bonds};
        EXPECT(executor.restore().accepted());
        const auto consumed = executor.consume_completion_receipt();
        EXPECT(consumed.error ==
               DeviceFactoryResetError::receipt_consume_failed);
        EXPECT(consumed.reset_receipt == 0);
        EXPECT(executor.status().phase ==
               DeviceFactoryResetPhase::reconciliation_required);
        EXPECT(executor.status().reset_receipt == 0);

        marker.consume_error = DeviceFactoryResetPortError::none;
        DeviceFactoryResetExecutor rebooted{marker, user, bonds};
        const auto restored = rebooted.restore();
        EXPECT(restored.accepted());
        if (applied == 0) {
            EXPECT(restored.phase ==
                   DeviceFactoryResetPhase::completion_receipt_pending);
            const auto retried = rebooted.consume_completion_receipt();
            EXPECT(retried.accepted());
            EXPECT(retried.phase == DeviceFactoryResetPhase::idle_unowned);
            EXPECT(retried.reset_receipt == receipt);
        } else {
            // The record erase committed before power loss. The app cannot
            // learn the receipt, but the device is unowned and pairable.
            EXPECT(restored.phase == DeviceFactoryResetPhase::idle_unowned);
            EXPECT(restored.reset_receipt == 0);
        }
        EXPECT(marker.state == DeviceFactoryResetMarkerState::absent);
        EXPECT(marker.receipt == 0);
    }
}

void test_atomic_receipt_transition_power_loss_has_only_whole_states() {
    constexpr std::uint64_t receipt = UINT64_C(0x1020304050607080);
    for (std::uint8_t applied = 0; applied < 2; ++applied) {
        Marker marker{};
        marker.clear_error = DeviceFactoryResetPortError::uncertain;
        marker.clear_applies_on_error = applied != 0;
        UserDomain user{};
        BondDomain bonds{};
        {
            DeviceFactoryResetExecutor interrupted{marker, user, bonds};
            EXPECT(interrupted.restore().accepted());
            EXPECT(interrupted.begin(receipt).accepted());
            EXPECT(interrupted.continue_cleanup().error ==
                   DeviceFactoryResetError::marker_completion_failed);
            EXPECT(marker.state ==
                   (applied == 0
                        ? DeviceFactoryResetMarkerState::intent_committed
                        : DeviceFactoryResetMarkerState::receipt_pending));
            EXPECT(marker.receipt == receipt);
            EXPECT(marker.state != DeviceFactoryResetMarkerState::invalid);
        }

        marker.clear_error = DeviceFactoryResetPortError::none;
        DeviceFactoryResetExecutor rebooted{marker, user, bonds};
        const auto restored = rebooted.restore();
        EXPECT(restored.accepted());
        if (applied == 0) {
            EXPECT(restored.phase == DeviceFactoryResetPhase::cleanup_required);
            EXPECT(rebooted.continue_cleanup().accepted());
            EXPECT(marker.state ==
                   DeviceFactoryResetMarkerState::receipt_pending);
        } else {
            EXPECT(restored.phase ==
                   DeviceFactoryResetPhase::completion_receipt_pending);
        }

        DeviceFactoryResetExecutor receipt_boot{marker, user, bonds};
        EXPECT(receipt_boot.restore().phase ==
               DeviceFactoryResetPhase::completion_receipt_pending);
        const auto consumed = receipt_boot.consume_completion_receipt();
        EXPECT(consumed.accepted());
        EXPECT(consumed.phase == DeviceFactoryResetPhase::idle_unowned);
        EXPECT(consumed.reset_receipt == receipt);
    }
}

static_assert(std::is_trivially_copyable_v<DeviceFactoryResetStatus>);
static_assert(std::is_trivially_copyable_v<DeviceFactoryResetResult>);

}  // namespace

int main() {
    test_restore_distinguishes_old_unowned_and_incoherent_state();
    test_known_no_change_commit_preserves_old_state();
    test_confirmed_recovery_is_containment_only_and_marker_gated();
    test_confirmed_recovery_known_no_change_stays_contained();
    test_verified_intent_orders_cleanup_and_alone_permits_reboot();
    test_uncertain_commit_never_erases_and_reboot_reconciles();
    test_boot_with_verified_intent_resumes_cleanup();
    test_power_loss_after_marker_commit_resumes_before_domain_access();
    test_power_loss_during_user_erase_resumes_idempotently();
    test_power_loss_during_bond_erase_resumes_idempotently();
    test_power_loss_during_marker_clear_restores_exact_outcome();
    test_every_user_failure_remains_retryable_cleanup();
    test_every_bond_failure_remains_retryable_cleanup();
    test_marker_clear_failures_never_publish_success();
    test_clear_uncertainty_applied_resolves_unowned_on_boot();
    test_invalid_marker_results_and_reentry_fail_closed();
    test_app_receipt_survives_power_loss_and_is_consumed_once();
    test_uncertain_receipt_consume_never_publishes_receipt();
    test_atomic_receipt_transition_power_loss_has_only_whole_states();
    if (failures != 0) {
        std::cerr << failures
                  << " device factory-reset executor assertion(s) failed\n";
        return 1;
    }
    std::cout << "PASS: 19 device factory-reset executor scenario groups\n";
    return 0;
}
