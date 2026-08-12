#include "opentrail/update_checkpoint_kv_storage.hpp"

namespace opentrail::update {
namespace {

const char* key_for_slot(std::uint8_t slot) {
    switch (slot) {
        case 0:
            return kUpdateCheckpointSlotAKey;
        case 1:
            return kUpdateCheckpointSlotBKey;
        default:
            return nullptr;
    }
}

UpdateCheckpointStorageError map_error(
    UpdateCheckpointKvBackendError error) {
    switch (error) {
        case UpdateCheckpointKvBackendError::none:
            return UpdateCheckpointStorageError::none;
        case UpdateCheckpointKvBackendError::not_found:
            return UpdateCheckpointStorageError::not_found;
        case UpdateCheckpointKvBackendError::invalid_argument:
            return UpdateCheckpointStorageError::invalid_argument;
        case UpdateCheckpointKvBackendError::io_failure:
            return UpdateCheckpointStorageError::io_failure;
    }
    return UpdateCheckpointStorageError::io_failure;
}

UpdateCheckpointStorageError commit_pending(
    UpdateCheckpointKvBackend& backend) {
    return map_error(backend.commit(
        kUpdateCheckpointPartitionLabel,
        kUpdateCheckpointNamespace));
}

}  // namespace

UpdateCheckpointKvStorage::UpdateCheckpointKvStorage(
    UpdateCheckpointKvBackend& backend)
    : backend_(backend) {}

UpdateCheckpointStorageError UpdateCheckpointKvStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || output == nullptr ||
        size != kUpdateCheckpointRecordBytes) {
        return UpdateCheckpointStorageError::invalid_argument;
    }

    std::size_t actual_size = 0;
    const auto error = backend_.read_blob(
        kUpdateCheckpointPartitionLabel,
        kUpdateCheckpointNamespace,
        key,
        output,
        size,
        actual_size);
    if (error != UpdateCheckpointKvBackendError::none) {
        return map_error(error);
    }
    return actual_size == kUpdateCheckpointRecordBytes
               ? UpdateCheckpointStorageError::none
               : UpdateCheckpointStorageError::io_failure;
}

UpdateCheckpointStorageError UpdateCheckpointKvStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || data == nullptr ||
        size != kUpdateCheckpointRecordBytes) {
        return UpdateCheckpointStorageError::invalid_argument;
    }

    const auto written = backend_.write_blob(
        kUpdateCheckpointPartitionLabel,
        kUpdateCheckpointNamespace,
        key,
        data,
        size);
    if (written != UpdateCheckpointKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

UpdateCheckpointStorageError UpdateCheckpointKvStorage::erase_slot(
    std::uint8_t slot) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return UpdateCheckpointStorageError::invalid_argument;
    }

    const auto erased = backend_.erase_key(
        kUpdateCheckpointPartitionLabel,
        kUpdateCheckpointNamespace,
        key);
    if (erased == UpdateCheckpointKvBackendError::not_found) {
        return UpdateCheckpointStorageError::none;
    }
    if (erased != UpdateCheckpointKvBackendError::none) {
        return map_error(erased);
    }
    return commit_pending(backend_);
}

}  // namespace opentrail::update
