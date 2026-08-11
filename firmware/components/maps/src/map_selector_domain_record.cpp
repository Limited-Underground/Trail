#include "opentrail/map_selector_domain_record.hpp"

#include <algorithm>

namespace opentrail::maps {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'M', 'D'}};
constexpr std::size_t kCrcOffset = kMapSelectorDomainRecordBytes - 4;

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) <<
                 (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) <<
                 (index * 8U);
    }
    return value;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool all_zero(const MapSelectorDomainId& domain) {
    return std::all_of(
        domain.begin(), domain.end(), [](std::uint8_t value) {
            return value == 0;
        });
}

bool fresh_origin(const MapSelectorDomainRecord& record) {
    return record.origin ==
           MapSelectorDomainRecordOrigin::fresh_device_commissioning;
}

bool replacement_origin(const MapSelectorDomainRecord& record) {
    return record.origin ==
           MapSelectorDomainRecordOrigin::same_device_replacement;
}

}  // namespace

bool map_selector_domain_id_nonzero(const MapSelectorDomainId& domain) {
    return !all_zero(domain);
}

MapSelectorDomainRecordError validate_map_selector_domain_record(
    const MapSelectorDomainRecord& record) {
    if (record.version != kMapSelectorDomainRecordVersion ||
        !map_selector_domain_id_nonzero(record.current_domain) ||
        record.domain_epoch == 0 || record.record_generation == 0) {
        return MapSelectorDomainRecordError::invalid_record;
    }

    if (fresh_origin(record)) {
        if (map_selector_domain_id_nonzero(record.retired_domain) ||
            record.retired_selector_generation != 0 ||
            record.domain_epoch != 1) {
            return MapSelectorDomainRecordError::invalid_record;
        }
        if (record.state ==
            MapSelectorDomainRecordState::pending_first_baseline) {
            return record.accepted_selector_generation == 0
                       ? MapSelectorDomainRecordError::none
                       : MapSelectorDomainRecordError::invalid_record;
        }
        if (record.state == MapSelectorDomainRecordState::active) {
            return record.accepted_selector_generation != 0
                       ? MapSelectorDomainRecordError::none
                       : MapSelectorDomainRecordError::invalid_record;
        }
        return MapSelectorDomainRecordError::invalid_record;
    }

    if (replacement_origin(record)) {
        if (!map_selector_domain_id_nonzero(record.retired_domain) ||
            record.current_domain == record.retired_domain ||
            record.domain_epoch < 2) {
            return MapSelectorDomainRecordError::invalid_record;
        }
        if (record.state ==
            MapSelectorDomainRecordState::pending_selector_reseed) {
            return record.accepted_selector_generation == 0
                       ? MapSelectorDomainRecordError::none
                       : MapSelectorDomainRecordError::invalid_record;
        }
        if (record.state == MapSelectorDomainRecordState::active) {
            return record.accepted_selector_generation >
                           record.retired_selector_generation
                       ? MapSelectorDomainRecordError::none
                       : MapSelectorDomainRecordError::invalid_record;
        }
        return MapSelectorDomainRecordError::invalid_record;
    }

    return MapSelectorDomainRecordError::invalid_record;
}

MapSelectorDomainRecordResult encode_map_selector_domain_record(
    const MapSelectorDomainRecord& record,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (output == nullptr || output_capacity < kMapSelectorDomainRecordBytes) {
        return {MapSelectorDomainRecordError::invalid_argument, 0};
    }
    const auto validation = validate_map_selector_domain_record(record);
    if (validation != MapSelectorDomainRecordError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = record.version;
    candidate[5] = static_cast<std::uint8_t>(record.state);
    candidate[6] = static_cast<std::uint8_t>(record.origin);
    std::copy(
        record.current_domain.begin(),
        record.current_domain.end(),
        candidate.begin() + 8);
    std::copy(
        record.retired_domain.begin(),
        record.retired_domain.end(),
        candidate.begin() + 24);
    write_u64(candidate.data() + 40, record.retired_selector_generation);
    write_u64(candidate.data() + 48, record.accepted_selector_generation);
    write_u64(candidate.data() + 56, record.domain_epoch);
    write_u64(candidate.data() + 64, record.record_generation);
    candidate[kMapSelectorDomainRecordCommitOffset] =
        kMapSelectorDomainRecordCommitMarker;
    write_u32(
        candidate.data() + kCrcOffset,
        crc32(candidate.data(), kCrcOffset));
    std::copy(candidate.begin(), candidate.end(), output);
    return {MapSelectorDomainRecordError::none, candidate.size()};
}

MapSelectorDomainRecordResult decode_map_selector_domain_record(
    const std::uint8_t* data,
    std::size_t size,
    MapSelectorDomainRecord& output) {
    if (data == nullptr || size != kMapSelectorDomainRecordBytes) {
        return {MapSelectorDomainRecordError::invalid_argument, 0};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) {
        return {MapSelectorDomainRecordError::bad_magic, 0};
    }
    if (data[4] != kMapSelectorDomainRecordVersion) {
        return {MapSelectorDomainRecordError::unsupported_version, 0};
    }
    if (data[7] != 0 ||
        !std::all_of(data + 72,
                     data + kMapSelectorDomainRecordCommitOffset,
                     [](std::uint8_t value) { return value == 0; })) {
        return {MapSelectorDomainRecordError::noncanonical_record, 0};
    }
    if (data[kMapSelectorDomainRecordCommitOffset] !=
        kMapSelectorDomainRecordCommitMarker) {
        return {MapSelectorDomainRecordError::uncommitted_record, 0};
    }
    if (read_u32(data + kCrcOffset) != crc32(data, kCrcOffset)) {
        return {MapSelectorDomainRecordError::integrity_failure, 0};
    }

    MapSelectorDomainRecord candidate{};
    candidate.version = data[4];
    candidate.state = static_cast<MapSelectorDomainRecordState>(data[5]);
    candidate.origin = static_cast<MapSelectorDomainRecordOrigin>(data[6]);
    std::copy(data + 8, data + 24, candidate.current_domain.begin());
    std::copy(data + 24, data + 40, candidate.retired_domain.begin());
    candidate.retired_selector_generation = read_u64(data + 40);
    candidate.accepted_selector_generation = read_u64(data + 48);
    candidate.domain_epoch = read_u64(data + 56);
    candidate.record_generation = read_u64(data + 64);
    const auto validation = validate_map_selector_domain_record(candidate);
    if (validation != MapSelectorDomainRecordError::none) {
        return {validation, 0};
    }
    output = candidate;
    return {MapSelectorDomainRecordError::none, size};
}

}  // namespace opentrail::maps
