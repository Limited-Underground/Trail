#pragma once

#include "opentrail/group_pair_index.hpp"
#include "opentrail/group_roster.hpp"

namespace opentrail::identity::roster {

class GroupState;

enum class DurableRosterError : std::uint8_t {
    none = 0,
    missing,
    capacity,
    invalid_scope,
    invalid_record,
    storage_failure,
    index_failure,
    not_ready,
    refused,
};

struct DurableRosterResult {
    DurableRosterError error;
    Outcome outcome{Outcome::not_initialized};
};

struct DurableRosterSlot {
    PublicKey admin{}, local{};
    std::uint64_t group_id{0};
    std::uint32_t minimum_version{0};
};

struct DurableRosterSnapshot {
    GroupName group{};
    Digest digest{};
    State state{State::uninitialized};
    std::uint32_t version{0};
    std::size_t bytes{0};
};

struct DurablePairScope {
    GroupName group;
    Fingerprint local, peer;
    std::uint32_t local_added, peer_added, roster_version;
    Digest roster_digest;
    persistence::GroupPairSlot location;
};

// One serialized owner per qualified roster. The backend may only stage writes
// for this owner. Its blob replacement is atomic and reads see committed bytes.
// Retire the owner and handle after an uncertain write. Reopen with fresh owners
// replays the idempotent index reconciliation before releasing a pair scope.
class DurableGroupRoster {
public:
    static constexpr std::size_t kStorageRecordBytes = 4 + 8 + 2 * kPublicKeyBytes +
        4 + 5 + 2 + kDigestBytes + kKnownDigests * (4 + kDigestBytes) +
        kResolvedConflicts * kDigestBytes + kMaximumRosterBytes + sizeof(std::uint32_t);

    DurableGroupRoster(persistence::PersistentKvBackend& backend, persistence::GroupPairIndex& index,
                       const RosterCrypto& crypto, const PublicKey& admin, std::uint64_t group,
                       const PublicKey& local, std::uint32_t minimum_version = 1);
    DurableGroupRoster(const DurableGroupRoster&) = delete;
    DurableGroupRoster& operator=(const DurableGroupRoster&) = delete;

    // Explicit creation of a fresh roster, after commissioning the pair index.
    [[nodiscard]] DurableRosterError initialize();
    // Commits a signed version-one self-admin roster as the initial record.
    [[nodiscard]] DurableRosterError create_owned(const std::uint8_t* bytes, std::size_t size);
    [[nodiscard]] DurableRosterError reopen();
    // Slot metadata locates the owner. Reopen verifies its signed roster and trust state.
    [[nodiscard]] static DurableRosterError inspect_slot(persistence::PersistentKvBackend& backend,
                                                         std::size_t slot, DurableRosterSlot& output);
    [[nodiscard]] DurableRosterError snapshot(DurableRosterSnapshot& output, std::uint8_t* bytes,
                                               std::size_t capacity);
    // Verify the exact signed active roster without reopening or reconciling
    // the pair catalog. This read never writes or grants pair authority.
    [[nodiscard]] std::optional<LocationRule> read_active_location_rule(
        std::size_t slot, std::uint32_t version, const Fingerprint& admin, const Digest& digest);
    // Restore the verified roster and retained member consent into a fresh
    // durable GroupState. Pair epoch history is imported separately.
    [[nodiscard]] DurableRosterError hydrate_group(GroupState& live);
    [[nodiscard]] DurableRosterResult offer(const std::uint8_t* bytes, std::size_t size);
    [[nodiscard]] DurableRosterError accept_member(const PublicKey& peer);
    [[nodiscard]] DurableRosterError keep_current();
    [[nodiscard]] DurableRosterError stop_trusting();
    [[nodiscard]] std::size_t copy_signed_roster(std::uint8_t* out, std::size_t capacity);
    [[nodiscard]] std::optional<DurablePairScope> find_pair(const Fingerprint& peer);
    // Only creates a new mapping. An existing mapping with missing pair records
    // cannot be reissued as fresh after an interrupted provisioning attempt.
    [[nodiscard]] std::optional<DurablePairScope> reserve_pair(const Fingerprint& peer);

private:
    struct Record {
        static constexpr std::size_t kScope = 4 + 8 + 2 * kPublicKeyBytes;
        static constexpr std::size_t kCrc = kStorageRecordBytes - sizeof(std::uint32_t);
        std::array<std::uint8_t, kStorageRecordBytes> bytes{};
        bool intact() const;
        bool names(std::uint64_t group, const PublicKey& admin, const PublicKey& local) const;
    };
    struct Workspace {
        RosterState next;
        Roster parsed;
        Record serialized, readback;
    };
    DurableRosterError open(bool create, const std::uint8_t* initial = nullptr, std::size_t size = 0);
    static DurableRosterError read(persistence::PersistentKvBackend& backend, const char* key, Record& record);
    DurableRosterError verify_current();
    DurableRosterError reconcile();
    DurableRosterError save(const char* key, const RosterState& next);
    std::optional<DurablePairScope> lookup(const Fingerprint& peer, bool reserve);
    bool encode_record(const RosterState& state, Record& out) const;
    bool decode_record(const Record& record, RosterState& out);

    persistence::PersistentKvBackend& backend_;
    persistence::GroupPairIndex& index_;
    const RosterCrypto& crypto_;
    const PublicKey admin_, local_;
    const std::uint64_t group_;
    const std::uint32_t minimum_version_;
    GroupName group_name_{};
    Fingerprint local_fingerprint_{};
    RosterState state_{};
    // Transfer and candidate buffers belong to the serialized owner so that
    // roster save and index reconciliation can share a bounded call stack.
    Workspace workspace_{};
    const char* key_{nullptr};
    bool ready_{false};
    bool busy_{false};
    bool faulted_{false};
};

}  // namespace opentrail::identity::roster
