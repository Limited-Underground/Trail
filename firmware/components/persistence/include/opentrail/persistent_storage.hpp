#pragma once

#include <cstddef>
#include <cstdint>

namespace opentrail::persistence {

inline constexpr std::size_t kPersistentSlotCount = 2;
inline constexpr std::size_t kPersistentSlotBytes = 64;

enum class StorageDomain : std::uint8_t {
    configuration = 0,
    secret_material = 1,
    protocol_state = 2,
};

struct StorageByteView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

struct MutableStorageByteView {
    std::uint8_t* data{nullptr};
    std::size_t size{0};
};

enum class StorageError : std::uint8_t {
    none = 0,
    invalid_argument,
    io_failure,
    write_requires_erase,
};

struct StorageReadResult {
    StorageError error{StorageError::none};
    std::size_t bytes_read{0};

    [[nodiscard]] constexpr bool read() const {
        return error == StorageError::none;
    }
};

class PersistentStorage {
public:
    virtual ~PersistentStorage() = default;

    virtual StorageReadResult read_slot(
        StorageDomain domain,
        std::size_t slot,
        MutableStorageByteView destination) = 0;
    virtual StorageError erase_slot(
        StorageDomain domain,
        std::size_t slot) = 0;
    virtual StorageError write_slot(
        StorageDomain domain,
        std::size_t slot,
        std::size_t offset,
        StorageByteView source) = 0;
    virtual StorageError sync_slot(
        StorageDomain domain,
        std::size_t slot) = 0;
};

}  // namespace opentrail::persistence
