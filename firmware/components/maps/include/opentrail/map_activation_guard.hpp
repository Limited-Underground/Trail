#pragma once

#include <cstdint>

namespace opentrail::maps {

enum class MapSlot : std::uint8_t {
    none = 0,
    slot_a = 1,
    slot_b = 2,
};

enum class MapSelectorState : std::uint8_t {
    missing = 0,
    valid,
    unreadable,
    ambiguous,
};

enum class MapActivationState : std::uint8_t {
    stopped = 0,
    mapless,
    active,
    staged,
    trial,
    fallback_required,
};

enum class MapActivationReason : std::uint8_t {
    none = 0,
    no_selector,
    selector_unreadable,
    selector_ambiguous,
    selected_package_invalid,
    candidate_removed,
    trial_read_failed,
    trial_deadline_reached,
    clock_regression,
    active_media_removed,
    fallback_unavailable,
    checkpoint_invalid,
    checkpoint_policy_mismatch,
    trial_boot_limit_reached,
};

enum class MapActivationError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_policy,
    invalid_selector,
    invalid_package,
    verification_required,
    selector_mismatch,
    trial_health_failed,
    trial_deadline_reached,
    clock_regression,
    fallback_unavailable,
    invalid_checkpoint,
    checkpoint_mismatch,
    trial_boot_limit_reached,
};

struct MapActivationPolicy {
    std::uint64_t maximum_package_bytes{0};
    std::uint64_t trial_deadline_ms{0};
    std::uint16_t required_healthy_reads{0};
    std::uint8_t maximum_trial_boots{0};
};

// Evidence is produced by adapters. The guard never opens, modifies, mounts,
// authenticates, transfers, downloads, or renders a package.
struct MapPackageEvidence {
    MapSlot slot{MapSlot::none};
    std::uint64_t generation{0};
    std::uint64_t package_bytes{0};
    bool manifest_valid{false};
    bool rights_permitted{false};
    bool attribution_available{false};
    bool integrity_verified{false};
    bool reader_compatible{false};
    bool index_readable{false};
    bool storage_sufficient{false};
    bool read_only_capable{false};
};

struct MapBootSelection {
    MapSelectorState selector_state{MapSelectorState::missing};
    MapPackageEvidence selected{};
};

struct MapActivationStatus {
    bool running{false};
    MapActivationState state{MapActivationState::stopped};
    MapActivationReason reason{MapActivationReason::none};
    MapSlot active_slot{MapSlot::none};
    MapSlot previous_slot{MapSlot::none};
    MapSlot staged_slot{MapSlot::none};
    std::uint64_t active_generation{0};
    std::uint64_t previous_generation{0};
    std::uint64_t staged_generation{0};
    std::uint64_t trial_started_ms{0};
    std::uint64_t last_monotonic_ms{0};
    std::uint16_t healthy_trial_reads{0};
    std::uint8_t trial_boots{0};
    bool map_available{false};
    bool unavailable_notice_required{false};
    bool previous_cleanup_permitted{false};
};

static_assert(sizeof(MapActivationStatus) <= 80,
              "Map activation status must remain bounded");

struct MapSelectorCheckpoint;

// Pure lifecycle policy. A storage adapter must persist and verify a selector
// before mark_selector_committed() or complete_fallback() is called. Package
// authentication, selector persistence, file operations, rendering, and all
// communications remain separate responsibilities.
class MapActivationGuard {
public:
    [[nodiscard]] MapActivationError start(
        const MapActivationPolicy& policy,
        const MapBootSelection& boot);
    [[nodiscard]] MapActivationError start_from_checkpoint(
        const MapActivationPolicy& policy,
        const MapSelectorCheckpoint& checkpoint,
        const MapPackageEvidence& selected,
        const MapPackageEvidence& previous,
        std::uint64_t now_ms);
    void stop();

    [[nodiscard]] MapActivationError stage(
        const MapPackageEvidence& candidate);
    [[nodiscard]] MapActivationError cancel_staged();
    [[nodiscard]] MapActivationError mark_selector_committed(
        MapSlot slot,
        std::uint64_t generation,
        std::uint64_t now_ms);
    [[nodiscard]] MapActivationError report_trial_read(
        bool complete_read_succeeded,
        std::uint64_t now_ms);
    [[nodiscard]] MapActivationError tick(std::uint64_t now_ms);
    [[nodiscard]] MapActivationError complete_fallback(
        const MapPackageEvidence& restored);
    [[nodiscard]] MapActivationError mark_previous_removed(
        MapSlot slot,
        std::uint64_t generation);
    [[nodiscard]] MapActivationError report_media_removed(MapSlot slot);
    [[nodiscard]] MapActivationError export_checkpoint(
        std::uint64_t record_generation,
        MapSelectorCheckpoint& output) const;

    [[nodiscard]] MapActivationStatus status() const;

private:
    [[nodiscard]] bool package_acceptable(
        const MapPackageEvidence& package) const;
    void clear_staged();
    void enter_mapless(MapActivationReason reason);
    void require_fallback(MapActivationReason reason);
    [[nodiscard]] MapActivationError advance_trial_clock(
        std::uint64_t now_ms);

    MapActivationPolicy policy_{};
    MapActivationStatus status_{};
};

}  // namespace opentrail::maps
