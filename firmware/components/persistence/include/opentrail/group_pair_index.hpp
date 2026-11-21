#pragma once

#include <array>
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
    // Format 2 stores each pair's group as one index into the qualified group
    // records. Full group and peer identities remain part of every lookup.
    static constexpr std::size_t kMaximumSerializedBytes = 5 + identity::roster::kFingerprintBytes + 8 +
        kMaximumPairs * (8 + 1 + identity::roster::kFingerprintBytes + 3 * 4) +
        kMaximumGroups * (identity::roster::kFingerprintBytes + 8 + 4 + identity::roster::kDigestBytes) + 4;
    static_assert(kMaximumSerializedBytes <= 2048);
    static_assert(kMaximumGroups < 0xff);

    GroupPairIndex(PersistentKvBackend& backend, const identity::roster::Fingerprint& local)
        : backend_(backend), local_(local) {}
    GroupPairIndex(const GroupPairIndex&) = delete;
    GroupPairIndex& operator=(const GroupPairIndex&) = delete;

    // Explicit commissioning only. Normal lookups and mutations refuse a missing catalog.
    [[nodiscard]] GroupPairIndexError initialize();
    [[nodiscard]] GroupPairIndexError reopen();
    [[nodiscard]] const identity::roster::Fingerprint& local_identity() const { return local_; }
    [[nodiscard]] GroupPairIndexError group_status(const identity::roster::GroupName& group,
                                                    bool require_capacity = false);
    [[nodiscard]] GroupPairIndexResult find(const identity::roster::RosterState& roster,
                                            const identity::roster::Fingerprint& peer);
    [[nodiscard]] GroupPairIndexResult reserve(const identity::roster::RosterState& roster,
                                               const identity::roster::Fingerprint& peer);

    // Apply the committed, current roster watermark and retire obsolete pairs atomically.
    // A left or distrusted roster retires all of its pairs and keeps its watermark.
    // All inactive rosters block lookup and reserve.
    [[nodiscard]] GroupPairIndexError prune(const identity::roster::RosterState& roster);

private:
    struct RosterSnapshot;
    struct Entry {
        std::uint64_t incarnation{0};
        identity::roster::GroupName group{};
        identity::roster::Fingerprint peer{};
        std::uint32_t local_added{0}, peer_added{0}, roster_version{0};
        bool names(const identity::roster::GroupName& name,
                   const identity::roster::Fingerprint& fingerprint) const;
        bool valid(const identity::roster::Fingerprint& local, std::uint64_t high_water) const;
    };
    struct GroupWatermark {
        identity::roster::GroupName group{};
        std::uint32_t version{0};
        identity::roster::Digest digest{};
        bool current(const RosterSnapshot& roster) const;
    };
    static constexpr std::size_t kEntryBytes = 8 + 1 + identity::roster::kFingerprintBytes + 3 * 4;
    static constexpr std::size_t kGroupBytes =
        identity::roster::kFingerprintBytes + 8 + 4 + identity::roster::kDigestBytes;
    static constexpr std::size_t kBodyBytes = 5 + identity::roster::kFingerprintBytes + 8 +
        kMaximumPairs * kEntryBytes + kMaximumGroups * kGroupBytes;
    static_assert(kBodyBytes + 4 == kMaximumSerializedBytes);
    using Blob = std::array<std::uint8_t, kMaximumSerializedBytes>;
    struct Catalog {
        std::uint64_t high_water{0};
        std::array<Entry, kMaximumPairs> entries{};
        std::array<GroupWatermark, kMaximumGroups> groups{};
        GroupWatermark* group_for(const identity::roster::GroupName& name);
        std::size_t pair_count(const identity::roster::GroupName& group) const;
        bool valid(const identity::roster::Fingerprint& local);
        void encode(const identity::roster::Fingerprint& local, Blob& blob) const;
        GroupPairIndexError decode(const Blob& blob, const identity::roster::Fingerprint& local);
    };
    struct Workspace {
        Catalog catalog;
        Blob serialized{}, readback{};
    };
    GroupPairIndexError load(Catalog& catalog);
    GroupPairIndexError save(const Catalog& catalog);
    GroupPairIndexResult find_or_reserve(const identity::roster::RosterState& roster,
                                         const identity::roster::Fingerprint& peer, bool reserve);

    PersistentKvBackend& backend_;
    const identity::roster::Fingerprint local_;
    // One fixed workspace per serialized index owner. Backend callbacks cannot
    // re-enter the owner while its decoded catalog or transfer buffers are used.
    Workspace workspace_{};
    bool busy_{false};
    bool faulted_{false};
};

}  // namespace opentrail::persistence
