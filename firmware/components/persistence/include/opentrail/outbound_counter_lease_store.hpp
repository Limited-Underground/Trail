#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/persistent_storage.hpp"

namespace opentrail::persistence {

inline constexpr std::uint8_t kOutboundCounterEnvelopeVersion = 1;
inline constexpr std::uint8_t kOutboundCounterSchemaVersion = 1;
inline constexpr std::size_t kOutboundCounterCommitOffset = 60;
inline constexpr std::uint32_t kOutboundCounterCommitMarker = 0xC0171EA5U;
inline constexpr std::uint32_t kMaximumOutboundCounterLeaseSize = 65536;

using CounterDomainId = std::array<std::uint8_t, 16>;

enum class OutboundCounterError : std::uint8_t {
    none = 0,
    invalid_request,
    not_started,
    already_started,
    no_valid_state,
    storage_failure,
    integrity_failure,
    unsupported_version,
    generation_conflict,
    domain_mismatch,
    generation_exhausted,
    counter_exhausted,
    verification_failure,
};

enum class OutboundCounterSlotState : std::uint8_t {
    blank = 0,
    valid,
    uncommitted,
    malformed,
    integrity_failure,
    unsupported_version,
    storage_failure,
};

struct OutboundCounterLeaseRequest {
    CounterDomainId domain_id{};
    std::uint32_t group_epoch{0};
    std::uint32_t lease_size{0};
};

struct OutboundCounterLease {
    OutboundCounterError error{OutboundCounterError::no_valid_state};
    std::uint64_t first_counter{0};
    std::uint64_t last_counter{0};
    std::uint32_t generation{0};
    std::size_t written_slot{kPersistentSlotCount};
    std::array<OutboundCounterSlotState, kPersistentSlotCount> slot_states{};
    bool initialized{false};

    [[nodiscard]] constexpr bool reserved() const {
        return error == OutboundCounterError::none;
    }
};

struct OutboundCounterAllocation {
    OutboundCounterError error{OutboundCounterError::not_started};
    std::uint64_t counter{0};

    [[nodiscard]] constexpr bool allocated() const {
        return error == OutboundCounterError::none;
    }
};

// Durably advances the high-water counter before returning a lease. A reset may
// waste unused counters from a lease, but cannot return them again for the same
// domain and epoch. Key rotation must change the domain before storage reset.
class OutboundCounterLeaseStore {
public:
    explicit OutboundCounterLeaseStore(PersistentStorage& storage);

    [[nodiscard]] OutboundCounterLease reserve(
        const OutboundCounterLeaseRequest& request);

private:
    PersistentStorage& storage_;
};

// Fixed-memory consumer that serves counters from a durable lease and reserves
// the next lease before returning its first counter.
class OutboundCounterAllocator {
public:
    explicit OutboundCounterAllocator(OutboundCounterLeaseStore& store);

    [[nodiscard]] OutboundCounterError start(
        const OutboundCounterLeaseRequest& request);
    [[nodiscard]] OutboundCounterAllocation next();

private:
    [[nodiscard]] OutboundCounterError reserve_next();

    OutboundCounterLeaseStore& store_;
    OutboundCounterLeaseRequest request_{};
    std::uint64_t next_counter_{0};
    std::uint64_t lease_last_{0};
    bool started_{false};
    bool has_remaining_{false};
};

}  // namespace opentrail::persistence
