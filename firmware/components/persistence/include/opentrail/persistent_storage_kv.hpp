#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/persistent_storage.hpp"

namespace opentrail::persistence {

inline constexpr char kPersistentKvPartitionLabel[] = "ot_state";
inline constexpr char kPersistentKvConfigurationNamespace[] = "ot_config";
inline constexpr char kPersistentKvSecretNamespace[] = "ot_secret";
inline constexpr char kPersistentKvProtocolNamespace[] = "ot_proto";
inline constexpr char kPersistentKvCounterNamespace[] = "ot_counter";
inline constexpr char kPersistentKvSlotAKey[] = "slot_a";
inline constexpr char kPersistentKvSlotBKey[] = "slot_b";

static_assert(sizeof(kPersistentKvPartitionLabel) - 1 <= 15);
static_assert(sizeof(kPersistentKvConfigurationNamespace) - 1 <= 15);
static_assert(sizeof(kPersistentKvSecretNamespace) - 1 <= 15);
static_assert(sizeof(kPersistentKvProtocolNamespace) - 1 <= 15);
static_assert(sizeof(kPersistentKvCounterNamespace) - 1 <= 15);
static_assert(sizeof(kPersistentKvSlotAKey) - 1 <= 15);
static_assert(sizeof(kPersistentKvSlotBKey) - 1 <= 15);

enum class PersistentKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Backend writes and erases stage one mutation. commit() must make it durable
// before returning success. One adapter instance must exclusively own its
// backend handle/transaction; unrelated pending writes are prohibited.
class PersistentKvBackend {
public:
    virtual ~PersistentKvBackend() = default;

    [[nodiscard]] virtual PersistentKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual PersistentKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual PersistentKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) = 0;
    [[nodiscard]] virtual PersistentKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

// Adapts the four-domain, two-slot, flash-like 64-byte PersistentStorage
// contract to isolated key/value namespaces. Partial writes are accumulated in
// RAM only after erase; sync writes and commits one complete 64-byte blob.
// This is NVS-ready common code, not a secret store or ESP-IDF backend.
class PersistentStorageKv final : public PersistentStorage {
public:
    explicit PersistentStorageKv(PersistentKvBackend& backend);

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

private:
    struct WorkingSlot {
        std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
        bool erased{false};
        bool dirty{false};
        bool uncertain{false};
    };

    [[nodiscard]] WorkingSlot* working_slot(
        StorageDomain domain,
        std::size_t slot);

    PersistentKvBackend& backend_;
    std::array<
        std::array<WorkingSlot, kPersistentSlotCount>,
        kStorageDomainCount> working_{};
};

}  // namespace opentrail::persistence
