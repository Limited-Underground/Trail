#include "opentrail/companion_factory_reset_authority.hpp"

namespace opentrail::companion {

CompanionFactoryResetActionAuthority::
    CompanionFactoryResetActionAuthority(
        DeviceFactoryResetExecutor& executor)
    : executor_(executor) {}

std::uint32_t CompanionFactoryResetActionAuthority::issue_token() {
    if (next_token_ == 0) {
        return 0;
    }
    const auto token = next_token_;
    ++next_token_;
    return token;
}

void CompanionFactoryResetActionAuthority::clear_prepared() {
    prepared_kind_ = CompanionActionKind::quick_status;
    prepared_disposition_ = CompanionActionDisposition::rejected;
    prepared_reason_ = CompanionActionRejectReason::internal_failure;
    prepared_reset_receipt_ = 0;
    prepared_token_ = 0;
    if (phase_ == CompanionFactoryResetAuthorityPhase::prepared) {
        phase_ = CompanionFactoryResetAuthorityPhase::idle;
    }
}

CompanionActionAuthorityResult
CompanionFactoryResetActionAuthority::prepare_action(
    const CompanionActionRequest& request) {
    if (phase_ != CompanionFactoryResetAuthorityPhase::idle) {
        return {CompanionAuthorityError::not_ready,
                CompanionActionDisposition::rejected,
                CompanionActionRejectReason::unavailable, 0};
    }

    const auto token = issue_token();
    if (token == 0) {
        return {CompanionAuthorityError::failed,
                CompanionActionDisposition::rejected,
                CompanionActionRejectReason::internal_failure, 0};
    }

    prepared_kind_ = request.kind;
    prepared_reset_receipt_ = request.critical_alert_id;
    prepared_token_ = token;
    phase_ = CompanionFactoryResetAuthorityPhase::prepared;
    if (request.kind != CompanionActionKind::factory_reset) {
        prepared_disposition_ = CompanionActionDisposition::rejected;
        prepared_reason_ = CompanionActionRejectReason::unsupported_action;
        return {CompanionAuthorityError::none, prepared_disposition_,
                prepared_reason_, token};
    }
    if (request.critical_alert_id == 0) {
        prepared_disposition_ = CompanionActionDisposition::rejected;
        prepared_reason_ = CompanionActionRejectReason::policy_denied;
        return {CompanionAuthorityError::none, prepared_disposition_,
                prepared_reason_, token};
    }

    const auto executor = executor_.status();
    if (executor.phase != DeviceFactoryResetPhase::idle_old_state ||
        !executor.old_state_preserved) {
        prepared_disposition_ = CompanionActionDisposition::rejected;
        prepared_reason_ = CompanionActionRejectReason::unavailable;
        return {CompanionAuthorityError::none, prepared_disposition_,
                prepared_reason_, token};
    }

    prepared_disposition_ = CompanionActionDisposition::admitted;
    prepared_reason_ = CompanionActionRejectReason::none;
    return {CompanionAuthorityError::none, prepared_disposition_,
            prepared_reason_, token};
}

CompanionAuthorityError
CompanionFactoryResetActionAuthority::commit_action(
    const CompanionActionRequest& request,
    const CompanionActionAuthorityResult& prepared) {
    if (phase_ != CompanionFactoryResetAuthorityPhase::prepared ||
        prepared_token_ == 0 ||
        prepared.operation_token != prepared_token_ ||
        request.kind != prepared_kind_ ||
        request.critical_alert_id != prepared_reset_receipt_ ||
        prepared.disposition != prepared_disposition_ ||
        prepared.reject_reason != prepared_reason_) {
        clear_prepared();
        return CompanionAuthorityError::failed;
    }

    if (prepared_disposition_ == CompanionActionDisposition::rejected) {
        clear_prepared();
        return CompanionAuthorityError::none;
    }
    if (request.kind != CompanionActionKind::factory_reset ||
        prepared_disposition_ != CompanionActionDisposition::admitted ||
        prepared_reason_ != CompanionActionRejectReason::none) {
        clear_prepared();
        return CompanionAuthorityError::failed;
    }

    prepared_token_ = 0;
    const auto result = executor_.begin(request.critical_alert_id);
    if (result.accepted() &&
        result.phase == DeviceFactoryResetPhase::cleanup_required) {
        phase_ = CompanionFactoryResetAuthorityPhase::intent_committed;
        return CompanionAuthorityError::none;
    }
    if (result.error ==
            DeviceFactoryResetError::marker_commit_known_no_change &&
        result.phase == DeviceFactoryResetPhase::idle_old_state) {
        phase_ = CompanionFactoryResetAuthorityPhase::idle;
        return CompanionAuthorityError::failed;
    }

    phase_ = CompanionFactoryResetAuthorityPhase::outcome_unknown;
    return CompanionAuthorityError::outcome_unknown;
}

CompanionFactoryResetAuthorityStatus
CompanionFactoryResetActionAuthority::status() const {
    const bool committed =
        phase_ == CompanionFactoryResetAuthorityPhase::intent_committed;
    return {
        phase_,
        committed,
        committed ||
            phase_ == CompanionFactoryResetAuthorityPhase::outcome_unknown,
    };
}

}  // namespace opentrail::companion
