#include "opentrail/map_selector_kv_storage.hpp"

#include <array>

namespace opentrail::maps {
namespace {

const char* key_for_slot(std::uint8_t slot) {
    switch (slot) {
        case 0:
            return kMapSelectorSlotAKey;
        case 1:
            return kMapSelectorSlotBKey;
        default:
            return nullptr;
    }
}

MapSelectorStorageError map_error(MapSelectorKvBackendError error) {
    switch (error) {
        case MapSelectorKvBackendError::none:
            return MapSelectorStorageError::none;
        case MapSelectorKvBackendError::not_found:
            return MapSelectorStorageError::not_found;
        case MapSelectorKvBackendError::invalid_argument:
            return MapSelectorStorageError::invalid_argument;
        case MapSelectorKvBackendError::io_failure:
            return MapSelectorStorageError::io_failure;
    }
    return MapSelectorStorageError::io_failure;
}

MapSelectorStorageError commit_pending(MapSelectorKvBackend& backend) {
    return map_error(backend.commit(
        kMapSelectorPartitionLabel, kMapSelectorNamespace));
}

}  // namespace

MapSelectorKvStorage::MapSelectorKvStorage(MapSelectorKvBackend& backend)
    : backend_(backend) {}

MapSelectorStorageError MapSelectorKvStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || output == nullptr ||
        size != kMapSelectorCheckpointBytes) {
        return MapSelectorStorageError::invalid_argument;
    }

    std::size_t actual_size = 0;
    const auto error = backend_.read_blob(
        kMapSelectorPartitionLabel,
        kMapSelectorNamespace,
        key,
        output,
        size,
        actual_size);
    if (error != MapSelectorKvBackendError::none) {
        return map_error(error);
    }
    return actual_size == kMapSelectorCheckpointBytes
               ? MapSelectorStorageError::none
               : MapSelectorStorageError::io_failure;
}

MapSelectorStorageError MapSelectorKvStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || data == nullptr ||
        size != kMapSelectorCheckpointBytes ||
        data[kMapSelectorCommitOffset] != 0) {
        return MapSelectorStorageError::invalid_argument;
    }

    const auto written = backend_.write_blob(
        kMapSelectorPartitionLabel,
        kMapSelectorNamespace,
        key,
        data,
        size);
    if (written != MapSelectorKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

MapSelectorStorageError MapSelectorKvStorage::commit_slot(
    std::uint8_t slot,
    std::size_t offset,
    std::uint8_t value) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || offset != kMapSelectorCommitOffset ||
        value != kMapSelectorCommitMarker) {
        return MapSelectorStorageError::invalid_argument;
    }

    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    std::size_t actual_size = 0;
    const auto read = backend_.read_blob(
        kMapSelectorPartitionLabel,
        kMapSelectorNamespace,
        key,
        bytes.data(),
        bytes.size(),
        actual_size);
    if (read != MapSelectorKvBackendError::none) {
        return map_error(read);
    }
    if (actual_size != bytes.size() || bytes[offset] != 0) {
        return MapSelectorStorageError::io_failure;
    }

    bytes[offset] = value;
    const auto written = backend_.write_blob(
        kMapSelectorPartitionLabel,
        kMapSelectorNamespace,
        key,
        bytes.data(),
        bytes.size());
    if (written != MapSelectorKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

MapSelectorStorageError MapSelectorKvStorage::erase_slot(
    std::uint8_t slot) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return MapSelectorStorageError::invalid_argument;
    }

    const auto erased = backend_.erase_key(
        kMapSelectorPartitionLabel,
        kMapSelectorNamespace,
        key);
    if (erased == MapSelectorKvBackendError::not_found) {
        return MapSelectorStorageError::none;
    }
    if (erased != MapSelectorKvBackendError::none) {
        return map_error(erased);
    }
    return commit_pending(backend_);
}

}  // namespace opentrail::maps
