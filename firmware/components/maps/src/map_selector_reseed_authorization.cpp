#include "opentrail/map_selector_reseed_authorization.hpp"

namespace opentrail::maps {
namespace {

bool policy_equal(
    const MapActivationPolicy& left,
    const MapActivationPolicy& right) {
    return left.maximum_package_bytes == right.maximum_package_bytes &&
           left.trial_deadline_ms == right.trial_deadline_ms &&
           left.required_healthy_reads == right.required_healthy_reads &&
           left.maximum_trial_boots == right.maximum_trial_boots;
}

bool package_equal(
    const MapPackageEvidence& left,
    const MapPackageEvidence& right) {
    return left.slot == right.slot && left.generation == right.generation &&
           left.package_bytes == right.package_bytes &&
           left.manifest_valid == right.manifest_valid &&
           left.rights_permitted == right.rights_permitted &&
           left.attribution_available == right.attribution_available &&
           left.integrity_verified == right.integrity_verified &&
           left.reader_compatible == right.reader_compatible &&
           left.index_readable == right.index_readable &&
           left.storage_sufficient == right.storage_sufficient &&
           left.read_only_capable == right.read_only_capable;
}

bool binding_equal(
    const MapSelectorReseedAuthorizationBinding& left,
    const MapSelectorReseedAuthorizationBinding& right) {
    return policy_equal(left.policy, right.policy) &&
           left.trusted_minimum_generation ==
               right.trusted_minimum_generation &&
           package_equal(left.baseline, right.baseline);
}

bool valid_policy(const MapSelectorReseedAuthorizationPolicy& policy) {
    return policy.maximum_grant_lifetime_ms != 0 &&
           policy.maximum_grant_lifetime_ms <=
               kMapSelectorReseedMaximumAuthorizationLifetimeMs;
}

bool valid_activation_policy(const MapActivationPolicy& policy) {
    return policy.maximum_package_bytes != 0 &&
           policy.trial_deadline_ms != 0 &&
           policy.required_healthy_reads != 0 &&
           policy.maximum_trial_boots != 0;
}

bool valid_package(
    const MapPackageEvidence& package,
    const MapActivationPolicy& policy) {
    return (package.slot == MapSlot::slot_a ||
            package.slot == MapSlot::slot_b) &&
           package.generation != 0 && package.package_bytes != 0 &&
           package.package_bytes <= policy.maximum_package_bytes &&
           package.manifest_valid && package.rights_permitted &&
           package.attribution_available && package.integrity_verified &&
           package.reader_compatible && package.index_readable &&
           package.storage_sufficient && package.read_only_capable;
}

bool valid_binding(const MapSelectorReseedAuthorizationBinding& binding) {
    return valid_activation_policy(binding.policy) &&
           valid_package(binding.baseline, binding.policy);
}

bool local_transport(MapSelectorReseedServiceTransport transport) {
    return transport == MapSelectorReseedServiceTransport::local_usb ||
           transport == MapSelectorReseedServiceTransport::
                            authenticated_local_wireless;
}

}  // namespace

MapSelectorReseedPermit::MapSelectorReseedPermit(
    const MapSelectorReseedAuthorizationBinding& binding,
    std::uint64_t boot_session_id,
    std::uint64_t issued_at_ms,
    std::uint64_t expires_at_ms)
    : binding_(binding),
      boot_session_id_(boot_session_id),
      issued_at_ms_(issued_at_ms),
      expires_at_ms_(expires_at_ms),
      granted_(true) {}

MapSelectorReseedPermit::MapSelectorReseedPermit(
    MapSelectorReseedPermit&& other) noexcept
    : binding_(other.binding_),
      boot_session_id_(other.boot_session_id_),
      issued_at_ms_(other.issued_at_ms_),
      expires_at_ms_(other.expires_at_ms_),
      granted_(other.granted_),
      consumed_(other.consumed_) {
    other.binding_ = {};
    other.boot_session_id_ = 0;
    other.issued_at_ms_ = 0;
    other.expires_at_ms_ = 0;
    other.granted_ = false;
    other.consumed_ = true;
}

MapSelectorReseedPermit& MapSelectorReseedPermit::operator=(
    MapSelectorReseedPermit&& other) noexcept {
    if (this != &other) {
        binding_ = other.binding_;
        boot_session_id_ = other.boot_session_id_;
        issued_at_ms_ = other.issued_at_ms_;
        expires_at_ms_ = other.expires_at_ms_;
        granted_ = other.granted_;
        consumed_ = other.consumed_;
        other.binding_ = {};
        other.boot_session_id_ = 0;
        other.issued_at_ms_ = 0;
        other.expires_at_ms_ = 0;
        other.granted_ = false;
        other.consumed_ = true;
    }
    return *this;
}

MapSelectorReseedPermitUse MapSelectorReseedPermit::consume(
    const MapSelectorReseedAuthorizationBinding& expected_binding,
    std::uint64_t boot_session_id,
    std::uint64_t now_ms) {
    if (consumed_) {
        return MapSelectorReseedPermitUse::already_consumed;
    }
    if (!granted_) {
        return MapSelectorReseedPermitUse::unavailable;
    }

    consumed_ = true;
    granted_ = false;
    if (!binding_equal(binding_, expected_binding)) {
        return MapSelectorReseedPermitUse::binding_mismatch;
    }
    if (boot_session_id == 0 || boot_session_id != boot_session_id_) {
        return MapSelectorReseedPermitUse::boot_session_mismatch;
    }
    if (now_ms < issued_at_ms_) {
        return MapSelectorReseedPermitUse::not_yet_valid;
    }
    return now_ms < expires_at_ms_ ? MapSelectorReseedPermitUse::none
                                   : MapSelectorReseedPermitUse::expired;
}

void MapSelectorReseedPermit::invalidate() {
    binding_ = {};
    boot_session_id_ = 0;
    issued_at_ms_ = 0;
    expires_at_ms_ = 0;
    granted_ = false;
    consumed_ = false;
}

MapSelectorReseedAuthorizer::MapSelectorReseedAuthorizer(
    MapSelectorReseedAuthorizationBackend& backend)
    : backend_(backend) {}

MapSelectorReseedAuthorizationResult MapSelectorReseedAuthorizer::authorize(
    const MapSelectorReseedAuthorizationPolicy& policy,
    const MapSelectorReseedAuthorizationRequest& request,
    const MapSelectorReseedAuthorizationBinding& binding,
    MapSelectorReseedPermit& output_permit) {
    output_permit.invalidate();
    MapSelectorReseedAuthorizationResult result{};

    if (!valid_policy(policy)) {
        result.error = MapSelectorReseedAuthorizationError::invalid_policy;
        return result;
    }
    if (request.authorization_handle == 0 || request.boot_session_id == 0) {
        result.error = MapSelectorReseedAuthorizationError::invalid_request;
        return result;
    }
    if (!valid_binding(binding)) {
        result.error = MapSelectorReseedAuthorizationError::invalid_binding;
        return result;
    }

    const auto grant = backend_.verify_and_consume(
        request.authorization_handle, binding);
    result.backend_called = true;
    result.backend_state = grant.state;

    switch (grant.state) {
        case MapSelectorReseedAuthorizationBackendState::not_ready:
            result.error =
                MapSelectorReseedAuthorizationError::backend_not_ready;
            return result;
        case MapSelectorReseedAuthorizationBackendState::failed:
            result.error = MapSelectorReseedAuthorizationError::backend_failed;
            return result;
        case MapSelectorReseedAuthorizationBackendState::denied:
            result.error = MapSelectorReseedAuthorizationError::denied;
            return result;
        case MapSelectorReseedAuthorizationBackendState::authorized:
            break;
        default:
            result.error = MapSelectorReseedAuthorizationError::backend_failed;
            return result;
    }

    if (grant.authorization_handle != request.authorization_handle) {
        result.error = MapSelectorReseedAuthorizationError::handle_mismatch;
        return result;
    }
    if (grant.scope !=
        MapSelectorReseedAuthorizationScope::selector_reseed) {
        result.error = MapSelectorReseedAuthorizationError::scope_mismatch;
        return result;
    }
    if (!local_transport(grant.transport)) {
        result.error =
            MapSelectorReseedAuthorizationError::transport_not_local;
        return result;
    }
    if (grant.boot_session_id != request.boot_session_id) {
        result.error =
            MapSelectorReseedAuthorizationError::boot_session_mismatch;
        return result;
    }
    if (!binding_equal(grant.binding, binding)) {
        result.error = MapSelectorReseedAuthorizationError::binding_mismatch;
        return result;
    }
    if (!grant.acknowledgements.complete()) {
        result.error = MapSelectorReseedAuthorizationError::intent_incomplete;
        return result;
    }
    if (grant.local_confirmation_revision == 0) {
        result.error =
            MapSelectorReseedAuthorizationError::local_confirmation_missing;
        return result;
    }
    if (grant.expires_at_ms <= grant.issued_at_ms) {
        result.error =
            MapSelectorReseedAuthorizationError::invalid_time_window;
        return result;
    }

    const auto lifetime = grant.expires_at_ms - grant.issued_at_ms;
    if (lifetime > policy.maximum_grant_lifetime_ms ||
        lifetime > kMapSelectorReseedMaximumAuthorizationLifetimeMs) {
        result.error =
            MapSelectorReseedAuthorizationError::lifetime_exceeded;
        return result;
    }
    if (request.now_ms < grant.issued_at_ms) {
        result.error = MapSelectorReseedAuthorizationError::not_yet_valid;
        return result;
    }
    if (request.now_ms >= grant.expires_at_ms) {
        result.error = MapSelectorReseedAuthorizationError::expired;
        return result;
    }

    output_permit = MapSelectorReseedPermit{
        binding,
        grant.boot_session_id,
        grant.issued_at_ms,
        grant.expires_at_ms};
    result.error = MapSelectorReseedAuthorizationError::none;
    return result;
}

}  // namespace opentrail::maps
