#include "opentrail/map_selector_domain_authorization.hpp"

namespace opentrail::maps {
namespace {

bool domain_nonzero(const std::array<std::uint8_t, 16>& domain) {
    for (const auto byte : domain) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

bool binding_equal(
    const MapSelectorDomainAuthorizationBinding& left,
    const MapSelectorDomainAuthorizationBinding& right) {
    return left.request == right.request &&
           left.lifecycle_state == right.lifecycle_state &&
           left.media_state == right.media_state &&
           left.reviewed_selector_generation ==
               right.reviewed_selector_generation &&
           left.proposed_domain == right.proposed_domain;
}

bool valid_policy(const MapSelectorDomainAuthorizationPolicy& policy) {
    return policy.maximum_grant_lifetime_ms != 0 &&
           policy.maximum_grant_lifetime_ms <=
               kMapSelectorDomainMaximumAuthorizationLifetimeMs;
}

MapSelectorDomainAuthorizationScope required_scope(
    const MapSelectorDomainAuthorizationBinding& binding) {
    const auto route = classify_map_selector_reset(
        binding.request, binding.lifecycle_state);
    if (route.state ==
            MapSelectorResetPolicyState::external_recovery_required &&
        route.action == MapSelectorResetPolicyAction::
                            use_external_same_device_recovery &&
        binding.request ==
            MapSelectorResetRequest::protected_source_replacement &&
        binding.lifecycle_state == MapSelectorLifecycleState::
                                       same_device_source_missing_or_replaced) {
        return MapSelectorDomainAuthorizationScope::
            replace_same_device_domain;
    }
    if (route.state == MapSelectorResetPolicyState::
                           new_device_provisioning_required &&
        route.action ==
            MapSelectorResetPolicyAction::commission_fresh_device_domain &&
        binding.request == MapSelectorResetRequest::whole_device_replacement &&
        binding.lifecycle_state ==
            MapSelectorLifecycleState::new_device_unprovisioned) {
        return MapSelectorDomainAuthorizationScope::
            commission_new_device_domain;
    }
    return MapSelectorDomainAuthorizationScope::none;
}

bool valid_media_binding(
    const MapSelectorDomainAuthorizationBinding& binding,
    MapSelectorDomainAuthorizationScope scope) {
    if (scope == MapSelectorDomainAuthorizationScope::
                     replace_same_device_domain) {
        return (binding.media_state ==
                    MapSelectorDomainMediaState::verified_empty &&
                binding.reviewed_selector_generation == 0) ||
               (binding.media_state ==
                    MapSelectorDomainMediaState::retained_quarantined &&
                binding.reviewed_selector_generation != 0);
    }
    if (scope == MapSelectorDomainAuthorizationScope::
                     commission_new_device_domain) {
        return binding.media_state ==
                   MapSelectorDomainMediaState::verified_empty &&
               binding.reviewed_selector_generation == 0;
    }
    return false;
}

bool valid_binding(
    const MapSelectorDomainAuthorizationBinding& binding,
    MapSelectorDomainAuthorizationScope& scope) {
    scope = required_scope(binding);
    return scope != MapSelectorDomainAuthorizationScope::none &&
           domain_nonzero(binding.proposed_domain) &&
           valid_media_binding(binding, scope);
}

}  // namespace

MapSelectorDomainAuthorizationPermit::
    MapSelectorDomainAuthorizationPermit(
        MapSelectorDomainAuthorizationScope scope,
        const MapSelectorDomainAuthorizationBinding& binding,
        std::uint64_t boot_session_id,
        std::uint64_t issued_at_ms,
        std::uint64_t expires_at_ms)
    : scope_(scope),
      binding_(binding),
      boot_session_id_(boot_session_id),
      issued_at_ms_(issued_at_ms),
      expires_at_ms_(expires_at_ms),
      granted_(true) {}

MapSelectorDomainAuthorizationPermit::
    MapSelectorDomainAuthorizationPermit(
        MapSelectorDomainAuthorizationPermit&& other) noexcept
    : scope_(other.scope_),
      binding_(other.binding_),
      boot_session_id_(other.boot_session_id_),
      issued_at_ms_(other.issued_at_ms_),
      expires_at_ms_(other.expires_at_ms_),
      granted_(other.granted_),
      consumed_(other.consumed_) {
    other.invalidate();
    other.consumed_ = true;
}

MapSelectorDomainAuthorizationPermit&
MapSelectorDomainAuthorizationPermit::operator=(
    MapSelectorDomainAuthorizationPermit&& other) noexcept {
    if (this != &other) {
        scope_ = other.scope_;
        binding_ = other.binding_;
        boot_session_id_ = other.boot_session_id_;
        issued_at_ms_ = other.issued_at_ms_;
        expires_at_ms_ = other.expires_at_ms_;
        granted_ = other.granted_;
        consumed_ = other.consumed_;
        other.invalidate();
        other.consumed_ = true;
    }
    return *this;
}

void MapSelectorDomainAuthorizationPermit::invalidate() {
    scope_ = MapSelectorDomainAuthorizationScope::none;
    binding_ = {};
    boot_session_id_ = 0;
    issued_at_ms_ = 0;
    expires_at_ms_ = 0;
    granted_ = false;
    consumed_ = false;
}

MapSelectorDomainAuthorizer::MapSelectorDomainAuthorizer(
    MapSelectorDomainAuthorizationBackend& backend)
    : backend_(backend) {}

MapSelectorDomainAuthorizationResult MapSelectorDomainAuthorizer::authorize(
    const MapSelectorDomainAuthorizationPolicy& policy,
    const MapSelectorDomainAuthorizationRequest& request,
    const MapSelectorDomainAuthorizationBinding& binding,
    MapSelectorDomainAuthorizationPermit& output_permit) {
    output_permit.invalidate();
    MapSelectorDomainAuthorizationResult result{};

    if (!valid_policy(policy)) {
        result.error = MapSelectorDomainAuthorizationError::invalid_policy;
        return result;
    }
    if (request.authorization_handle == 0 || request.boot_session_id == 0) {
        result.error = MapSelectorDomainAuthorizationError::invalid_request;
        return result;
    }

    MapSelectorDomainAuthorizationScope expected_scope{
        MapSelectorDomainAuthorizationScope::none};
    if (!valid_binding(binding, expected_scope)) {
        result.error =
            required_scope(binding) ==
                    MapSelectorDomainAuthorizationScope::none
                ? MapSelectorDomainAuthorizationError::invalid_route
                : MapSelectorDomainAuthorizationError::invalid_binding;
        return result;
    }
    result.required_scope = expected_scope;

    const auto grant = backend_.verify_and_consume(
        request.authorization_handle, binding);
    result.backend_called = true;
    result.backend_state = grant.state;

    switch (grant.state) {
        case MapSelectorDomainAuthorizationBackendState::not_ready:
            result.error =
                MapSelectorDomainAuthorizationError::backend_not_ready;
            return result;
        case MapSelectorDomainAuthorizationBackendState::failed:
            result.error = MapSelectorDomainAuthorizationError::backend_failed;
            return result;
        case MapSelectorDomainAuthorizationBackendState::denied:
            result.error = MapSelectorDomainAuthorizationError::denied;
            return result;
        case MapSelectorDomainAuthorizationBackendState::authorized:
            break;
        default:
            result.error = MapSelectorDomainAuthorizationError::backend_failed;
            return result;
    }

    if (grant.authorization_handle != request.authorization_handle) {
        result.error = MapSelectorDomainAuthorizationError::handle_mismatch;
        return result;
    }
    if (grant.scope != expected_scope) {
        result.error = MapSelectorDomainAuthorizationError::scope_mismatch;
        return result;
    }
    if (grant.transport != MapSelectorDomainServiceTransport::local_usb) {
        result.error =
            MapSelectorDomainAuthorizationError::transport_not_local_usb;
        return result;
    }
    if (grant.boot_session_id != request.boot_session_id) {
        result.error =
            MapSelectorDomainAuthorizationError::boot_session_mismatch;
        return result;
    }
    if (!binding_equal(grant.binding, binding)) {
        result.error = MapSelectorDomainAuthorizationError::binding_mismatch;
        return result;
    }
    if (!grant.acknowledgements.complete()) {
        result.error = MapSelectorDomainAuthorizationError::intent_incomplete;
        return result;
    }
    if (grant.local_confirmation_revision == 0) {
        result.error =
            MapSelectorDomainAuthorizationError::local_confirmation_missing;
        return result;
    }
    if (grant.expires_at_ms <= grant.issued_at_ms) {
        result.error =
            MapSelectorDomainAuthorizationError::invalid_time_window;
        return result;
    }

    const auto lifetime = grant.expires_at_ms - grant.issued_at_ms;
    if (lifetime > policy.maximum_grant_lifetime_ms ||
        lifetime > kMapSelectorDomainMaximumAuthorizationLifetimeMs) {
        result.error =
            MapSelectorDomainAuthorizationError::lifetime_exceeded;
        return result;
    }
    if (request.now_ms < grant.issued_at_ms) {
        result.error = MapSelectorDomainAuthorizationError::not_yet_valid;
        return result;
    }
    if (request.now_ms >= grant.expires_at_ms) {
        result.error = MapSelectorDomainAuthorizationError::expired;
        return result;
    }

    output_permit = MapSelectorDomainAuthorizationPermit{
        expected_scope,
        binding,
        grant.boot_session_id,
        grant.issued_at_ms,
        grant.expires_at_ms};
    result.error = MapSelectorDomainAuthorizationError::none;
    return result;
}

}  // namespace opentrail::maps
