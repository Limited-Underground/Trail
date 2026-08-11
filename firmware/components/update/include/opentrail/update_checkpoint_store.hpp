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

    [[nodiscard]] UpdateCheckpointLoadResult restore(
        UpdateBootGuard& guard);
    [[nodiscard]] UpdateCheckpointSaveResult save(
        const UpdateBootGuard& guard);
    [[nodiscard]] UpdateCheckpointStoreError reset();

private:
    UpdateCheckpointStorage& storage_;
};

}  // namespace opentrail::update
