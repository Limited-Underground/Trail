#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::maps {

inline constexpr std::uint8_t kMapSelectorDomainRecordVersion = 0;
inline constexpr std::size_t kMapSelectorDomainRecordBytes = 80;
inline constexpr std::size_t kMapSelectorDomainRecordCommitOffset = 75;
inline constexpr std::uint8_t kMapSelectorDomainRecordCommitMarker = 0xB6U;

using MapSelectorDomainId = std::array<std::uint8_t, 16>;

enum class MapSelectorDomainRecordState : std::uint8_t {
    unknown = 0,
    pending_first_baseline,
    pending_selector_reseed,
    active,
};

enum class MapSelectorDomainRecordOrigin : std::uint8_t {
    unknown = 0,
    fresh_device_commissioning,
    same_device_replacement,
};

// OTMD/v0 is a separate lifecycle record. OTM0/v0 remains unchanged and does
// not gain a partial or truncated domain identifier.
struct MapSelectorDomainRecord {
    std::uint8_t version{kMapSelectorDomainRecordVersion};
    MapSelectorDomainRecordState state{
        MapSelectorDomainRecordState::unknown};
    MapSelectorDomainRecordOrigin origin{
        MapSelectorDomainRecordOrigin::unknown};
    MapSelectorDomainId current_domain{};
    MapSelectorDomainId retired_domain{};
    std::uint64_t retired_selector_generation{0};
    std::uint64_t accepted_selector_generation{0};
    std::uint64_t domain_epoch{0};
    std::uint64_t record_generation{0};
};

enum class MapSelectorDomainRecordError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_record,
    bad_magic,
    unsupported_version,
    noncanonical_record,
    uncommitted_record,
    integrity_failure,
};

struct MapSelectorDomainRecordResult {
    MapSelectorDomainRecordError error{
        MapSelectorDomainRecordError::invalid_argument};
    std::size_t bytes{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == MapSelectorDomainRecordError::none;
    }
};

[[nodiscard]] bool map_selector_domain_id_nonzero(
    const MapSelectorDomainId& domain);
[[nodiscard]] MapSelectorDomainRecordError validate_map_selector_domain_record(
    const MapSelectorDomainRecord& record);
[[nodiscard]] MapSelectorDomainRecordResult encode_map_selector_domain_record(
    const MapSelectorDomainRecord& record,
    std::uint8_t* output,
    std::size_t output_capacity);
[[nodiscard]] MapSelectorDomainRecordResult decode_map_selector_domain_record(
    const std::uint8_t* data,
    std::size_t size,
    MapSelectorDomainRecord& output);

}  // namespace opentrail::maps
