#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/update_checkpoint_store.hpp"

namespace opentrail::update {

inline constexpr char kUpdateCheckpointPartitionLabel[] = "ot_state";
inline constexpr char kUpdateCheckpointNamespace[] = "ot_update";
inline constexpr char kUpdateCheckpointSlotAKey[] = "otu_chk_a";
inline constexpr char kUpdateCheckpointSlotBKey[] = "otu_chk_b";

static_assert(sizeof(kUpdateCheckpointPartitionLabel) - 1 <= 15);
static_assert(sizeof(kUpdateCheckpointNamespace) - 1 <= 15);
static_assert(sizeof(kUpdateCheckpointSlotAKey) - 1 <= 15);
static_assert(sizeof(kUpdateCheckpointSlotBKey) - 1 <= 15);

enum class UpdateCheckpointKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Target-facing key/value operations. write_blob() and erase_key() stage one
// change; commit() must make that change durable before returning success.
// The implementation must provide exclusive ownership with no unrelated
// pending changes on the same handle/transaction.
class UpdateCheckpointKvBackend {
public:
    virtual ~UpdateCheckpointKvBackend() = default;

    [[nodiscard]] virtual UpdateCheckpointKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual UpdateCheckpointKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual UpdateCheckpointKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) = 0;
    [[nodiscard]] virtual UpdateCheckpointKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

// Maps UpdateCheckpointStorage onto two exact 64-byte key/value blobs. This is
// NVS-ready but contains no ESP-IDF headers or physical-storage claims. The
// target backend owns initialization, handles, synchronization, security
// configuration, native error translation, and durability evidence.
class UpdateCheckpointKvStorage final : public UpdateCheckpointStorage {
public:
    explicit UpdateCheckpointKvStorage(UpdateCheckpointKvBackend& backend);

    [[nodiscard]] UpdateCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] UpdateCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] UpdateCheckpointStorageError erase_slot(
        std::uint8_t slot) override;

private:
    UpdateCheckpointKvBackend& backend_;
};

}  // namespace opentrail::update
