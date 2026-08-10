#include "memory_persistent_storage.hpp"

#include <algorithm>

namespace opentrail::persistence::test_support {

MemoryPersistentStorage::MemoryPersistentStorage() {
    for (auto& domain : domains_) {
        for (auto& slot : domain) {
            slot.fill(0xFFU);
        }
    }
}

std::size_t MemoryPersistentStorage::domain_index(StorageDomain domain) {
    return static_cast<std::size_t>(domain);
}

bool MemoryPersistentStorage::mutation_fails() {
    if (!power_loss_armed_) {
        return false;
    }
    if (successful_mutations_since_arm_ >=
        successful_mutations_before_failure_) {
        return true;
    }
    ++successful_mutations_since_arm_;
    return false;
}

StorageReadResult MemoryPersistentStorage::read_slot(
    StorageDomain domain,
    std::size_t slot,
    MutableStorageByteView destination) {
    const auto domain_slot = domain_index(domain);
    if (domain_slot >= domains_.size() || slot >= kPersistentSlotCount ||
        destination.data == nullptr ||
        destination.size < kPersistentSlotBytes) {
        return {StorageError::invalid_argument, 0};
    }
    ++counters_[domain_slot].reads;
    if (fail_next_read_) {
        fail_next_read_ = false;
        return {StorageError::io_failure, 0};
    }
    std::copy(
        domains_[domain_slot][slot].begin(),
        domains_[domain_slot][slot].end(),
        destination.data);
    return {StorageError::none, kPersistentSlotBytes};
}

StorageError MemoryPersistentStorage::erase_slot(
    StorageDomain domain,
    std::size_t slot) {
    const auto domain_slot = domain_index(domain);
    if (domain_slot >= domains_.size() || slot >= kPersistentSlotCount) {
        return StorageError::invalid_argument;
    }
    ++counters_[domain_slot].erases;
    if (mutation_fails()) {
        return StorageError::io_failure;
    }
    domains_[domain_slot][slot].fill(0xFFU);
    return StorageError::none;
}

StorageError MemoryPersistentStorage::write_slot(
    StorageDomain domain,
    std::size_t slot,
    std::size_t offset,
    StorageByteView source) {
    const auto domain_slot = domain_index(domain);
    if (domain_slot >= domains_.size() || slot >= kPersistentSlotCount ||
        source.data == nullptr || offset > kPersistentSlotBytes ||
        source.size > kPersistentSlotBytes - offset) {
        return StorageError::invalid_argument;
    }
    ++counters_[domain_slot].writes;
    if (mutation_fails()) {
        return StorageError::io_failure;
    }
    auto& target = domains_[domain_slot][slot];
    for (std::size_t index = 0; index < source.size; ++index) {
        if ((target[offset + index] & source.data[index]) !=
            source.data[index]) {
            return StorageError::write_requires_erase;
        }
    }
    for (std::size_t index = 0; index < source.size; ++index) {
        target[offset + index] = source.data[index];
    }
    return StorageError::none;
}

StorageError MemoryPersistentStorage::sync_slot(
    StorageDomain domain,
    std::size_t slot) {
    const auto domain_slot = domain_index(domain);
    if (domain_slot >= domains_.size() || slot >= kPersistentSlotCount) {
        return StorageError::invalid_argument;
    }
    ++counters_[domain_slot].syncs;
    return mutation_fails() ? StorageError::io_failure : StorageError::none;
}

void MemoryPersistentStorage::arm_power_loss_after(
    std::size_t successful_mutations) {
    successful_mutations_before_failure_ = successful_mutations;
    successful_mutations_since_arm_ = 0;
    power_loss_armed_ = true;
}

void MemoryPersistentStorage::clear_fault() {
    power_loss_armed_ = false;
    successful_mutations_before_failure_ = 0;
    successful_mutations_since_arm_ = 0;
    fail_next_read_ = false;
}

void MemoryPersistentStorage::fail_next_read() {
    fail_next_read_ = true;
}

void MemoryPersistentStorage::seed_slot(
    StorageDomain domain,
    std::size_t slot,
    const std::array<std::uint8_t, kPersistentSlotBytes>& bytes) {
    domains_[domain_index(domain)][slot] = bytes;
}

void MemoryPersistentStorage::corrupt_byte(
    StorageDomain domain,
    std::size_t slot,
    std::size_t offset,
    std::uint8_t xor_mask) {
    domains_[domain_index(domain)][slot][offset] ^= xor_mask;
}

const std::array<std::uint8_t, kPersistentSlotBytes>&
MemoryPersistentStorage::slot_bytes(
    StorageDomain domain,
    std::size_t slot) const {
    return domains_[domain_index(domain)][slot];
}

StorageAccessCounters MemoryPersistentStorage::counters(
    StorageDomain domain) const {
    return counters_[domain_index(domain)];
}

}  // namespace opentrail::persistence::test_support
