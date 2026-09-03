#include "opentrail/device_factory_reset_executor.hpp"

namespace opentrail::companion {
namespace {

class ScopedOperation final {
public:
    explicit ScopedOperation(bool& active) : active_(active) { active_ = true; }
    ~ScopedOperation() { active_ = false; }

private:
    bool& active_;
};

bool access_failed(DeviceFactoryResetPortError error) {
    return error != DeviceFactoryResetPortError::none;
}

}  // namespace

DeviceFactoryResetExecutor::DeviceFactoryResetExecutor(
    DeviceFactoryResetMarkerPort& marker,
    DeviceFactoryResetUserDomainPort& user_domain,
    DeviceFactoryResetBondDomainPort& bond_domain)
    : marker_(marker), user_domain_(user_domain), bond_domain_(bond_domain) {}

DeviceFactoryResetResult DeviceFactoryResetExecutor::reject(
    DeviceFactoryResetError error) {
    if (error == DeviceFactoryResetError::reentrant_call) {
        reentry_observed_ = true;
    }
    return {error, phase_, reset_receipt_};
}

DeviceFactoryResetResult DeviceFactoryResetExecutor::enter_reconciliation(
    DeviceFactoryResetError error) {
    phase_ = DeviceFactoryResetPhase::reconciliation_required;
    intent_verified_ = false;
    marker_state_ = DeviceFactoryResetMarkerState::invalid;
    reset_receipt_ = 0;
    return reject(error);
}

DeviceFactoryResetResult DeviceFactoryResetExecutor::require_cleanup(
    DeviceFactoryResetError error) {
    phase_ = DeviceFactoryResetPhase::cleanup_required;
    intent_verified_ = true;
    return reject(error);
}

DeviceFactoryResetResult DeviceFactoryResetExecutor::restore() {
    if (operation_active_) {
        return reject(DeviceFactoryResetError::reentrant_call);
    }
    if (phase_ != DeviceFactoryResetPhase::not_restored) {
        return reject(DeviceFactoryResetError::invalid_state);
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto marker = marker_.load();
    if (reentry_observed_) {
        return enter_reconciliation(DeviceFactoryResetError::reentrant_call);
    }
    if (access_failed(marker.error) ||
        marker.state == DeviceFactoryResetMarkerState::invalid) {
        return enter_reconciliation(
            DeviceFactoryResetError::marker_load_failed);
    }
    if (marker.state == DeviceFactoryResetMarkerState::intent_committed) {
        marker_state_ = marker.state;
        reset_receipt_ = marker.reset_receipt;
        phase_ = DeviceFactoryResetPhase::cleanup_required;
        intent_verified_ = true;
        return {DeviceFactoryResetError::none, phase_, reset_receipt_};
    }
    if (marker.state == DeviceFactoryResetMarkerState::receipt_pending) {
        if (marker.reset_receipt == 0) {
            return enter_reconciliation(
                DeviceFactoryResetError::marker_load_failed);
        }
        marker_state_ = marker.state;
        reset_receipt_ = marker.reset_receipt;
        const auto user = user_domain_.inspect_absence();
        if (reentry_observed_) {
            return enter_reconciliation(
                DeviceFactoryResetError::reentrant_call);
        }
        const auto bonds = bond_domain_.inspect_empty();
        if (reentry_observed_) {
            return enter_reconciliation(
                DeviceFactoryResetError::reentrant_call);
        }
        if (access_failed(user.error) || access_failed(bonds.error) ||
            !user.verified_absent || !bonds.verified_absent) {
            phase_ = DeviceFactoryResetPhase::cleanup_required;
            intent_verified_ = true;
            return {DeviceFactoryResetError::none, phase_, reset_receipt_};
        }
        phase_ = DeviceFactoryResetPhase::completion_receipt_pending;
        intent_verified_ = false;
        return {DeviceFactoryResetError::none, phase_, reset_receipt_};
    }
    if (marker.state != DeviceFactoryResetMarkerState::absent) {
        return enter_reconciliation(
            DeviceFactoryResetError::marker_load_failed);
    }

    const auto user = user_domain_.inspect_absence();
    if (reentry_observed_) {
        return enter_reconciliation(DeviceFactoryResetError::reentrant_call);
    }
    const auto bonds = bond_domain_.inspect_empty();
    if (reentry_observed_) {
        return enter_reconciliation(DeviceFactoryResetError::reentrant_call);
    }
    if (access_failed(user.error) || access_failed(bonds.error)) {
        return enter_reconciliation(
            DeviceFactoryResetError::initial_state_unavailable);
    }
    if (user.verified_absent != bonds.verified_absent) {
        return enter_reconciliation(
            DeviceFactoryResetError::initial_state_incoherent);
    }

    intent_verified_ = false;
    marker_state_ = DeviceFactoryResetMarkerState::absent;
    reset_receipt_ = 0;
    phase_ = user.verified_absent
                 ? DeviceFactoryResetPhase::idle_unowned
                 : DeviceFactoryResetPhase::idle_old_state;
    return {DeviceFactoryResetError::none, phase_, 0};
}

DeviceFactoryResetResult DeviceFactoryResetExecutor::begin(
    std::uint64_t reset_receipt) {
    return commit_intent(DeviceFactoryResetPhase::idle_old_state,
                         reset_receipt);
}

DeviceFactoryResetResult
DeviceFactoryResetExecutor::begin_confirmed_recovery() {
    if (phase_ != DeviceFactoryResetPhase::not_restored &&
        phase_ != DeviceFactoryResetPhase::reconciliation_required) {
        return reject(DeviceFactoryResetError::invalid_state);
    }
    return commit_intent(phase_, 0);
}

DeviceFactoryResetResult DeviceFactoryResetExecutor::commit_intent(
    DeviceFactoryResetPhase required_phase,
    std::uint64_t reset_receipt) {
    if (operation_active_) {
        return reject(DeviceFactoryResetError::reentrant_call);
    }
    if (phase_ != required_phase ||
        (required_phase != DeviceFactoryResetPhase::idle_old_state &&
         required_phase != DeviceFactoryResetPhase::not_restored &&
         required_phase !=
             DeviceFactoryResetPhase::reconciliation_required)) {
        return reject(DeviceFactoryResetError::invalid_state);
    }

    const auto entry_phase = phase_;

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto committed =
        marker_.commit_intent_and_readback(reset_receipt);
    if (reentry_observed_) {
        return enter_reconciliation(DeviceFactoryResetError::reentrant_call);
    }

    if (committed.error == DeviceFactoryResetPortError::known_no_change &&
        committed.state == DeviceFactoryResetMarkerState::absent) {
        phase_ = entry_phase;
        intent_verified_ = false;
        marker_state_ = DeviceFactoryResetMarkerState::absent;
        reset_receipt_ = 0;
        return reject(
            DeviceFactoryResetError::marker_commit_known_no_change);
    }
    if (committed.error == DeviceFactoryResetPortError::uncertain) {
        return enter_reconciliation(
            DeviceFactoryResetError::marker_commit_uncertain);
    }
    if (committed.error != DeviceFactoryResetPortError::none) {
        return enter_reconciliation(
            DeviceFactoryResetError::marker_commit_uncertain);
    }
    if (committed.state !=
            DeviceFactoryResetMarkerState::intent_committed ||
        committed.reset_receipt != reset_receipt) {
        return enter_reconciliation(
            DeviceFactoryResetError::marker_commit_invalid_readback);
    }

    phase_ = DeviceFactoryResetPhase::cleanup_required;
    intent_verified_ = true;
    marker_state_ = committed.state;
    reset_receipt_ = committed.reset_receipt;
    return {DeviceFactoryResetError::none, phase_, reset_receipt_};
}

DeviceFactoryResetResult DeviceFactoryResetExecutor::continue_cleanup() {
    if (operation_active_) {
        return reject(DeviceFactoryResetError::reentrant_call);
    }
    if (phase_ != DeviceFactoryResetPhase::cleanup_required ||
        !intent_verified_) {
        return reject(DeviceFactoryResetError::invalid_state);
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);

    const auto user = user_domain_.erase_all_and_verify_absent();
    if (reentry_observed_) {
        return require_cleanup(DeviceFactoryResetError::reentrant_call);
    }
    if (access_failed(user.error)) {
        return require_cleanup(
            DeviceFactoryResetError::user_domain_erase_failed);
    }
    if (!user.verified_absent) {
        return require_cleanup(
            DeviceFactoryResetError::user_domain_not_absent);
    }

    const auto bonds = bond_domain_.erase_all_and_verify_empty();
    if (reentry_observed_) {
        return require_cleanup(DeviceFactoryResetError::reentrant_call);
    }
    if (access_failed(bonds.error)) {
        return require_cleanup(DeviceFactoryResetError::bond_erase_failed);
    }
    if (!bonds.verified_absent) {
        return require_cleanup(
            DeviceFactoryResetError::bond_inventory_not_empty);
    }

    const auto completed = marker_.complete_cleanup_and_readback();
    if (reentry_observed_) {
        return require_cleanup(DeviceFactoryResetError::reentrant_call);
    }
    if (access_failed(completed.error)) {
        return require_cleanup(
            DeviceFactoryResetError::marker_completion_failed);
    }
    const auto expected_state =
        reset_receipt_ == 0
            ? DeviceFactoryResetMarkerState::absent
            : DeviceFactoryResetMarkerState::receipt_pending;
    if (completed.state != expected_state ||
        completed.reset_receipt != reset_receipt_) {
        return require_cleanup(
            DeviceFactoryResetError::marker_completion_invalid_readback);
    }

    phase_ = DeviceFactoryResetPhase::reboot_unowned_permitted;
    intent_verified_ = false;
    marker_state_ = completed.state;
    return {DeviceFactoryResetError::none, phase_, reset_receipt_};
}

DeviceFactoryResetResult
DeviceFactoryResetExecutor::consume_completion_receipt() {
    if (operation_active_) {
        return reject(DeviceFactoryResetError::reentrant_call);
    }
    if (phase_ != DeviceFactoryResetPhase::completion_receipt_pending ||
        marker_state_ != DeviceFactoryResetMarkerState::receipt_pending ||
        reset_receipt_ == 0) {
        return reject(DeviceFactoryResetError::invalid_state);
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto consumed =
        marker_.consume_completion_receipt_and_readback();
    if (reentry_observed_) {
        return enter_reconciliation(DeviceFactoryResetError::reentrant_call);
    }
    if (access_failed(consumed.error)) {
        return enter_reconciliation(
            DeviceFactoryResetError::receipt_consume_failed);
    }
    if (!consumed.marker_verified_absent ||
        consumed.reset_receipt != reset_receipt_) {
        return enter_reconciliation(
            DeviceFactoryResetError::receipt_consume_invalid_readback);
    }

    const auto receipt = reset_receipt_;
    phase_ = DeviceFactoryResetPhase::idle_unowned;
    marker_state_ = DeviceFactoryResetMarkerState::absent;
    intent_verified_ = false;
    reset_receipt_ = 0;
    return {DeviceFactoryResetError::none, phase_, receipt};
}

DeviceFactoryResetStatus DeviceFactoryResetExecutor::status() const {
    return {
        phase_,
        intent_verified_,
        phase_ == DeviceFactoryResetPhase::idle_old_state,
        phase_ == DeviceFactoryResetPhase::cleanup_required,
        phase_ == DeviceFactoryResetPhase::reboot_unowned_permitted,
        phase_ == DeviceFactoryResetPhase::completion_receipt_pending,
        reset_receipt_,
    };
}

}  // namespace opentrail::companion
