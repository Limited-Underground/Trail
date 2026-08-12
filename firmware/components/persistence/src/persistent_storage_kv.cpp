#include "opentrail/persistent_storage_kv.hpp"

#include <algorithm>

namespace opentrail::persistence {
namespace {

std::size_t domain_index(StorageDomain domain) {
    return static_cast<std::size_t>(domain);
}

const char* namespace_for_domain(StorageDomain domain) {
    switch (domain) {
        case StorageDomain::configuration:
            return kPersistentKvConfigurationNamespace;
        case StorageDomain::secret_material:
            return kPersistentKvSecretNamespace;
        case StorageDomain::protocol_state:
            return kPersistentKvProtocolNamespace;
        case StorageDomain::outbound_counter_state:
            return kPersistentKvCounterNamespace;
        case StorageDomain::breadcrumb_archive_state:
            return kPersistentKvArchiveNamespace;
    }
    return nullptr;
}

const char* key_for_slot(std::size_t slot) {
    switch (slot) {
        case 0:
            return kPersistentKvSlotAKey;
        case 1:
            return kPersistentKvSlotBKey;
        default:
            return nullptr;
    }
}

StorageError map_error(PersistentKvBackendError error) {
    switch (error) {
        case PersistentKvBackendError::none:
            return StorageError::none;
        case PersistentKvBackendError::invalid_argument:
            return StorageError::invalid_argument;
        case PersistentKvBackendError::not_found:
        case PersistentKvBackendError::io_failure:
            return StorageError::io_failure;
    }
    return StorageError::io_failure;
}

}  // namespace

PersistentStorageKv::PersistentStorageKv(PersistentKvBackend& backend)
    : backend_(backend) {
    for (auto& domain : working_) {
        for (auto& slot : domain) {
            slot.bytes.fill(0xFFU);
        }
    }
}

PersistentStorageKv::WorkingSlot* PersistentStorageKv::working_slot(
    StorageDomain domain,
    std::size_t slot) {
    const auto index = domain_index(domain);
    if (index >= working_.size() || slot >= kPersistentSlotCount) {
        return nullptr;
    }
    return &working_[index][slot];
}

StorageReadResult PersistentStorageKv::read_slot(
    StorageDomain domain,
    std::size_t slot,
    MutableStorageByteView destination) {
    const auto* namespace_name = namespace_for_domain(domain);
    const auto* key = key_for_slot(slot);
    if (namespace_name == nullptr || key == nullptr ||
        destination.data == nullptr ||
        destination.size != kPersistentSlotBytes) {
        return {StorageError::invalid_argument, 0};
    }

    std::size_t actual_size = 0;
    const auto read = backend_.read_blob(
        kPersistentKvPartitionLabel,
        namespace_name,
        key,
        destination.data,
        destination.size,
        actual_size);
    if (read == PersistentKvBackendError::not_found) {
        std::fill(
            destination.data,
            destination.data + destination.size,
            0xFFU);
        return {StorageError::none, kPersistentSlotBytes};
    }
    if (read != PersistentKvBackendError::none) {
        return {map_error(read), 0};
    }
    if (actual_size != kPersistentSlotBytes) {
        return {StorageError::io_failure, 0};
    }
    return {StorageError::none, kPersistentSlotBytes};
}

StorageError PersistentStorageKv::erase_slot(
    StorageDomain domain,
    std::size_t slot) {
    const auto* namespace_name = namespace_for_domain(domain);
    const auto* key = key_for_slot(slot);
    auto* working = working_slot(domain, slot);
    if (namespace_name == nullptr || key == nullptr || working == nullptr) {
        return StorageError::invalid_argument;
    }

    const auto erased = backend_.erase_key(
        kPersistentKvPartitionLabel,
        namespace_name,
        key);
    if (erased != PersistentKvBackendError::none &&
        erased != PersistentKvBackendError::not_found) {
        working->uncertain = true;
        working->erased = false;
        working->dirty = false;
        return map_error(erased);
    }
    if (erased == PersistentKvBackendError::none) {
        const auto committed = backend_.commit(
            kPersistentKvPartitionLabel, namespace_name);
        if (committed != PersistentKvBackendError::none) {
            working->uncertain = true;
            working->erased = false;
            working->dirty = false;
            return map_error(committed);
        }
    }

    working->bytes.fill(0xFFU);
    working->erased = true;
    working->dirty = false;
    working->uncertain = false;
    return StorageError::none;
}

StorageError PersistentStorageKv::write_slot(
    StorageDomain domain,
    std::size_t slot,
    std::size_t offset,
    StorageByteView source) {
    auto* working = working_slot(domain, slot);
    if (namespace_for_domain(domain) == nullptr ||
        key_for_slot(slot) == nullptr || working == nullptr ||
        source.data == nullptr || source.size == 0 ||
        offset > kPersistentSlotBytes ||
        source.size > kPersistentSlotBytes - offset) {
        return StorageError::invalid_argument;
    }
    if (!working->erased || working->uncertain) {
        return StorageError::write_requires_erase;
    }
    for (std::size_t index = 0; index < source.size; ++index) {
        const auto current = working->bytes[offset + index];
        const auto next = source.data[index];
        if ((current & next) != next) {
            return StorageError::write_requires_erase;
        }
    }
    std::copy(
        source.data,
        source.data + source.size,
        working->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    working->dirty = true;
    return StorageError::none;
}

StorageError PersistentStorageKv::sync_slot(
    StorageDomain domain,
    std::size_t slot) {
    const auto* namespace_name = namespace_for_domain(domain);
    const auto* key = key_for_slot(slot);
    auto* working = working_slot(domain, slot);
    if (namespace_name == nullptr || key == nullptr || working == nullptr) {
        return StorageError::invalid_argument;
    }
    if (!working->erased || working->uncertain) {
        return StorageError::write_requires_erase;
    }
    if (!working->dirty) {
        return StorageError::none;
    }

    const auto written = backend_.write_blob(
        kPersistentKvPartitionLabel,
        namespace_name,
        key,
        working->bytes.data(),
        working->bytes.size());
    if (written != PersistentKvBackendError::none) {
        working->uncertain = true;
        working->erased = false;
        working->dirty = false;
        return map_error(written);
    }
    const auto committed = backend_.commit(
        kPersistentKvPartitionLabel, namespace_name);
    if (committed != PersistentKvBackendError::none) {
        working->uncertain = true;
        working->erased = false;
        working->dirty = false;
        return map_error(committed);
    }
    working->dirty = false;
    return StorageError::none;
}

}  // namespace opentrail::persistence
