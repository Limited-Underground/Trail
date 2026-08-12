#include "opentrail/duplicate_checkpoint_kv_storage.hpp"

namespace opentrail::delivery {
namespace {

const char* key_for_slot(std::uint8_t slot) {
    switch (slot) {
        case 0:
            return kDuplicateCheckpointSlotAKey;
        case 1:
            return kDuplicateCheckpointSlotBKey;
        default:
            return nullptr;
    }
}

DuplicateCheckpointStorageError map_error(
    DuplicateCheckpointKvBackendError error) {
    switch (error) {
        case DuplicateCheckpointKvBackendError::none:
            return DuplicateCheckpointStorageError::none;
        case DuplicateCheckpointKvBackendError::not_found:
            return DuplicateCheckpointStorageError::not_found;
        case DuplicateCheckpointKvBackendError::invalid_argument:
            return DuplicateCheckpointStorageError::invalid_argument;
        case DuplicateCheckpointKvBackendError::io_failure:
            return DuplicateCheckpointStorageError::io_failure;
    }
    return DuplicateCheckpointStorageError::io_failure;
}

DuplicateCheckpointStorageError commit_pending(
    DuplicateCheckpointKvBackend& backend) {
    return map_error(backend.commit(
        kDuplicateCheckpointPartitionLabel,
        kDuplicateCheckpointNamespace));
}

}  // namespace

DuplicateCheckpointKvStorage::DuplicateCheckpointKvStorage(
    DuplicateCheckpointKvBackend& backend)
    : backend_(backend) {}

DuplicateCheckpointStorageError DuplicateCheckpointKvStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || output == nullptr ||
        size != kStoredDuplicateCheckpointBytes) {
        return DuplicateCheckpointStorageError::invalid_argument;
    }

    std::size_t actual_size = 0;
    const auto error = backend_.read_blob(
        kDuplicateCheckpointPartitionLabel,
        kDuplicateCheckpointNamespace,
        key,
        output,
        size,
        actual_size);
    if (error != DuplicateCheckpointKvBackendError::none) {
        return map_error(error);
    }
    return actual_size == kStoredDuplicateCheckpointBytes
               ? DuplicateCheckpointStorageError::none
               : DuplicateCheckpointStorageError::io_failure;
}

DuplicateCheckpointStorageError DuplicateCheckpointKvStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || data == nullptr ||
        size != kStoredDuplicateCheckpointBytes) {
        return DuplicateCheckpointStorageError::invalid_argument;
    }

    const auto written = backend_.write_blob(
        kDuplicateCheckpointPartitionLabel,
        kDuplicateCheckpointNamespace,
        key,
        data,
        size);
    if (written != DuplicateCheckpointKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

DuplicateCheckpointStorageError DuplicateCheckpointKvStorage::erase_slot(
    std::uint8_t slot) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return DuplicateCheckpointStorageError::invalid_argument;
    }

    const auto erased = backend_.erase_key(
        kDuplicateCheckpointPartitionLabel,
        kDuplicateCheckpointNamespace,
        key);
    if (erased == DuplicateCheckpointKvBackendError::not_found) {
        return DuplicateCheckpointStorageError::none;
    }
    if (erased != DuplicateCheckpointKvBackendError::none) {
        return map_error(erased);
    }
    return commit_pending(backend_);
}

}  // namespace opentrail::delivery
