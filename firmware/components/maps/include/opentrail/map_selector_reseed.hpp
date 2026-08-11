#pragma once

#include <cstdint>

#include "opentrail/map_selector_reseed_authorization.hpp"
#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

enum class MapSelectorReseedState : std::uint8_t {
    rejected = 0,
    committed,
    reconciliation_required,
    service_required,
};

enum class MapSelectorReseedReason : std::uint8_t {
    none = 0,
    authorization_required,
    authorization_already_consumed,
    authorization_binding_mismatch,
    authorization_boot_session_mismatch,
    authorization_not_yet_valid,
    authorization_expired,
    live_guard_not_running,
    mapless_service_required,
    invalid_policy,
    candidate_rejected,
    first_use_requires_baseline,
    storage_unavailable,
    generation_exhausted,
    selector_reset_failed,
    selector_reset_unverified,
    selector_changed,
    checkpoint_save_failed,
    checkpoint_commit_uncertain,
    checkpoint_verification_failed,
};

struct MapSelectorReseedContext {
    MapActivationPolicy policy{};
    std::uint64_t trusted_minimum_generation{0};
    std::uint64_t boot_session_id{0};
    std::uint64_t authorization_use_time_ms{0};
};

struct MapSelectorReseedResult {
    MapSelectorReseedState state{MapSelectorReseedState::rejected};
    MapSelectorReseedReason reason{
        MapSelectorReseedReason::authorization_required};
    MapSelectorInspectionResult inspection{};
    MapSelectorResetResult reset{};
    MapSelectorSaveResult save{};
    MapActivationError candidate_error{MapActivationError::none};
    MapActivationState before_state{MapActivationState::stopped};
    MapActivationState live_state{MapActivationState::stopped};
    MapSlot baseline_slot{MapSlot::none};
    std::uint64_t baseline_package_generation{0};
    std::uint64_t prior_observed_record_generation{0};
    std::uint64_t generation_base{0};
    std::uint64_t active_record_generation{0};
    bool map_exposure_allowed{false};
    bool live_guard_mapless{false};
    bool selector_clear_verified{false};
    bool authorization_consumed{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == MapSelectorReseedState::committed && save.saved();
    }
};

// Replaces a dirty or previously used selector domain from an already-mapless
// service state. It clears only the two abstract selector records, verifies
// emptiness, then commits a stable baseline above observed/trusted history
// before map exposure. Package bytes are never erased or modified here.
// The caller must provide a fresh exact-operation permit minted by the
// service-authorizer boundary and exclusive ownership of the selector store
// throughout reseed(). The permit is consumed before any store access.
class MapSelectorReseedCoordinator {
public:
    explicit MapSelectorReseedCoordinator(MapSelectorStore& store);

    [[nodiscard]] MapSelectorReseedResult reseed(
        MapActivationGuard& live_guard,
        const MapSelectorReseedContext& context,
        const MapPackageEvidence& baseline,
        MapSelectorReseedPermit& permit);

private:
    MapSelectorStore& store_;
};

}  // namespace opentrail::maps
