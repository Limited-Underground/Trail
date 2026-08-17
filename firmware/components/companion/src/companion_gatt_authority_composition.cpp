#include "opentrail/companion_gatt_authority_composition.hpp"

namespace opentrail::companion {
namespace {

class ScopedOperation {
public:
    explicit ScopedOperation(bool& active) : active_(active) {
        active_ = true;
    }
    ~ScopedOperation() { active_ = false; }

    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

CompanionGattTrustedBindingResult binding_failure(
    CompanionGattTrustedBindingError error) {
    return {error, {}, 0};
}

CompanionGattAuthorizationDecision authority_failure(
    CompanionGattAuthorizationAuthorityError error) {
    return {error, CompanionAuthorizationClaimOutcome::denied,
            CompanionAuthorizationDenyReason::unknown, 0};
}

CompanionGattAuthorizationDecision authority_denial(
    CompanionAuthorizationDenyReason reason) {
    return {CompanionGattAuthorizationAuthorityError::none,
            CompanionAuthorizationClaimOutcome::denied, reason, 0};
}

CompanionAuthorizationDenyReason denial_reason(
    CompanionAuthorizationError error) {
    switch (error) {
        case CompanionAuthorizationError::physical_presence_required:
            return CompanionAuthorizationDenyReason::
                physical_presence_required;
        case CompanionAuthorizationError::physical_presence_expired:
            return CompanionAuthorizationDenyReason::physical_presence_expired;
        case CompanionAuthorizationError::owner_not_claimed:
        case CompanionAuthorizationError::owner_already_claimed:
        case CompanionAuthorizationError::owner_mismatch:
        case CompanionAuthorizationError::same_owner_replacement:
        case CompanionAuthorizationError::controller_in_use:
            return CompanionAuthorizationDenyReason::owner_state_conflict;
        case CompanionAuthorizationError::persistence_not_ready:
        case CompanionAuthorizationError::persistence_failed:
        case CompanionAuthorizationError::persistence_uncertain:
        case CompanionAuthorizationError::persistence_conflict:
        case CompanionAuthorizationError::persistence_record_invalid:
        case CompanionAuthorizationError::persistence_rollback:
        case CompanionAuthorizationError::generation_exhausted:
            return CompanionAuthorizationDenyReason::persistence_unavailable;
        case CompanionAuthorizationError::physical_presence_mismatch:
        case CompanionAuthorizationError::physical_event_replayed:
            return CompanionAuthorizationDenyReason::policy_denied;
        default:
            return CompanionAuthorizationDenyReason::internal_failure;
    }
}

bool accepted_for_authorize(const CompanionAuthorizationResult& result) {
    return result.disposition == CompanionAuthorizationDisposition::claimed ||
           result.disposition ==
               CompanionAuthorizationDisposition::authorized ||
           result.disposition ==
               CompanionAuthorizationDisposition::duplicate_authorization;
}

bool same_reference(const CompanionPrivateBondReference& left,
                    const CompanionPrivateBondReference& right) {
    return left.bond_generation == right.bond_generation &&
           left.value == right.value;
}

}  // namespace

PersistentCompanionGattTrustedBindingAuthority::
    PersistentCompanionGattTrustedBindingAuthority(
        CompanionGattPrivateBondReferenceSource& reference_source,
        CompanionBondBindingResolver& binding_resolver,
        CompanionGattPrivateSessionIssuer& session_issuer)
    : reference_source_(reference_source),
      binding_resolver_(binding_resolver),
      session_issuer_(session_issuer) {}

CompanionGattTrustedBindingResult
PersistentCompanionGattTrustedBindingAuthority::resolve(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation) {
    if (operation_active_) {
        reentry_observed_ = true;
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }
    if (connection_handle == kCompanionGattInvalidConnectionHandle ||
        transport_generation == 0) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    if (context_seen_) {
        if (transport_generation < transport_generation_ ||
            (transport_generation == transport_generation_ &&
             connection_handle != connection_handle_)) {
            return binding_failure(CompanionGattTrustedBindingError::failed);
        }
        if (transport_generation == transport_generation_ && cached_) {
            return cached_result_;
        }
    }
    if (!context_seen_ || transport_generation > transport_generation_) {
        context_seen_ = true;
        reference_seen_ = false;
        cached_ = false;
        reference_ = {};
        cached_result_ = {};
        connection_handle_ = connection_handle;
        transport_generation_ = transport_generation;
    }

    const auto reference =
        reference_source_.resolve(connection_handle_, transport_generation_);
    if (reentry_observed_) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }
    if (reference.error == CompanionGattPrivateBondReferenceError::not_ready) {
        return binding_failure(CompanionGattTrustedBindingError::not_ready);
    }
    if (reference.error != CompanionGattPrivateBondReferenceError::none) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }
    if (reference_seen_ && !same_reference(reference.reference, reference_)) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }
    if (!reference_seen_) {
        reference_ = reference.reference;
        reference_seen_ = true;
    }

    const auto binding = binding_resolver_.resolve(reference_);
    if (reentry_observed_) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }
    if (binding.error == CompanionBondBindingError::not_ready) {
        return binding_failure(CompanionGattTrustedBindingError::not_ready);
    }
    if (!binding.resolved()) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }

    const CompanionGattPrivateSessionContext context{
        connection_handle_, transport_generation_, binding.token};
    const auto session = session_issuer_.issue(context);
    if (reentry_observed_) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }
    if (session.error == CompanionGattPrivateSessionError::not_ready) {
        return binding_failure(CompanionGattTrustedBindingError::not_ready);
    }
    if (session.error != CompanionGattPrivateSessionError::none ||
        session.boot_challenge == 0 || session.session_challenge == 0 ||
        session.controller_binding == 0 ||
        session.provisional_session_nonce == 0) {
        return binding_failure(CompanionGattTrustedBindingError::failed);
    }

    CompanionControllerClaim claim{};
    claim.bond_identity = binding.token;
    claim.boot_challenge = session.boot_challenge;
    claim.session_challenge = session.session_challenge;
    claim.controller_binding = session.controller_binding;
    cached_result_ = {CompanionGattTrustedBindingError::none, claim,
                      session.provisional_session_nonce};
    cached_ = true;
    return cached_result_;
}

PersistentCompanionGattAuthorizationAuthority::
    PersistentCompanionGattAuthorizationAuthority(
        CompanionAuthorizationAuthority& authority)
    : authority_(authority) {}

CompanionGattAuthorizationDecision
PersistentCompanionGattAuthorizationAuthority::apply_claim(
    CompanionAuthorizationPurpose purpose,
    const CompanionControllerClaim& claim,
    std::uint64_t now_ms) {
    if (operation_active_) {
        reentry_observed_ = true;
        return authority_failure(
            CompanionGattAuthorizationAuthorityError::failed);
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);

    const auto status = authority_.status(now_ms);
    if (!status.restored) {
        return authority_failure(
            status.faulted ? CompanionGattAuthorizationAuthorityError::failed
                           : CompanionGattAuthorizationAuthorityError::not_ready);
    }
    if (status.faulted) {
        return authority_failure(
            CompanionGattAuthorizationAuthorityError::failed);
    }

    CompanionAuthorizationResult result{};
    switch (purpose) {
        case CompanionAuthorizationPurpose::authorize_controller:
            result = status.owner_present
                         ? authority_.authorize_connection(claim, now_ms)
                         : authority_.claim_owner(claim, now_ms);
            break;
        case CompanionAuthorizationPurpose::replace_controller:
            result = authority_.replace_owner(claim, now_ms);
            break;
        default:
            return authority_failure(
                CompanionGattAuthorizationAuthorityError::failed);
    }
    if (reentry_observed_) {
        if (result.accepted() && result.controller_binding != 0) {
            (void)authority_.release_connection(result.controller_binding);
        }
        return authority_failure(
            CompanionGattAuthorizationAuthorityError::failed);
    }
    if (!result.accepted()) {
        if (result.error == CompanionAuthorizationError::persistence_not_ready ||
            result.error == CompanionAuthorizationError::not_restored) {
            return authority_failure(
                CompanionGattAuthorizationAuthorityError::not_ready);
        }
        return authority_denial(denial_reason(result.error));
    }
    if (result.controller_binding == 0 ||
        result.controller_binding != claim.controller_binding) {
        if (result.controller_binding != 0) {
            (void)authority_.release_connection(result.controller_binding);
        }
        return authority_failure(
            CompanionGattAuthorizationAuthorityError::failed);
    }
    if (purpose == CompanionAuthorizationPurpose::authorize_controller &&
        accepted_for_authorize(result)) {
        return {CompanionGattAuthorizationAuthorityError::none,
                CompanionAuthorizationClaimOutcome::accepted,
                CompanionAuthorizationDenyReason::none,
                result.controller_binding};
    }
    if (purpose == CompanionAuthorizationPurpose::replace_controller &&
        result.disposition == CompanionAuthorizationDisposition::replaced) {
        return {CompanionGattAuthorizationAuthorityError::none,
                CompanionAuthorizationClaimOutcome::replaced,
                CompanionAuthorizationDenyReason::none,
                result.controller_binding};
    }
    (void)authority_.release_connection(result.controller_binding);
    return authority_failure(CompanionGattAuthorizationAuthorityError::failed);
}

CompanionGattAuthorizationAuthorityError
PersistentCompanionGattAuthorizationAuthority::release_connection(
    std::uint64_t controller_binding) {
    if (operation_active_) {
        reentry_observed_ = true;
        return CompanionGattAuthorizationAuthorityError::failed;
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto result = authority_.release_connection(controller_binding);
    if (reentry_observed_ || !result.accepted() ||
        result.disposition != CompanionAuthorizationDisposition::released) {
        return CompanionGattAuthorizationAuthorityError::failed;
    }
    return CompanionGattAuthorizationAuthorityError::none;
}

}  // namespace opentrail::companion
