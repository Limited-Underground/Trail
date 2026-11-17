#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "opentrail/durable_group_roster.hpp"
#include "opentrail/group_pair.hpp"

namespace opentrail::persistence {

inline constexpr char kGroupPairRecordsNamespace[] = "ot_group_pair";

enum class GroupPairRecordsError : std::uint8_t {
    none, unavailable, missing, wrong_incarnation, malformed, storage_failure,
    wrong_binding, wrong_state, bad_epoch,
};

enum class GroupPairPhase : std::uint8_t { empty, prepared, activation_possible, committed, observed };
enum class GroupPairLane : std::uint8_t { data_tx, ack_tx, data_rx, ack_rx };

struct GroupPairHistory {
    GroupPairPhase phase{GroupPairPhase::empty};
    std::uint32_t used_epoch{0};
    std::uint32_t committed_epoch{0};
    bool history{false};
};

// One serialized owner of a peer's public records and one live session. The
// KV handle must be exclusive and separate from roster/index handles. The
// trusted lifecycle caller authenticates peer activation before GroupState
// commits. These records do not store keys or grant radio admission.
class GroupPairRecords final {
public:
    GroupPairRecords(PersistentKvBackend& backend, identity::roster::DurableGroupRoster& roster,
                     identity::roster::GroupState& live_group,
                     const identity::roster::PublicKey& peer_key,
                     const identity::roster::Fingerprint& peer);
    GroupPairRecords(const GroupPairRecords&) = delete;
    GroupPairRecords& operator=(const GroupPairRecords&) = delete;

    // Explicitly allocate a fresh index mapping and record its empty history.
    // An existing mapping with absent records is refused even here.
    [[nodiscard]] GroupPairRecordsError provision();
    // Public recovery facts only, including interrupted intent and its used
    // epoch. Reopening never reauthorizes the saved activation or counters.
    [[nodiscard]] GroupPairRecordsError history(GroupPairHistory& output);

    // A new durable GroupState cannot run this peer before this exact-scope
    // import. Saved intent is retired, and only monotonic public facts survive.
    [[nodiscard]] GroupPairRecordsError hydrate(const identity::roster::RosterCrypto& crypto);
    // Durable mode publishes replies/confirms only after their public history
    // commits and reads back. The raw GroupState output APIs refuse this mode.
    [[nodiscard]] GroupPairRecordsError answer(const identity::roster::PairStart& start,
        const identity::roster::Ephemeral& ephemeral, const identity::roster::BootToken& boot,
        std::uint64_t now_ms, const identity::roster::RosterCrypto& crypto,
        const identity::roster::RosterSigner& signer, identity::roster::RandomSource& random,
        identity::roster::PairReply& output);
    [[nodiscard]] GroupPairRecordsError finish_reply(const identity::roster::PairReply& reply,
        std::uint64_t now_ms, const identity::roster::RosterCrypto& crypto,
        const identity::roster::RosterSigner& signer, identity::roster::PairConfirm& output,
        std::optional<identity::roster::VerifiedPairBinding>& proof);
    [[nodiscard]] GroupPairRecordsError finish_confirm(const identity::roster::PairConfirm& confirm,
        std::uint64_t now_ms, const identity::roster::RosterCrypto& crypto,
        std::optional<identity::roster::VerifiedPairBinding>& proof);

    [[nodiscard]] GroupPairRecordsError prepare(const identity::roster::VerifiedPairBinding& proof);
    [[nodiscard]] GroupPairRecordsError mark_activation_possible();
    [[nodiscard]] GroupPairRecordsError commit();
    // Borrow while this exact live, committed binding owns the lane. The
    // caller supplies each lane's existing counter/replay owner exclusively.
    [[nodiscard]] PersistentStorage* storage(GroupPairLane lane);

private:
    struct Record;
    using Scope = identity::roster::DurablePairScope;
    using Error = GroupPairRecordsError;
    using Image = std::array<std::uint8_t, kPersistentSlotCount * kPersistentSlotBytes>;

    class LaneStorage final : public PersistentStorage {
    public:
        LaneStorage(GroupPairRecords& owner, GroupPairLane lane) : owner_(owner), lane_(lane) {}
        StorageReadResult read_slot(StorageDomain domain, std::size_t slot,
                                    MutableStorageByteView destination) override;
        StorageError erase_slot(StorageDomain domain, std::size_t slot) override;
        StorageError write_slot(StorageDomain domain, std::size_t slot, std::size_t offset,
                                StorageByteView source) override;
        StorageError sync_slot(StorageDomain domain, std::size_t slot) override;
    private:
        struct WorkingSlot {
            std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
            bool erased{false};
            bool dirty{false};
        };
        static bool valid_slot(StorageDomain domain, std::size_t slot);
        GroupPairRecords& owner_;
        const GroupPairLane lane_;
        std::array<WorkingSlot, kPersistentSlotCount> working_{};
    };

    std::optional<Scope> current_scope();
    bool current_binding(const Scope& scope, identity::roster::PairStep step) const;
    Error load_record(std::size_t slot, Record& output);
    Error persist_start(const identity::roster::VerifiedPairStart& proof);
    Error store_record(const Scope& scope, Record& record);
    Error load_lane(const Scope& scope, std::uint32_t epoch, GroupPairLane lane, Image& output);
    Error store_lane(const Scope& scope, std::uint32_t epoch, GroupPairLane lane, const Image& input);
    Error check_lanes(const Scope& scope, std::uint32_t epoch);
    Error live_record(Record& output, GroupPairPhase phase, identity::roster::PairStep step);
    Error transition(GroupPairPhase from, GroupPairPhase to, identity::roster::PairStep step);
    bool access(GroupPairLane lane, Image& output);
    bool save(GroupPairLane lane, const Image& input);
    bool write_verified(const char* key, const std::uint8_t* bytes, std::size_t size,
                        std::uint8_t* readback);

    PersistentKvBackend& backend_;
    identity::roster::DurableGroupRoster& roster_;
    identity::roster::GroupState& live_group_;
    const identity::roster::PublicKey peer_key_;
    const identity::roster::Fingerprint peer_;
    std::optional<Scope> scope_;
    std::uint64_t binding_id_{0};
    std::uint32_t epoch_{0};
    bool faulted_{false};
    // One authoritative image per lane for this live binding. Only verified
    // owner writes advance it. Retiring the binding discards all four floors.
    std::array<std::optional<Image>, 4> lane_floors_{};
    std::array<LaneStorage, 4> lanes_;
};

}  // namespace opentrail::persistence
