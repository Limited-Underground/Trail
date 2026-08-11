#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/map_selector_domain_record.hpp"

namespace opentrail::maps {

inline constexpr std::size_t kMapSelectorDomainSlotCount = 2;

enum class MapSelectorDomainStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// This interface is a separate persistence domain from the OTM0 selector
// store. Implementations must bind it to two exact 80-byte OTMD slots.
class MapSelectorDomainStorage {
public:
    virtual ~MapSelectorDomainStorage() = default;

    [[nodiscard]] virtual MapSelectorDomainStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) = 0;
    [[nodiscard]] virtual MapSelectorDomainStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual MapSelectorDomainStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) = 0;
};

enum class MapSelectorDomainSlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    uncommitted,
    io_failure,
};

enum class MapSelectorDomainStoreError : std::uint8_t {
    none = 0,
    no_record,
    invalid_state,
    generation_conflict,
    generation_mismatch,
    generation_exhausted,
    transition_rejected,
    storage_failure,
    verification_failure,
    record_rejected,
};

enum class MapSelectorDomainSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct MapSelectorDomainInspectionResult {
    MapSelectorDomainStoreError error{MapSelectorDomainStoreError::no_record};
    MapSelectorDomainSource source{MapSelectorDomainSource::none};
    MapSelectorDomainSlotState slot_a{MapSelectorDomainSlotState::empty};
    MapSelectorDomainSlotState slot_b{MapSelectorDomainSlotState::empty};
    MapSelectorDomainRecordError codec_error{
        MapSelectorDomainRecordError::none};
    MapSelectorDomainRecord record{};
    bool record_available{false};
    bool recovery_required{false};
};

struct MapSelectorDomainSaveResult {
    MapSelectorDomainStoreError error{
        MapSelectorDomainStoreError::storage_failure};
    MapSelectorDomainSource written_slot{MapSelectorDomainSource::none};
    MapSelectorDomainSlotState slot_a{MapSelectorDomainSlotState::empty};
    MapSelectorDomainSlotState slot_b{MapSelectorDomainSlotState::empty};
    MapSelectorDomainRecordError codec_error{
        MapSelectorDomainRecordError::none};
    std::uint64_t generation{0};
    bool repaired_peer{false};
    bool commit_uncertain{false};

    [[nodiscard]] constexpr bool saved() const {
        return error == MapSelectorDomainStoreError::none;
    }
};

class MapSelectorDomainStore {
public:
    explicit MapSelectorDomainStore(MapSelectorDomainStorage& storage);

    [[nodiscard]] MapSelectorDomainInspectionResult inspect();

    // The caller supplies the exact next canonical record generation. Empty
    // media accepts only generation 1 pending-first-baseline state. Existing
    // media accepts only the immediately following generation and an allowed
    // lifecycle transition. This method grants no provisioning authority.
    [[nodiscard]] MapSelectorDomainSaveResult save(
        const MapSelectorDomainRecord& proposed);

private:
    MapSelectorDomainStorage& storage_;
};

}  // namespace opentrail::maps
