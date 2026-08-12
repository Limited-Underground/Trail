#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/duplicate_checkpoint_store.hpp"

namespace opentrail::delivery {

inline constexpr char kDuplicateCheckpointPartitionLabel[] = "ot_state";
inline constexpr char kDuplicateCheckpointNamespace[] = "ot_replay";
inline constexpr char kDuplicateCheckpointSlotAKey[] = "ods_dup_a";
inline constexpr char kDuplicateCheckpointSlotBKey[] = "ods_dup_b";

static_assert(sizeof(kDuplicateCheckpointPartitionLabel) - 1 <= 15);
static_assert(sizeof(kDuplicateCheckpointNamespace) - 1 <= 15);
static_assert(sizeof(kDuplicateCheckpointSlotAKey) - 1 <= 15);
static_assert(sizeof(kDuplicateCheckpointSlotBKey) - 1 <= 15);

enum class DuplicateCheckpointKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Target-facing key/value operations. write_blob() and erase_key() stage one
// change; commit() must make it durable before returning success. The target
// owns the handle, initialization, synchronization, and native error mapping.
class DuplicateCheckpointKvBackend {
public:
    virtual ~DuplicateCheckpointKvBackend() = default;

    [[nodiscard]] virtual DuplicateCheckpointKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual DuplicateCheckpointKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual DuplicateCheckpointKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) = 0;
    [[nodiscard]] virtual DuplicateCheckpointKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

// Maps DuplicateCheckpointStorage onto two exact 704-byte ODS0 blobs. This is
// NVS-ready common code, not an ESP-IDF backend or a protected-storage claim.
class DuplicateCheckpointKvStorage final
    : public DuplicateCheckpointStorage {
public:
    explicit DuplicateCheckpointKvStorage(
        DuplicateCheckpointKvBackend& backend);

    [[nodiscard]] DuplicateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] DuplicateCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] DuplicateCheckpointStorageError erase_slot(
        std::uint8_t slot) override;

private:
    DuplicateCheckpointKvBackend& backend_;
};

}  // namespace opentrail::delivery
