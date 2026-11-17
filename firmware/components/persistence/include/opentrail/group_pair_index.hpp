#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "opentrail/group_roster.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::persistence {

enum class GroupPairIndexError : std::uint8_t {
    none = 0,
    missing,
    not_indexed,
    invalid_scope,
    roster_inactive,
    stale_roster,
    stale_membership,
    invalid_record,
    storage_failure,
    capacity,
    exhausted,
};

struct GroupPairSlot {
    std::size_t slot;
    std::uint64_t incarnation;
};

struct GroupPairIndexResult {
    GroupPairIndexError error;
    std::optional<GroupPairSlot> location{};
};

// Index for the group portion of the V1 pair budget (four groups, five peers each).
// The caller serializes the index and exclusively owns the KV handle. Reserve and
// prune take the current accepted roster after its durable commit and readback.
// A slot names storage, not traffic authority. Open a store only after lookup
// against the current roster, and match its records to the exact scope AND the
// incarnation. A recycled slot must never inherit old pair or counter records.
class GroupPairIndex {
public:
    static constexpr std::size_t kMaximumGroups = 4;
    static constexpr std::size_t kPeersPerGroup = identity::roster::kMaximumMembers - 1;
    static constexpr std::size_t kMaximumPairs = kMaximumGroups * kPeersPerGroup;

    GroupPairIndex(PersistentKvBackend& backend, const identity::roster::Fingerprint& local)
        : backend_(backend), local_(local) {}
    GroupPairIndex(const GroupPairIndex&) = delete;
    GroupPairIndex& operator=(const GroupPairIndex&) = delete;

    // Explicit commissioning only. Normal lookups and mutations refuse a missing catalog.
    [[nodiscard]] GroupPairIndexError initialize();
    [[nodiscard]] GroupPairIndexResult find(const identity::roster::RosterState& roster,
                                            const identity::roster::Fingerprint& peer);
    [[nodiscard]] GroupPairIndexResult reserve(const identity::roster::RosterState& roster,
                                               const identity::roster::Fingerprint& peer);

    // Retire obsolete pair mappings for a still-active group after its roster changed.
    // Persist the roster watermark even if this update retires the last pair.
    // Group-wide retention on leave/distrust is separate. All inactive rosters block use.
    [[nodiscard]] GroupPairIndexError prune(const identity::roster::RosterState& roster);

private:
    struct Catalog;
    GroupPairIndexError load(Catalog& catalog);
    GroupPairIndexError save(const Catalog& catalog);
    GroupPairIndexResult find_or_reserve(const identity::roster::RosterState& roster,
                                         const identity::roster::Fingerprint& peer, bool reserve);

    PersistentKvBackend& backend_;
    const identity::roster::Fingerprint local_;
    bool faulted_{false};
};

}  // namespace opentrail::persistence
