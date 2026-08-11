#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/map_selector_store.hpp"

namespace opentrail::maps {

inline constexpr char kMapSelectorPartitionLabel[] = "ot_state";
inline constexpr char kMapSelectorNamespace[] = "ot_maps";
inline constexpr char kMapSelectorSlotAKey[] = "otm_sel_a";
inline constexpr char kMapSelectorSlotBKey[] = "otm_sel_b";

static_assert(sizeof(kMapSelectorPartitionLabel) - 1 <= 15);
static_assert(sizeof(kMapSelectorNamespace) - 1 <= 15);
static_assert(sizeof(kMapSelectorSlotAKey) - 1 <= 15);
static_assert(sizeof(kMapSelectorSlotBKey) - 1 <= 15);

enum class MapSelectorKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Target-facing key/value operations. write_blob() and erase_key() stage one
// change; commit() must make that change durable before returning success.
// The implementation must provide exclusive ownership with no unrelated
// pending changes on the same handle/transaction.
class MapSelectorKvBackend {
public:
    virtual ~MapSelectorKvBackend() = default;

    [[nodiscard]] virtual MapSelectorKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual MapSelectorKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual MapSelectorKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) = 0;
    [[nodiscard]] virtual MapSelectorKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

// Maps the abstract two-slot selector store onto two exact 64-byte key/value
// blobs. It is NVS-ready but contains no ESP-IDF headers or physical-storage
// claims. The target backend owns initialization, handles, synchronization,
// security configuration, and native error translation.
class MapSelectorKvStorage final : public MapSelectorStorage {
public:
    explicit MapSelectorKvStorage(MapSelectorKvBackend& backend);

    [[nodiscard]] MapSelectorStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] MapSelectorStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] MapSelectorStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override;
    [[nodiscard]] MapSelectorStorageError erase_slot(
        std::uint8_t slot) override;

private:
    MapSelectorKvBackend& backend_;
};

}  // namespace opentrail::maps
