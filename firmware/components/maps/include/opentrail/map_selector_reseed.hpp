#pragma once

#include <cstdint>

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

// These acknowledgements are caller-supplied service intent evidence. They are
// not authentication, durable consent, or proof of an operator's identity.
struct MapSelectorReseedAuthorizationEvidence {
    bool explicit_operator_confirmation{false};
    bool map_unavailability_acknowledged{false};
    bool selector_only_scope_confirmed{false};
    bool package_retention_confirmed{false};
    bool trusted_generation_reviewed{false};

    [[nodiscard]] constexpr bool complete() const {
        return explicit_operator_confirmation &&
               map_unavailability_acknowledged &&
               selector_only_scope_confirmed &&
               package_retention_confirmed &&
               trusted_generation_reviewed;
    }
};

struct MapSelectorReseedContext {
    MapActivationPolicy policy{};
    std::uint64_t trusted_minimum_generation{0};
    MapSelectorReseedAuthorizationEvidence authorization{};
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
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == MapSelectorReseedState::committed && save.saved();
    }
};

// Replaces a dirty or previously used selector domain from an already-mapless
// service state. It clears only the two abstract selector records, verifies
// emptiness, then commits a stable baseline above observed/trusted history
// before map exposure. Package bytes are never erased or modified here.
// The caller must provide real authorization upstream and exclusive ownership
// of the selector store throughout reseed().
class MapSelectorReseedCoordinator {
public:
    explicit MapSelectorReseedCoordinator(MapSelectorStore& store);

    [[nodiscard]] MapSelectorReseedResult reseed(
        MapActivationGuard& live_guard,
        const MapSelectorReseedContext& context,
        const MapPackageEvidence& baseline);

private:
    MapSelectorStore& store_;
};

}  // namespace opentrail::maps
