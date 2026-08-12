#include "opentrail/map_selector_domain_kv_storage.hpp"

#include <array>

namespace opentrail::maps {
namespace {

const char* key_for_slot(std::uint8_t slot) {
    switch (slot) {
        case 0:
            return kMapSelectorDomainSlotAKey;
        case 1:
            return kMapSelectorDomainSlotBKey;
        default:
            return nullptr;
    }
}

MapSelectorDomainStorageError map_error(
    MapSelectorDomainKvBackendError error) {
    switch (error) {
        case MapSelectorDomainKvBackendError::none:
            return MapSelectorDomainStorageError::none;
        case MapSelectorDomainKvBackendError::not_found:
            return MapSelectorDomainStorageError::not_found;
        case MapSelectorDomainKvBackendError::invalid_argument:
            return MapSelectorDomainStorageError::invalid_argument;
        case MapSelectorDomainKvBackendError::io_failure:
            return MapSelectorDomainStorageError::io_failure;
    }
    return MapSelectorDomainStorageError::io_failure;
}

MapSelectorDomainStorageError commit_pending(
    MapSelectorDomainKvBackend& backend) {
    return map_error(backend.commit(
        kMapSelectorDomainPartitionLabel,
        kMapSelectorDomainNamespace));
}

}  // namespace

MapSelectorDomainKvStorage::MapSelectorDomainKvStorage(
    MapSelectorDomainKvBackend& backend)
    : backend_(backend) {}

MapSelectorDomainStorageError MapSelectorDomainKvStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || output == nullptr ||
        size != kMapSelectorDomainRecordBytes) {
        return MapSelectorDomainStorageError::invalid_argument;
    }
    std::size_t actual_size = 0;
    const auto read = backend_.read_blob(
        kMapSelectorDomainPartitionLabel,
        kMapSelectorDomainNamespace,
        key,
        output,
        size,
        actual_size);
    if (read != MapSelectorDomainKvBackendError::none) {
        return map_error(read);
    }
    return actual_size == kMapSelectorDomainRecordBytes
               ? MapSelectorDomainStorageError::none
               : MapSelectorDomainStorageError::io_failure;
}

MapSelectorDomainStorageError MapSelectorDomainKvStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || data == nullptr ||
        size != kMapSelectorDomainRecordBytes ||
        data[kMapSelectorDomainRecordCommitOffset] != 0) {
        return MapSelectorDomainStorageError::invalid_argument;
    }
    const auto written = backend_.write_blob(
        kMapSelectorDomainPartitionLabel,
        kMapSelectorDomainNamespace,
        key,
        data,
        size);
    if (written != MapSelectorDomainKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

MapSelectorDomainStorageError MapSelectorDomainKvStorage::commit_slot(
    std::uint8_t slot,
    std::size_t offset,
    std::uint8_t value) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr ||
        offset != kMapSelectorDomainRecordCommitOffset ||
        value != kMapSelectorDomainRecordCommitMarker) {
        return MapSelectorDomainStorageError::invalid_argument;
    }

    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    std::size_t actual_size = 0;
    const auto read = backend_.read_blob(
        kMapSelectorDomainPartitionLabel,
        kMapSelectorDomainNamespace,
        key,
        bytes.data(),
        bytes.size(),
        actual_size);
    if (read != MapSelectorDomainKvBackendError::none) {
        return map_error(read);
    }
    if (actual_size != bytes.size() || bytes[offset] != 0) {
        return MapSelectorDomainStorageError::io_failure;
    }
    bytes[offset] = value;
    const auto written = backend_.write_blob(
        kMapSelectorDomainPartitionLabel,
        kMapSelectorDomainNamespace,
        key,
        bytes.data(),
        bytes.size());
    if (written != MapSelectorDomainKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

}  // namespace opentrail::maps
