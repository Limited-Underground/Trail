#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/map_selector_domain_store.hpp"

namespace opentrail::maps {

inline constexpr char kMapSelectorDomainPartitionLabel[] = "ot_state";
inline constexpr char kMapSelectorDomainNamespace[] = "ot_map_domain";
inline constexpr char kMapSelectorDomainSlotAKey[] = "otmd_a";
inline constexpr char kMapSelectorDomainSlotBKey[] = "otmd_b";

static_assert(sizeof(kMapSelectorDomainPartitionLabel) - 1 <= 15);
static_assert(sizeof(kMapSelectorDomainNamespace) - 1 <= 15);
static_assert(sizeof(kMapSelectorDomainSlotAKey) - 1 <= 15);
static_assert(sizeof(kMapSelectorDomainSlotBKey) - 1 <= 15);

enum class MapSelectorDomainKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Backend writes stage one complete blob and commit() makes that mutation
// durable. One adapter instance must exclusively own its transaction/handle.
class MapSelectorDomainKvBackend {
public:
    virtual ~MapSelectorDomainKvBackend() = default;

    [[nodiscard]] virtual MapSelectorDomainKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual MapSelectorDomainKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual MapSelectorDomainKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

// Maps the non-erasable trust-domain store onto two exact 80-byte key/value
// blobs. Marker commit rereads the prepared record and rewrites the whole blob
// with byte 75 changed. No erase or reset authority is exposed.
class MapSelectorDomainKvStorage final
    : public MapSelectorDomainStorage {
public:
    explicit MapSelectorDomainKvStorage(
        MapSelectorDomainKvBackend& backend);

    [[nodiscard]] MapSelectorDomainStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] MapSelectorDomainStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] MapSelectorDomainStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override;

private:
    MapSelectorDomainKvBackend& backend_;
};

}  // namespace opentrail::maps
