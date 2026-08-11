#include "opentrail/map_selector_reset_policy.hpp"

namespace opentrail::maps {
namespace {

bool known_lifecycle_state(MapSelectorLifecycleState state) {
    switch (state) {
        case MapSelectorLifecycleState::unknown:
        case MapSelectorLifecycleState::same_device_state_intact:
        case MapSelectorLifecycleState::same_device_source_unavailable:
        case MapSelectorLifecycleState::same_device_source_missing_or_replaced:
        case MapSelectorLifecycleState::new_device_unprovisioned:
        case MapSelectorLifecycleState::new_device_with_retained_selector:
            return true;
    }
    return false;
}

bool known_request(MapSelectorResetRequest request) {
    switch (request) {
        case MapSelectorResetRequest::unknown:
        case MapSelectorResetRequest::ordinary_factory_reset:
        case MapSelectorResetRequest::selector_service_reseed:
        case MapSelectorResetRequest::protected_source_replacement:
        case MapSelectorResetRequest::whole_device_replacement:
            return true;
    }
    return false;
}

MapSelectorResetPolicyResult blocked(
    MapSelectorResetPolicyReason reason) {
    MapSelectorResetPolicyResult result{};
    result.reason = reason;
    return result;
}

MapSelectorResetPolicyResult external_recovery(
    MapSelectorResetPolicyReason reason) {
    MapSelectorResetPolicyResult result{};
    result.state = MapSelectorResetPolicyState::external_recovery_required;
    result.reason = reason;
    result.action =
        MapSelectorResetPolicyAction::use_external_same_device_recovery;
    result.independent_authority_required = true;
    return result;
}

}  // namespace

MapSelectorResetPolicyResult classify_map_selector_reset(
    MapSelectorResetRequest request,
    MapSelectorLifecycleState lifecycle_state) {
    if (!known_request(request) ||
        request == MapSelectorResetRequest::unknown) {
        return blocked(MapSelectorResetPolicyReason::invalid_request);
    }
    if (!known_lifecycle_state(lifecycle_state) ||
        lifecycle_state == MapSelectorLifecycleState::unknown) {
        return blocked(
            MapSelectorResetPolicyReason::invalid_lifecycle_state);
    }

    if (request == MapSelectorResetRequest::ordinary_factory_reset) {
        MapSelectorResetPolicyResult result{};
        result.state = MapSelectorResetPolicyState::preserve_map_state;
        result.reason = MapSelectorResetPolicyReason::none;
        result.action = MapSelectorResetPolicyAction::
            preserve_selector_and_trusted_history;
        result.ordinary_factory_reset_allowed = true;
        result.map_must_remain_unavailable =
            lifecycle_state !=
            MapSelectorLifecycleState::same_device_state_intact;
        return result;
    }

    if (request == MapSelectorResetRequest::selector_service_reseed) {
        switch (lifecycle_state) {
            case MapSelectorLifecycleState::same_device_state_intact: {
                MapSelectorResetPolicyResult result{};
                result.state = MapSelectorResetPolicyState::
                    authorized_selector_service_required;
                result.reason = MapSelectorResetPolicyReason::none;
                result.action = MapSelectorResetPolicyAction::
                    use_authorized_selector_reseed;
                result.selector_reseed_permit_required = true;
                return result;
            }
            case MapSelectorLifecycleState::
                same_device_source_unavailable:
                return blocked(MapSelectorResetPolicyReason::
                                   protected_source_unavailable);
            case MapSelectorLifecycleState::
                same_device_source_missing_or_replaced:
                return external_recovery(MapSelectorResetPolicyReason::
                                             same_device_history_missing);
            case MapSelectorLifecycleState::new_device_unprovisioned:
            case MapSelectorLifecycleState::
                new_device_with_retained_selector:
                return blocked(MapSelectorResetPolicyReason::
                                   device_continuity_mismatch);
            case MapSelectorLifecycleState::unknown:
                break;
        }
    }

    if (request == MapSelectorResetRequest::protected_source_replacement) {
        switch (lifecycle_state) {
            case MapSelectorLifecycleState::same_device_state_intact:
                return blocked(MapSelectorResetPolicyReason::
                                   protected_source_replacement_not_needed);
            case MapSelectorLifecycleState::
                same_device_source_unavailable:
                return external_recovery(MapSelectorResetPolicyReason::
                                             protected_source_unavailable);
            case MapSelectorLifecycleState::
                same_device_source_missing_or_replaced:
                return external_recovery(MapSelectorResetPolicyReason::
                                             same_device_history_missing);
            case MapSelectorLifecycleState::new_device_unprovisioned:
            case MapSelectorLifecycleState::
                new_device_with_retained_selector:
                return blocked(MapSelectorResetPolicyReason::
                                   device_continuity_mismatch);
            case MapSelectorLifecycleState::unknown:
                break;
        }
    }

    if (request == MapSelectorResetRequest::whole_device_replacement) {
        switch (lifecycle_state) {
            case MapSelectorLifecycleState::new_device_unprovisioned: {
                MapSelectorResetPolicyResult result{};
                result.state = MapSelectorResetPolicyState::
                    new_device_provisioning_required;
                result.reason = MapSelectorResetPolicyReason::none;
                result.action = MapSelectorResetPolicyAction::
                    commission_fresh_device_domain;
                result.independent_authority_required = true;
                return result;
            }
            case MapSelectorLifecycleState::
                new_device_with_retained_selector:
                return blocked(MapSelectorResetPolicyReason::
                                   retained_selector_import_forbidden);
            case MapSelectorLifecycleState::same_device_state_intact:
            case MapSelectorLifecycleState::
                same_device_source_unavailable:
            case MapSelectorLifecycleState::
                same_device_source_missing_or_replaced:
                return blocked(MapSelectorResetPolicyReason::
                                   device_continuity_mismatch);
            case MapSelectorLifecycleState::unknown:
                break;
        }
    }

    return blocked(MapSelectorResetPolicyReason::invalid_request);
}

}  // namespace opentrail::maps
