#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"

namespace opentrail::maps {

inline constexpr std::size_t kMapSelectorSlotCount = 2;

enum class MapSelectorStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class MapSelectorStorage {
public:
    virtual ~MapSelectorStorage() = default;

    [[nodiscard]] virtual MapSelectorStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) = 0;
    [[nodiscard]] virtual MapSelectorStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual MapSelectorStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) = 0;
    [[nodiscard]] virtual MapSelectorStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class MapSelectorSlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    uncommitted,
    io_failure,
};

enum class MapSelectorStoreError : std::uint8_t {
    none = 0,
    no_checkpoint,
    invalid_state,
    generation_conflict,
    generation_exhausted,
    generation_below_floor,
    state_mismatch,
    storage_failure,
    verification_failure,
    checkpoint_rejected,
};

enum class MapSelectorSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct MapSelectorInspectionResult {
    MapSelectorStoreError error{MapSelectorStoreError::no_checkpoint};
    MapSelectorSource source{MapSelectorSource::none};
    MapSelectorSlotState slot_a{MapSelectorSlotState::empty};
    MapSelectorSlotState slot_b{MapSelectorSlotState::empty};
    MapSelectorCheckpointError codec_error{
        MapSelectorCheckpointError::none};
    std::uint64_t generation{0};
    bool checkpoint_available{false};
    bool recovery_required{false};
};

struct MapSelectorLoadResult {
    MapSelectorStoreError error{MapSelectorStoreError::no_checkpoint};
    MapSelectorSource source{MapSelectorSource::none};
    MapSelectorSlotState slot_a{MapSelectorSlotState::empty};
    MapSelectorSlotState slot_b{MapSelectorSlotState::empty};
    MapSelectorCheckpointError codec_error{
        MapSelectorCheckpointError::none};
    MapActivationError guard_error{MapActivationError::none};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct MapSelectorVerifyResult {
    MapSelectorStoreError error{MapSelectorStoreError::no_checkpoint};
    MapSelectorSource source{MapSelectorSource::none};
    MapSelectorSlotState slot_a{MapSelectorSlotState::empty};
    MapSelectorSlotState slot_b{MapSelectorSlotState::empty};
    MapSelectorCheckpointError codec_error{
        MapSelectorCheckpointError::none};
    MapActivationError guard_error{MapActivationError::none};
    std::uint64_t generation{0};
    bool exact_match{false};
    bool recovery_required{false};
};

struct MapSelectorSaveResult {
    MapSelectorStoreError error{MapSelectorStoreError::storage_failure};
    MapSelectorSource written_slot{MapSelectorSource::none};
    MapSelectorSlotState slot_a{MapSelectorSlotState::empty};
    MapSelectorSlotState slot_b{MapSelectorSlotState::empty};
    MapSelectorCheckpointError codec_error{
        MapSelectorCheckpointError::none};
    MapActivationError guard_error{MapActivationError::none};
    std::uint64_t generation{0};
    bool repaired_peer{false};
    bool commit_uncertain{false};

    [[nodiscard]] constexpr bool saved() const {
        return error == MapSelectorStoreError::none;
    }
};

struct MapSelectorResetResult {
    MapSelectorStoreError error{MapSelectorStoreError::storage_failure};
    MapSelectorStorageError erase_slot_a{
        MapSelectorStorageError::io_failure};
    MapSelectorStorageError erase_slot_b{
        MapSelectorStorageError::io_failure};
    MapSelectorSlotState slot_a{MapSelectorSlotState::io_failure};
    MapSelectorSlotState slot_b{MapSelectorSlotState::io_failure};
    bool empty_verified{false};

    [[nodiscard]] constexpr bool cleared() const {
        return error == MapSelectorStoreError::none && empty_verified;
    }
};

class MapSelectorStore {
public:
    explicit MapSelectorStore(MapSelectorStorage& storage);

    [[nodiscard]] MapSelectorInspectionResult inspect();
    [[nodiscard]] MapSelectorLoadResult restore(
        MapActivationGuard& guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& selected,
        const MapPackageEvidence& previous,
        std::uint64_t now_ms);
    [[nodiscard]] MapSelectorLoadResult restore_at_or_above(
        MapActivationGuard& guard,
        const MapActivationPolicy& policy,
        const MapPackageEvidence& selected,
        const MapPackageEvidence& previous,
        std::uint64_t now_ms,
        std::uint64_t trusted_minimum_generation);
    [[nodiscard]] MapSelectorVerifyResult verify_current(
        const MapActivationGuard& guard,
        std::uint64_t trusted_minimum_generation = 0);
    [[nodiscard]] MapSelectorSaveResult save(
        const MapActivationGuard& guard);
    [[nodiscard]] MapSelectorSaveResult save_next_after(
        const MapActivationGuard& guard,
        std::uint64_t last_trusted_generation);
    // Saves only when both selector slots are still empty. This is not a lock;
    // callers must hold exclusive store ownership throughout the call.
    [[nodiscard]] MapSelectorSaveResult save_if_empty(
        const MapActivationGuard& guard,
        std::uint64_t last_trusted_generation = 0);
    // Re-inspects the store and saves only when the newest valid record still
    // has expected_current_generation. This is not a lock; callers must hold
    // exclusive store ownership throughout the call.
    [[nodiscard]] MapSelectorSaveResult save_after_exact(
        const MapActivationGuard& guard,
        std::uint64_t expected_current_generation,
        std::uint64_t last_trusted_generation);
    [[nodiscard]] MapSelectorStoreError reset();
    // Erases both selector records and then reads both back. A successful erase
    // call is not considered sufficient proof that the selector is empty.
    // Package bytes and all non-selector storage are outside this operation.
    [[nodiscard]] MapSelectorResetResult reset_and_verify_empty();

private:
    [[nodiscard]] MapSelectorSaveResult save_impl(
        const MapActivationGuard& guard,
        std::uint64_t last_trusted_generation,
        bool require_expected_generation,
        std::uint64_t expected_current_generation,
        bool require_empty);

    MapSelectorStorage& storage_;
};

}  // namespace opentrail::maps
