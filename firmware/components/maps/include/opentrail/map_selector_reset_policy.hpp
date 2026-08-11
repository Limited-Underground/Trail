#pragma once

#include <cstdint>

namespace opentrail::maps {

// This is a classification input, not proof of physical device continuity.
// Target composition must derive it from independently reviewed service
// evidence before asking the common policy for a route.
enum class MapSelectorLifecycleState : std::uint8_t {
    unknown = 0,
    same_device_state_intact,
    same_device_source_unavailable,
    same_device_source_missing_or_replaced,
    new_device_unprovisioned,
    new_device_with_retained_selector,
};

enum class MapSelectorResetRequest : std::uint8_t {
    unknown = 0,
    ordinary_factory_reset,
    selector_service_reseed,
    protected_source_replacement,
    whole_device_replacement,
};

enum class MapSelectorResetPolicyState : std::uint8_t {
    blocked = 0,
    preserve_map_state,
    authorized_selector_service_required,
    external_recovery_required,
    new_device_provisioning_required,
};

enum class MapSelectorResetPolicyReason : std::uint8_t {
    none = 0,
    invalid_request,
    invalid_lifecycle_state,
    protected_source_unavailable,
    same_device_history_missing,
    protected_source_replacement_not_needed,
    device_continuity_mismatch,
    retained_selector_import_forbidden,
};

enum class MapSelectorResetPolicyAction : std::uint8_t {
    none = 0,
    preserve_selector_and_trusted_history,
    use_authorized_selector_reseed,
    use_external_same_device_recovery,
    commission_fresh_device_domain,
};

struct MapSelectorResetPolicyResult {
    MapSelectorResetPolicyState state{MapSelectorResetPolicyState::blocked};
    MapSelectorResetPolicyReason reason{
        MapSelectorResetPolicyReason::invalid_request};
    MapSelectorResetPolicyAction action{MapSelectorResetPolicyAction::none};
    bool ordinary_factory_reset_allowed{false};
    bool selector_reseed_permit_required{false};
    bool independent_authority_required{false};
    bool map_must_remain_unavailable{true};

    [[nodiscard]] constexpr bool routed() const {
        return state != MapSelectorResetPolicyState::blocked;
    }
};

// Pure lifecycle routing only. It performs no I/O and returns no erase,
// protected-reset, generation-lowering, credential, or state-import authority.
// In particular, ordinary factory reset is outside both map persistence
// domains and must preserve selector records plus protected history.
[[nodiscard]] MapSelectorResetPolicyResult classify_map_selector_reset(
    MapSelectorResetRequest request,
    MapSelectorLifecycleState lifecycle_state);

}  // namespace opentrail::maps
