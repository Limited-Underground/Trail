#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/persistent_storage.hpp"

namespace opentrail::persistence::test_support {

struct StorageAccessCounters {
    std::uint32_t reads{0};
    std::uint32_t erases{0};
    std::uint32_t writes{0};
    std::uint32_t syncs{0};
};

class MemoryPersistentStorage final : public PersistentStorage {
public:
    MemoryPersistentStorage();

    StorageReadResult read_slot(
        StorageDomain domain,
        std::size_t slot,
        MutableStorageByteView destination) override;
    StorageError erase_slot(
        StorageDomain domain,
        std::size_t slot) override;
    StorageError write_slot(
        StorageDomain domain,
        std::size_t slot,
        std::size_t offset,
        StorageByteView source) override;
    StorageError sync_slot(
        StorageDomain domain,
        std::size_t slot) override;

    void arm_power_loss_after(std::size_t successful_mutations);
    void clear_fault();
    void fail_next_read();
    void seed_slot(
        StorageDomain domain,
        std::size_t slot,
        const std::array<std::uint8_t, kPersistentSlotBytes>& bytes);
    void corrupt_byte(
        StorageDomain domain,
        std::size_t slot,
        std::size_t offset,
        std::uint8_t xor_mask);
    [[nodiscard]] const std::array<std::uint8_t, kPersistentSlotBytes>&
    slot_bytes(StorageDomain domain, std::size_t slot) const;
    [[nodiscard]] StorageAccessCounters counters(StorageDomain domain) const;

private:
    static std::size_t domain_index(StorageDomain domain);
    bool mutation_fails();

    using Slot = std::array<std::uint8_t, kPersistentSlotBytes>;
    using DomainSlots = std::array<Slot, kPersistentSlotCount>;
    std::array<DomainSlots, kStorageDomainCount> domains_{};
    std::array<StorageAccessCounters, kStorageDomainCount> counters_{};
    std::size_t successful_mutations_before_failure_{0};
    std::size_t successful_mutations_since_arm_{0};
    bool power_loss_armed_{false};
    bool fail_next_read_{false};
};

}  // namespace opentrail::persistence::test_support
