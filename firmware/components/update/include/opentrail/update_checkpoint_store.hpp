#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/update_boot_guard.hpp"
#include "opentrail/update_checkpoint.hpp"

namespace opentrail::update {

inline constexpr std::size_t kUpdateCheckpointSlotCount = 2;

enum class UpdateCheckpointStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class UpdateCheckpointStorage {
public:
    virtual ~UpdateCheckpointStorage() = default;

    [[nodiscard]] virtual UpdateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) = 0;
    [[nodiscard]] virtual UpdateCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual UpdateCheckpointStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class UpdateCheckpointSlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    io_failure,
};

enum class UpdateCheckpointStoreError : std::uint8_t {
    none = 0,
    no_checkpoint,
    invalid_state,
    generation_conflict,
    generation_exhausted,
    storage_failure,
    verification_failure,
    checkpoint_rejected,
    generation_below_floor,
};

enum class UpdateCheckpointSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct UpdateCheckpointLoadResult {
    UpdateCheckpointStoreError error{
        UpdateCheckpointStoreError::no_checkpoint};
    UpdateCheckpointSource source{UpdateCheckpointSource::none};
    UpdateCheckpointSlotState slot_a{UpdateCheckpointSlotState::empty};
    UpdateCheckpointSlotState slot_b{UpdateCheckpointSlotState::empty};
    UpdateCheckpointCodecError codec_error{
        UpdateCheckpointCodecError::none};
    UpdateGuardError guard_error{UpdateGuardError::none};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct UpdateCheckpointInspectionResult {
    UpdateCheckpointStoreError error{
        UpdateCheckpointStoreError::no_checkpoint};
    UpdateCheckpointSource source{UpdateCheckpointSource::none};
    UpdateCheckpointSlotState slot_a{UpdateCheckpointSlotState::empty};
    UpdateCheckpointSlotState slot_b{UpdateCheckpointSlotState::empty};
    UpdateCheckpointCodecError codec_error{
        UpdateCheckpointCodecError::none};
    std::uint64_t generation{0};
    bool checkpoint_available{false};
    bool recovery_required{false};
};

struct UpdateCheckpointSaveResult {
    UpdateCheckpointStoreError error{
        UpdateCheckpointStoreError::storage_failure};
    UpdateCheckpointSource written_slot{UpdateCheckpointSource::none};
    UpdateCheckpointSlotState slot_a{UpdateCheckpointSlotState::empty};
    UpdateCheckpointSlotState slot_b{UpdateCheckpointSlotState::empty};
    UpdateCheckpointCodecError codec_error{
        UpdateCheckpointCodecError::none};
    UpdateGuardError guard_error{UpdateGuardError::none};
    std::uint64_t generation{0};
    bool repaired_peer{false};
    bool commit_uncertain{false};

    [[nodiscard]] constexpr bool saved() const {
        return error == UpdateCheckpointStoreError::none;
    }
};

class UpdateCheckpointStore {
public:
    explicit UpdateCheckpointStore(UpdateCheckpointStorage& storage);

    // Read-only inspection for generation reconciliation. This never restores
    // or otherwise mutates a lifecycle guard.
    [[nodiscard]] UpdateCheckpointInspectionResult inspect();
    [[nodiscard]] UpdateCheckpointLoadResult restore(
        UpdateBootGuard& guard);
    [[nodiscard]] UpdateCheckpointLoadResult restore_at_or_above(
        UpdateBootGuard& guard,
        std::uint64_t trusted_minimum_generation);
    [[nodiscard]] UpdateCheckpointSaveResult save(
        const UpdateBootGuard& guard);
    [[nodiscard]] UpdateCheckpointSaveResult save_next_after(
        const UpdateBootGuard& guard,
        std::uint64_t last_trusted_generation);
    [[nodiscard]] UpdateCheckpointStoreError reset();

private:
    UpdateCheckpointStorage& storage_;
};

}  // namespace opentrail::update
