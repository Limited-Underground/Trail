#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/duplicate_checkpoint_codec.hpp"

namespace opentrail::delivery {

inline constexpr std::uint8_t kStoredDuplicateCheckpointVersion = 1;
inline constexpr std::size_t kStoredDuplicateCheckpointBytes = 704;
inline constexpr std::size_t kDuplicateCheckpointSlotCount = 2;

enum class DuplicateCheckpointStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class DuplicateCheckpointStorage {
public:
    virtual ~DuplicateCheckpointStorage() = default;

    [[nodiscard]] virtual DuplicateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) = 0;
    [[nodiscard]] virtual DuplicateCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual DuplicateCheckpointStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class DuplicateCheckpointSlotState : std::uint8_t {
    empty = 0,
    valid,
    legacy_unbound,
    binding_mismatch,
    invalid,
    io_failure,
};

enum class DuplicateCheckpointStoreError : std::uint8_t {
    none = 0,
    no_checkpoint,
    invalid_binding,
    legacy_unbound,
    binding_mismatch,
    invalid_state,
    generation_conflict,
    generation_exhausted,
    storage_failure,
    verification_failure,
    checkpoint_rejected,
};

enum class DuplicateCheckpointSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct DuplicateCheckpointLoadResult {
    DuplicateCheckpointStoreError error{
        DuplicateCheckpointStoreError::no_checkpoint};
    DuplicateCheckpointSource source{DuplicateCheckpointSource::none};
    DuplicateCheckpointSlotState slot_a{DuplicateCheckpointSlotState::empty};
    DuplicateCheckpointSlotState slot_b{DuplicateCheckpointSlotState::empty};
    DuplicateCheckpointCodecError codec_error{
        DuplicateCheckpointCodecError::none};
    DuplicateError duplicate_error{DuplicateError::none};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct DuplicateCheckpointSaveResult {
    DuplicateCheckpointStoreError error{
        DuplicateCheckpointStoreError::storage_failure};
    DuplicateCheckpointSource written_slot{DuplicateCheckpointSource::none};
    DuplicateCheckpointCodecError codec_error{
        DuplicateCheckpointCodecError::none};
    std::uint64_t generation{0};
    bool repaired_peer{false};

    [[nodiscard]] constexpr bool saved() const {
        return error == DuplicateCheckpointStoreError::none;
    }
};

class DuplicateCheckpointStore {
public:
    DuplicateCheckpointStore(
        DuplicateCheckpointStorage& storage,
        std::uint64_t group_context_id,
        std::uint32_t group_epoch);

    [[nodiscard]] DuplicateCheckpointLoadResult restore(
        DuplicateWindow& window,
        std::uint64_t now_ms);
    [[nodiscard]] DuplicateCheckpointSaveResult save(
        const DuplicateWindow& window,
        std::uint64_t now_ms);
    [[nodiscard]] DuplicateCheckpointStoreError reset();
    [[nodiscard]] constexpr std::uint64_t group_context_id() const {
        return group_context_id_;
    }
    [[nodiscard]] constexpr std::uint32_t group_epoch() const {
        return group_epoch_;
    }

private:
    DuplicateCheckpointStorage& storage_;
    std::uint64_t group_context_id_{0};
    std::uint32_t group_epoch_{0};
};

}  // namespace opentrail::delivery
