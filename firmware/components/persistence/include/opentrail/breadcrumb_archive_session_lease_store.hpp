#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/persistent_storage.hpp"

namespace opentrail::persistence {

inline constexpr std::uint8_t
    kBreadcrumbArchiveSessionLeaseEnvelopeVersion = 1;
inline constexpr std::uint8_t
    kBreadcrumbArchiveSessionLeaseSchemaVersion = 1;
inline constexpr std::size_t
    kBreadcrumbArchiveSessionLeaseCommitOffset = 60;
inline constexpr std::uint32_t
    kBreadcrumbArchiveSessionLeaseCommitMarker = 0xB7EA5E55U;

enum class BreadcrumbArchiveSessionLeaseError : std::uint8_t {
    none = 0,
    invalid_request,
    no_valid_state,
    storage_failure,
    integrity_failure,
    unsupported_version,
    generation_conflict,
    generation_exhausted,
    session_exhausted,
    verification_failure,
};

enum class BreadcrumbArchiveSessionLeaseSlotState : std::uint8_t {
    blank = 0,
    valid,
    uncommitted,
    malformed,
    integrity_failure,
    unsupported_version,
    storage_failure,
};

struct BreadcrumbArchiveSessionLeaseRequest {
    std::uint64_t initial_session_id{0};
    std::uint32_t lease_size{0};
};

struct BreadcrumbArchiveSessionLeaseAllocation {
    BreadcrumbArchiveSessionLeaseError error{
        BreadcrumbArchiveSessionLeaseError::no_valid_state};
    std::uint32_t generation{0};
    std::uint64_t first_session_id{0};
    std::uint64_t final_session_id{0};
    std::size_t written_slot{kPersistentSlotCount};
    std::array<
        BreadcrumbArchiveSessionLeaseSlotState,
        kPersistentSlotCount> slot_states{};
    bool initialized{false};

    [[nodiscard]] constexpr bool allocated() const {
        return error == BreadcrumbArchiveSessionLeaseError::none;
    }
};

// Reserves a nonoverlapping durable session-ID range before returning any ID
// from it. Unused IDs are deliberately abandoned on reboot. The record carries
// no device, user, group, endpoint, location, or credential identity.
class BreadcrumbArchiveSessionLeaseStore {
public:
    explicit BreadcrumbArchiveSessionLeaseStore(PersistentStorage& storage);

    [[nodiscard]] BreadcrumbArchiveSessionLeaseAllocation allocate(
        const BreadcrumbArchiveSessionLeaseRequest& request);

private:
    PersistentStorage& storage_;
};

}  // namespace opentrail::persistence
