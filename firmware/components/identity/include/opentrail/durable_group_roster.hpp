#pragma once

#include "opentrail/group_pair_index.hpp"
#include "opentrail/group_roster.hpp"

namespace opentrail::identity::roster {

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
    DurableGroupRoster(persistence::PersistentKvBackend& backend, persistence::GroupPairIndex& index,
                       const RosterCrypto& crypto, const PublicKey& admin, std::uint64_t group,
                       const PublicKey& local, std::uint32_t minimum_version = 1);
    DurableGroupRoster(const DurableGroupRoster&) = delete;
    DurableGroupRoster& operator=(const DurableGroupRoster&) = delete;

    // Explicit creation of a fresh roster, after commissioning the pair index.
    [[nodiscard]] DurableRosterError initialize();
    [[nodiscard]] DurableRosterError reopen();
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
    struct Record;
    DurableRosterError open(bool create);
    DurableRosterError read(const char* key, Record& record);
    DurableRosterError verify_current();
    DurableRosterError reconcile();
    DurableRosterError save(const char* key, const RosterState& next);
    std::optional<DurablePairScope> lookup(const Fingerprint& peer, bool reserve);
    bool encode_record(const RosterState& state, Record& out) const;
    bool decode_record(const Record& record, RosterState& out) const;

    persistence::PersistentKvBackend& backend_;
    persistence::GroupPairIndex& index_;
    const RosterCrypto& crypto_;
    const PublicKey admin_, local_;
    const std::uint64_t group_;
    const std::uint32_t minimum_version_;
    GroupName group_name_{};
    Fingerprint local_fingerprint_{};
    RosterState state_{};
    const char* key_{nullptr};
    bool ready_{false};
    bool busy_{false};
    bool faulted_{false};
};

}  // namespace opentrail::identity::roster
