#include "opentrail/group_pair_records.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <type_traits>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "opentrail/evaluation_replay_store.hpp"
#include "opentrail/outbound_counter_lease_store.hpp"
#include "opentrail/configuration_store.hpp"

namespace {

using namespace opentrail::identity::roster;
using namespace opentrail::persistence;
using Error = GroupPairRecordsError;
using KvError = PersistentKvBackendError;
using Blob = std::vector<std::uint8_t>;
using Disk = std::map<std::string, Blob>;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << "FAIL " << __LINE__ << ": " << #expression << '\n'; \
        std::exit(1); \
    } \
} while (false)

enum class Fault { none, write, unapplied, applied, readback };

class Storage final : public PersistentKvBackend {
public:
    explicit Storage(Disk& disk) : disk_(disk) {}

    KvError read_blob(const char* partition, const char* space, const char* key,
                      std::uint8_t* out, std::size_t capacity, std::size_t& size) override {
        CHECK(std::string(partition) == "ot_state");
        if (failed_readback_) return KvError::io_failure;
        const auto found = disk_.find(path(space, key));
        if (found == disk_.end()) return KvError::not_found;
        size = found->second.size();
        if (size > capacity) return KvError::invalid_argument;
        std::copy(found->second.begin(), found->second.end(), out);
        return KvError::none;
    }

    KvError write_blob(const char* partition, const char* space, const char* key,
                       const std::uint8_t* data, std::size_t size) override {
        CHECK(std::string(partition) == "ot_state");
        CHECK(pending_.empty());
        ++writes;
        if (writes == cut && fault == Fault::write) return KvError::io_failure;
        pending_key_ = path(space, key);
        pending_.assign(data, data + size);
        return KvError::none;
    }

    KvError erase_key(const char*, const char*, const char*) override {
        CHECK(false);
        return KvError::io_failure;
    }

    KvError commit(const char* partition, const char* space) override {
        CHECK(std::string(partition) == "ot_state");
        CHECK(!pending_.empty() && pending_key_.find(std::string(space) + "/") == 0);
        if (before_commit) before_commit();
        const auto outcome = writes == cut ? fault : Fault::none;
        if (outcome == Fault::unapplied) return KvError::io_failure;
        disk_[pending_key_] = std::move(pending_);
        pending_.clear();
        failed_readback_ = outcome == Fault::readback;
        return outcome == Fault::applied ? KvError::io_failure : KvError::none;
    }

    std::size_t writes{0}, cut{0};
    Fault fault{Fault::none};
    std::function<void()> before_commit;

private:
    static std::string path(const char* space, const char* key) { return std::string(space) + "/" + key; }

    Disk& disk_;
    Blob pending_;
    std::string pending_key_;
    bool failed_readback_{false};
};

template <std::size_t Size>
void hash(const std::uint8_t* bytes, std::size_t length, std::uint64_t seed,
          std::array<std::uint8_t, Size>& out) {
    std::uint64_t state = 1469598103934665603ULL ^ seed;
    for (std::size_t i = 0; i < length; ++i) state = (state ^ bytes[i]) * 1099511628211ULL;
    for (auto& byte : out) {
        state ^= state >> 19;
        state *= 1099511628211ULL;
        byte = static_cast<std::uint8_t>(state >> 39);
    }
}

Signature signature(const PublicKey& key, const std::uint8_t* data, std::size_t size) {
    Signature value{};
    hash(data, size, key.back(), value);
    return value;
}

// Deterministic TEST ONLY providers. GroupState still verifies the real signed
// transcript, but no test here makes a cryptographic or real-key claim.
class Crypto final : public RosterCrypto {
public:
    bool verify(const Signature& sig, const std::uint8_t* data, std::size_t size,
                const PublicKey& key) const override {
        return sig == signature(key, data, size);
    }
    bool digest(const std::uint8_t* data, std::size_t size, Digest& out) const override {
        hash(data, size, 0, out);
        return true;
    }
    bool fingerprint(const PublicKey& key, Fingerprint& out) const override {
        out = key;
        return true;
    }
};

class Signer final : public RosterSigner {
public:
    explicit Signer(PublicKey key) : key_(key) {}
    const PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t* bytes, std::size_t length, Signature& out) const override {
        out = signature(key_, bytes, length);
        return true;
    }
private:
    PublicKey key_;
};

class Random final : public RandomSource {
public:
    bool fill(std::uint8_t* out, std::size_t length) override {
        ++next_;
        for (std::size_t index = 0; index < length; ++index) {
            out[index] = static_cast<std::uint8_t>(next_ + index);
        }
        return true;
    }
private:
    unsigned next_{0};
};

constexpr PublicKey id(std::uint8_t last) {
    PublicKey value{};
    value[0] = 5;
    value.back() = last;
    return value;
}

template <std::size_t Size>
std::array<std::uint8_t, Size> value(std::uint8_t marker) {
    std::array<std::uint8_t, Size> bytes{};
    bytes.fill(marker);
    return bytes;
}

const Crypto crypto{};
constexpr PublicKey admin = id(1), local = id(2), peer = id(3);
constexpr std::uint64_t group = 0xfedcba9800000001ULL;
constexpr auto domain = StorageDomain::outbound_counter_state;
constexpr auto receive_lane = GroupPairLane::data_rx;

Roster signed_roster(Roster roster) {
    CHECK(sign(roster, Signer(admin)));
    return roster;
}

Roster add(const Roster& before, const PublicKey& added) {
    Roster change{};
    CHECK(add_member(before, crypto, added, std::to_string(added.back()), change));
    return signed_roster(change);
}

Roster remove(const Roster& before, const PublicKey& removed) {
    Roster change{};
    CHECK(remove_member(before, crypto, removed, change));
    return signed_roster(change);
}

struct Owners {
    Storage index_kv, roster_kv, pair_kv;
    GroupPairIndex index;
    DurableGroupRoster roster;
    GroupPairRecords records;

    Owners(Disk& disk, GroupState& live)
        : index_kv(disk), roster_kv(disk), pair_kv(disk), index(index_kv, local),
          roster(roster_kv, index, crypto, admin, group, local),
          records(pair_kv, roster, live, peer, peer) {}
};

struct Fixture {
    GroupState first;
    std::unique_ptr<GroupState> second = std::make_unique<GroupState>();
    Disk disk;
    Owners owner{disk, first};
    Roster held{};
    Random random;
    std::uint8_t next_marker{8};

    explicit Fixture(PairHistoryMode mode = PairHistoryMode::volatile_only) : first(mode) {
        CHECK(first.initialize(admin, group, local));
        CHECK(second->initialize(admin, group, peer));
        CHECK(owner.index.initialize() == GroupPairIndexError::none);
        CHECK(owner.roster.initialize() == DurableRosterError::none);
        Roster start{};
        CHECK(create_first(group, admin, "Admin", LocationRule::off, start));
        offer(add(add(signed_roster(start), local), peer), Outcome::accepted, Outcome::accepted);
        accept();
    }

    void accept() {
        CHECK(first.accept_member(peer));
        CHECK(owner.roster.accept_member(peer) == DurableRosterError::none);
    }

    void offer(Roster roster, Outcome local_result, Outcome peer_result) {
        std::array<std::uint8_t, kMaximumRosterBytes> bytes{};
        const auto size = encode(roster, bytes.data(), bytes.size());
        CHECK(size);
        const auto result = owner.roster.offer(bytes.data(), size);
        CHECK(result.error == DurableRosterError::none && result.outcome == local_result);
        CHECK(first.offer(bytes.data(), size, crypto) == local_result);
        CHECK(second->offer(bytes.data(), size, crypto) == peer_result);
        held = roster;
    }

    VerifiedPairBinding bind(std::uint64_t now = 10) {
        const auto marker = next_marker;
        next_marker += 4;
        PairStart start{};
        PairReply reply{};
        PairConfirm confirm{};
        std::optional<VerifiedPairBinding> a, b;
        CHECK(second->pair_start(local, value<32>(marker), value<16>(marker + 1), now,
                                Signer(peer), random, start) == PairOutcome::ok);
        CHECK(first.pair_on_start(start, value<32>(marker + 2), value<16>(marker + 3), now + 1,
                                  crypto, Signer(local), random, reply) == PairOutcome::ok);
        CHECK(second->pair_on_reply(local, reply, now + 2, crypto, Signer(peer), confirm, b) == PairOutcome::ok);
        CHECK(first.pair_on_confirm(peer, confirm, now + 3, crypto, a) == PairOutcome::ok);
        CHECK(a && b);
        return *a;
    }

    void activate(GroupPairRecords& records, const VerifiedPairBinding& proof) {
        CHECK(first.pair_activation_possible(peer, proof.id()) == PairOutcome::ok);
        CHECK(records.mark_activation_possible() == Error::none);
        CHECK(first.pair_commit(peer, proof.id()) == PairOutcome::ok);
        CHECK(records.commit() == Error::none);
    }
};

void no_traffic(GroupPairRecords& records) {
    for (unsigned lane = 0; lane < 4; ++lane) {
        CHECK(records.storage(static_cast<GroupPairLane>(lane)) == nullptr);
    }
}

void enrollment_and_reopen() {
    Fixture f;
    GroupPairHistory facts{};
    CHECK(f.owner.records.history(facts) == Error::unavailable);
    CHECK(f.owner.records.provision() == Error::none);
    CHECK(f.owner.records.history(facts) == Error::none && facts.phase == GroupPairPhase::empty);
    const auto proof = f.bind();
    CHECK(f.owner.records.prepare(proof) == Error::none);
    CHECK(f.owner.records.history(facts) == Error::none && facts.phase == GroupPairPhase::prepared);
    CHECK(f.owner.records.commit() == Error::unavailable);
    no_traffic(f.owner.records);
    f.activate(f.owner.records, proof);
    CHECK(f.owner.records.history(facts) == Error::none && facts.phase == GroupPairPhase::committed &&
          facts.used_epoch == 1 && facts.committed_epoch == 1);

    auto* storage = f.owner.records.storage(GroupPairLane::data_tx);
    CHECK(storage && storage != f.owner.records.storage(GroupPairLane::ack_tx));
    OutboundCounterLeaseStore counter(*storage);
    const auto lease = counter.reserve({value<16>(7), 1, 4});
    CHECK(lease.reserved() && lease.first_counter == 1 && lease.last_counter == 4);
    CHECK(counter.reserve({value<16>(7), 1, 4}).first_counter == 5);
    const auto old_data = f.disk.at("ot_group_pair/p00l0");
    std::cout << "Payload bytes: pair record " << f.disk.at("ot_group_pair/p00").size()
              << ", each lane " << old_data.size() << "\n";

    auto* replay_storage = f.owner.records.storage(receive_lane);
    CHECK(replay_storage);
    using opentrail::security_eval::EvaluationReplayStore;
    using opentrail::security_eval::ReplayError;
    EvaluationReplayStore replay(*replay_storage);
    CHECK(replay.start(value<std::tuple_size_v<EvaluationReplayStore::Context>>(11), true) == ReplayError::none);
    CHECK(replay.accept_authenticated(8) == ReplayError::none);
    CHECK(replay.accept_authenticated(8) == ReplayError::replay);
    EvaluationReplayStore reloaded_replay(*replay_storage);
    CHECK(reloaded_replay.start(value<std::tuple_size_v<EvaluationReplayStore::Context>>(11), false) == ReplayError::none);
    CHECK(reloaded_replay.accept_authenticated(8) == ReplayError::replay);
    CHECK(reloaded_replay.accept_authenticated(9) == ReplayError::none);

    f.first.restart();
    f.second->restart();
    std::array<std::uint8_t, kPersistentSlotBytes> retired{};
    CHECK(!storage->read_slot(domain, 0, {retired.data(), retired.size()}).read());
    Owners reopened(f.disk, f.first);
    CHECK(reopened.roster.reopen() == DurableRosterError::none);
    CHECK(reopened.records.history(facts) == Error::none && facts.committed_epoch == 1);
    CHECK(reopened.records.prepare(proof) == Error::bad_epoch);
    no_traffic(reopened.records);

    // The fixture can retain GroupState's in-memory epoch floor across restart
    // for this test. Production boot must seed that floor from history.
    const auto fresh = f.bind(60010);
    CHECK(fresh.epoch() == 2 && reopened.records.prepare(fresh) == Error::none);
    f.activate(reopened.records, fresh);
    CHECK(reopened.records.storage(receive_lane));
    f.disk["ot_group_pair/p00l0"] = old_data;
    no_traffic(reopened.records);  // A required lane has the prior epoch.
}

void roster_change_and_reuse() {
    Fixture f;
    CHECK(f.owner.records.provision() == Error::none);
    const auto proof = f.bind();
    CHECK(f.owner.records.prepare(proof) == Error::none);
    f.activate(f.owner.records, proof);
    auto* old_storage = f.owner.records.storage(receive_lane);
    CHECK(old_storage);
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    CHECK(old_storage->read_slot(domain, 0, {bytes.data(), bytes.size()}).read());
    const auto old = f.disk.at("ot_group_pair/p00");
    const auto old_lane = f.disk.at("ot_group_pair/p00l2");

    constexpr auto unaffected = id(4);
    f.offer(add(f.held, unaffected), Outcome::accepted, Outcome::accepted);
    CHECK(old_storage->read_slot(domain, 0, {bytes.data(), bytes.size()}).read());
    f.offer(remove(f.held, peer), Outcome::accepted, Outcome::accepted_removed_self);
    CHECK(!old_storage->read_slot(domain, 0, {bytes.data(), bytes.size()}).read());
    f.second = std::make_unique<GroupState>();
    CHECK(f.second->initialize(admin, group, peer));
    f.offer(add(f.held, peer), Outcome::accepted, Outcome::accepted);
    CHECK(!f.owner.roster.find_pair(peer));
    f.accept();
    GroupPairRecords replacement(f.owner.pair_kv, f.owner.roster, f.first, peer, peer);
    CHECK(replacement.provision() == Error::none);
    GroupPairHistory facts{};
    CHECK(replacement.history(facts) == Error::none && facts.used_epoch == 0);
    CHECK(replacement.prepare(proof) == Error::wrong_binding);
    const auto new_proof = f.bind(1000);
    CHECK(replacement.prepare(new_proof) == Error::none);
    f.activate(replacement, new_proof);
    CHECK(replacement.storage(receive_lane));

    f.disk["ot_group_pair/p00l2"] = old_lane;
    no_traffic(replacement);
    f.disk["ot_group_pair/p00"] = old;
    CHECK(replacement.history(facts) == Error::wrong_incarnation);
    CHECK(replacement.provision() == Error::unavailable);
}

void interruptions() {
    constexpr std::array faults{Fault::write, Fault::unapplied, Fault::applied, Fault::readback};
    // 1 provision, 2 prepare metadata, 3..6 prepare the four lanes,
    // 7 activation intent, 8 active commit.
    for (const auto fault : faults) {
        for (std::size_t cut = 1; cut <= 8; ++cut) {
            Fixture f;
            f.owner.pair_kv.fault = fault;
            f.owner.pair_kv.cut = cut;
            auto result = f.owner.records.provision();
            if (result == Error::none) {
                const auto proof = f.bind();
                result = f.owner.records.prepare(proof);
                if (result == Error::none) {
                    CHECK(f.first.pair_activation_possible(peer, proof.id()) == PairOutcome::ok);
                    result = f.owner.records.mark_activation_possible();
                }
                if (result == Error::none) {
                    CHECK(f.first.pair_commit(peer, proof.id()) == PairOutcome::ok);
                    result = f.owner.records.commit();
                }
            }
            CHECK(result == Error::storage_failure && f.owner.pair_kv.writes == cut);
            no_traffic(f.owner.records);
            Owners reopened(f.disk, f.first);
            CHECK(reopened.roster.reopen() == DurableRosterError::none);
            no_traffic(reopened.records);
            CHECK(reopened.records.provision() == Error::unavailable);
            GroupPairHistory facts{};
            const auto loaded = reopened.records.history(facts);
            const bool applied = fault == Fault::applied || fault == Fault::readback;
            CHECK(loaded == (cut == 1 && !applied ? Error::missing : Error::none));
            if (loaded != Error::none) continue;
            if (cut == 1 || (cut == 2 && !applied)) {
                CHECK(facts.phase == GroupPairPhase::empty && facts.used_epoch == 0);
            } else if (cut < 7 || (cut == 7 && !applied)) {
                CHECK(facts.phase == GroupPairPhase::prepared && facts.used_epoch == 1);
            } else if (cut == 7 || (cut == 8 && !applied)) {
                CHECK(facts.phase == GroupPairPhase::activation_possible);
            } else {
                CHECK(facts.phase == GroupPairPhase::committed && facts.committed_epoch == 1);
            }
        }
    }
}

void required_records() {
    Fixture f;
    CHECK(f.owner.records.provision() == Error::none);
    const auto proof = f.bind();
    CHECK(f.owner.records.prepare(proof) == Error::none);
    const auto lane = f.disk.at("ot_group_pair/p00l2");
    f.disk.erase("ot_group_pair/p00l2");
    CHECK(f.first.pair_activation_possible(peer, proof.id()) == PairOutcome::ok);
    CHECK(f.owner.records.mark_activation_possible() == Error::missing);
    f.disk["ot_group_pair/p00l2"] = lane;
    CHECK(f.owner.records.mark_activation_possible() == Error::none);
    CHECK(f.first.pair_commit(peer, proof.id()) == PairOutcome::ok);
    f.disk["ot_group_pair/p00l2"].back() ^= 1;
    CHECK(f.owner.records.commit() == Error::malformed);
    f.disk["ot_group_pair/p00l2"] = lane;
    CHECK(f.owner.records.commit() == Error::none);
    auto* existing = f.owner.records.storage(GroupPairLane::data_tx);
    CHECK(existing);
    f.disk.erase("ot_group_pair/p00l2");
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    CHECK(!existing->read_slot(domain, 0, {bytes.data(), bytes.size()}).read());
    no_traffic(f.owner.records);
    f.disk.erase("ot_group_pair/p00");
    GroupPairHistory facts{};
    CHECK(f.owner.records.history(facts) == Error::missing);
    GroupPairRecords missing(f.owner.pair_kv, f.owner.roster, f.first, peer, peer);
    CHECK(missing.provision() == Error::unavailable);
    CHECK(missing.prepare(proof) == Error::missing);
}


void same_epoch_rollback() {
    unsigned rejected = 0;
    for (const auto lane : {GroupPairLane::data_tx, GroupPairLane::ack_tx}) {
        Fixture f;
        CHECK(f.owner.records.provision() == Error::none);
        const auto proof = f.bind();
        CHECK(f.owner.records.prepare(proof) == Error::none);
        f.activate(f.owner.records, proof);
        auto* storage = f.owner.records.storage(lane);
        CHECK(storage);
        OutboundCounterLeaseStore counter(*storage);
        const auto context = value<16>(9);
        CHECK(counter.reserve({context, 1, 4}).first_counter == 1);
        const auto key = "ot_group_pair/p00l" + std::to_string(static_cast<unsigned>(lane));
        const auto older = f.disk.at(key);
        CHECK(counter.reserve({context, 1, 4}).first_counter == 5);
        f.disk[key] = older;
        if (!counter.reserve({context, 1, 4}).reserved()) {
            no_traffic(f.owner.records);
            ++rejected;
        }
    }
    for (const auto lane : {GroupPairLane::data_rx, GroupPairLane::ack_rx}) {
        Fixture f;
        CHECK(f.owner.records.provision() == Error::none);
        const auto proof = f.bind();
        CHECK(f.owner.records.prepare(proof) == Error::none);
        f.activate(f.owner.records, proof);
        auto* storage = f.owner.records.storage(lane);
        CHECK(storage);
        using namespace opentrail::security_eval;
        const auto context = value<std::tuple_size_v<EvaluationReplayStore::Context>>(17);
        EvaluationReplayStore first(*storage);
        CHECK(first.start(context, true) == ReplayError::none);
        CHECK(first.accept_authenticated(4) == ReplayError::none);
        const auto key = "ot_group_pair/p00l" + std::to_string(static_cast<unsigned>(lane));
        const auto older = f.disk.at(key);
        CHECK(first.accept_authenticated(8) == ReplayError::none);
        f.disk[key] = older;
        EvaluationReplayStore replacement(*storage);
        if (replacement.start(context, false) != ReplayError::none) {
            no_traffic(f.owner.records);
            ++rejected;
        }
    }
    std::cout << "same-epoch rollback refusals: " << rejected << "/4\n";
    CHECK(rejected == 4);
}

void active_lane_failures() {
    for (const auto lane : {GroupPairLane::data_tx, GroupPairLane::ack_tx,
                           GroupPairLane::data_rx, GroupPairLane::ack_rx}) {
        for (const auto fault : {Fault::write, Fault::unapplied, Fault::applied, Fault::readback}) {
            Fixture f;
            CHECK(f.owner.records.provision() == Error::none);
            const auto proof = f.bind();
            CHECK(f.owner.records.prepare(proof) == Error::none);
            f.activate(f.owner.records, proof);
            auto* storage = f.owner.records.storage(lane);
            CHECK(storage);
            const auto lane_id = static_cast<unsigned>(lane);
            if (lane_id < 2) {
                OutboundCounterLeaseStore counter(*storage);
                CHECK(counter.reserve({value<16>(9), 1, 4}).reserved());
                f.owner.pair_kv.cut = f.owner.pair_kv.writes + 1;
                f.owner.pair_kv.fault = fault;
                CHECK(!counter.reserve({value<16>(9), 1, 4}).reserved());
            } else {
                using namespace opentrail::security_eval;
                EvaluationReplayStore replay(*storage);
                CHECK(replay.start(value<std::tuple_size_v<EvaluationReplayStore::Context>>(17), true) ==
                      ReplayError::none);
                CHECK(replay.accept_authenticated(4) == ReplayError::none);
                f.owner.pair_kv.cut = f.owner.pair_kv.writes + 1;
                f.owner.pair_kv.fault = fault;
                CHECK(replay.accept_authenticated(8) != ReplayError::none);
            }
            CHECK(f.owner.pair_kv.writes == f.owner.pair_kv.cut);
            no_traffic(f.owner.records);
            std::array<std::uint8_t, kPersistentSlotBytes> record{};
            CHECK(!storage->read_slot(domain, 0, {record.data(), record.size()}).read());
        }
    }
}




void initialize_live(GroupState& group_state) {
    CHECK(group_state.initialize(admin, group, local));
}

void lost_confirm_catchup() {
    Fixture f(PairHistoryMode::durable);
    CHECK(f.owner.records.provision() == Error::none);
    CHECK(f.owner.roster.hydrate_group(f.first) == DurableRosterError::none);
    CHECK(f.owner.records.hydrate(crypto) == Error::none);
    PairStart start{};
    PairReply reply{};
    PairConfirm lost{};
    std::optional<VerifiedPairBinding> sender, receiver;
    CHECK(f.second->pair_start(local, value<32>(31), value<16>(32), 10,
                                Signer(peer), f.random, start) == PairOutcome::ok);
    CHECK(f.first.pair_on_start(start, value<32>(33), value<16>(34), 11,
                               crypto, Signer(local), f.random, reply) == PairOutcome::wrong_state);
    CHECK(f.owner.records.answer(start, value<32>(33), value<16>(34), 11,
                                  crypto, Signer(local), f.random, reply) == Error::none);
    CHECK(f.second->pair_on_reply(local, reply, 12, crypto, Signer(peer), lost, sender) == PairOutcome::ok);
    CHECK(sender && !receiver);  // The confirm never reaches the responder.
    GroupPairHistory facts{};
    CHECK(f.owner.records.history(facts) == Error::none && facts.history && facts.used_epoch == 0);

    // These are genuinely new objects, retaining only the durable bytes.
    GroupState fresh(PairHistoryMode::durable);
    initialize_live(fresh);
    Owners reopened(f.disk, fresh);
    CHECK(reopened.roster.reopen() == DurableRosterError::none);
    PairStart blocked{};
    CHECK(fresh.pair_start(peer, value<32>(41), value<16>(42), 60001,
                            Signer(local), f.random, blocked) == PairOutcome::roster_not_active);
    CHECK(reopened.roster.hydrate_group(fresh) == DurableRosterError::none);
    CHECK(fresh.roster().accepted(peer));  // Restored local consent, without a new tap.
    CHECK(fresh.pair_start(peer, value<32>(41), value<16>(42), 60001,
                            Signer(local), f.random, blocked) == PairOutcome::wrong_state);
    CHECK(reopened.records.hydrate(crypto) == Error::none);
    CHECK(fresh.pair_status(peer)->history && fresh.pair_status(peer)->used_epoch == 0);
    f.second->restart();
    CHECK(f.second->pair_start(local, value<32>(41), value<16>(42), 60010,
                                Signer(peer), f.random, start) == PairOutcome::ok);
    CHECK(start.epoch == 2);
    reply.signature = value<kSignatureBytes>(77);
    bool observed_commit = false;
    reopened.pair_kv.before_commit = [&] {
        CHECK(reply.signature == value<kSignatureBytes>(77));
        CHECK(fresh.pair_start(peer, value<32>(51), value<16>(52), 60011,
                                Signer(local), f.random, blocked) == PairOutcome::busy);
        observed_commit = true;
    };
    CHECK(reopened.records.answer(start, value<32>(43), value<16>(44), 60011,
                                   crypto, Signer(local), f.random, reply) == Error::none);
    CHECK(observed_commit);
    reopened.pair_kv.before_commit = {};
    CHECK(reopened.records.history(facts) == Error::none && facts.used_epoch == 1);
    PairConfirm confirm{};
    CHECK(f.second->pair_on_reply(local, reply, 60012, crypto, Signer(peer), confirm, sender) == PairOutcome::ok);
    CHECK(fresh.pair_on_confirm(peer, confirm, 60013, crypto, receiver) == PairOutcome::wrong_state);
    CHECK(reopened.records.finish_confirm(confirm, 60013, crypto, receiver) == Error::none);
    CHECK(receiver && receiver->epoch() == 2);
    CHECK(fresh.pair_activation_possible(peer, receiver->id()) == PairOutcome::ok);
    CHECK(reopened.records.mark_activation_possible() == Error::none);
    CHECK(fresh.pair_commit(peer, receiver->id()) == PairOutcome::ok);
    CHECK(reopened.records.commit() == Error::none);
    CHECK(reopened.records.history(facts) == Error::none && facts.committed_epoch == 2);
    auto* previous_lane = reopened.records.storage(GroupPairLane::data_tx);
    CHECK(previous_lane);
    CHECK(reopened.records.hydrate(crypto) == Error::unavailable);

    // Let epoch 3's confirm disappear too. A signed epoch-4 start must
    // durably catch up the responder without reviving epoch-2 lane access.
    f.second->restart();
    CHECK(f.second->pair_start(local, value<32>(91), value<16>(92), 120020,
                                Signer(peer), f.random, start) == PairOutcome::ok && start.epoch == 3);
    CHECK(reopened.records.answer(start, value<32>(93), value<16>(94), 120021,
                                   crypto, Signer(local), f.random, reply) == Error::none);
    CHECK(f.second->pair_on_reply(local, reply, 120022, crypto, Signer(peer), confirm, sender) == PairOutcome::ok);
    f.second->restart();
    CHECK(f.second->pair_start(local, value<32>(95), value<16>(96), 180030,
                                Signer(peer), f.random, start) == PairOutcome::ok && start.epoch == 4);
    CHECK(reopened.records.answer(start, value<32>(97), value<16>(98), 180031,
                                   crypto, Signer(local), f.random, reply) == Error::none);
    CHECK(reopened.records.history(facts) == Error::none && facts.used_epoch == 3 &&
          facts.committed_epoch == 2 && facts.phase == GroupPairPhase::committed);
    std::array<std::uint8_t, kPersistentSlotBytes> image{};
    CHECK(!previous_lane->read_slot(domain, 0, {image.data(), image.size()}).read());
    CHECK(reopened.records.hydrate(crypto) == Error::unavailable);
    no_traffic(reopened.records);
}

void catchup_write_failures() {
    for (const auto fault : {Fault::write, Fault::unapplied, Fault::applied, Fault::readback}) {
        Fixture f(PairHistoryMode::durable);
        CHECK(f.owner.records.provision() == Error::none);
        CHECK(f.owner.roster.hydrate_group(f.first) == DurableRosterError::none);
        CHECK(f.owner.records.hydrate(crypto) == Error::none);
        PairStart start{};
        PairReply reply{};
        reply.signature = value<kSignatureBytes>(77);
        CHECK(f.second->pair_start(local, value<32>(51), value<16>(52), 10,
                                    Signer(peer), f.random, start) == PairOutcome::ok);
        f.owner.pair_kv.cut = f.owner.pair_kv.writes + 1;
        f.owner.pair_kv.fault = fault;
        CHECK(f.owner.records.answer(start, value<32>(53), value<16>(54), 11,
                                      crypto, Signer(local), f.random, reply) == Error::storage_failure);
        CHECK(reply.signature == value<kSignatureBytes>(77));
        CHECK(f.owner.pair_kv.writes == f.owner.pair_kv.cut);
        CHECK(f.first.pair_start(peer, value<32>(55), value<16>(56), 12,
                                 Signer(local), f.random, start) == PairOutcome::wrong_state);
        CHECK(f.first.pair_on_start(start, value<32>(57), value<16>(58), 13,
                                    crypto, Signer(local), f.random, reply) == PairOutcome::wrong_state);
        no_traffic(f.owner.records);
    }
}

void put32(Blob& blob, std::size_t offset, std::uint32_t number) {
    for (unsigned index = 0; index < 4; ++index) {
        blob[offset + index] = static_cast<std::uint8_t>(number >> (8 * (3 - index)));
    }
}

void fix_crc(Blob& blob) {
    put32(blob, blob.size() - 4, configuration_crc32({blob.data(), blob.size() - 4}));
}

constexpr std::size_t phase_offset = 4 + 1 + 8 + 32 + 8 + 32 + 32 + 4 + 4;
constexpr std::size_t used_offset = phase_offset + 1;
constexpr std::size_t committed_offset = used_offset + 4;
constexpr std::size_t evidence_offset = committed_offset + 4;
constexpr std::size_t common_epoch_offset = 1 + 32 + 8 + 4 + 32;

void sign_stored_binding(Blob& blob) {
    auto* evidence = blob.data() + evidence_offset;
    const auto responder = signature(local, evidence, kBindingBytes);
    const auto initiator = signature(peer, evidence, kBindingBytes);
    auto next = std::copy(responder.begin(), responder.end(), evidence + kBindingBytes);
    std::copy(initiator.begin(), initiator.end(), next);
    fix_crc(blob);
}


void fresh_initiator_persists_before_confirm() {
    Fixture f;
    CHECK(f.owner.records.provision() == Error::none);
    const auto old = f.bind();
    CHECK(f.owner.records.prepare(old) == Error::none);
    f.activate(f.owner.records, old);
    GroupState fresh(PairHistoryMode::durable);
    initialize_live(fresh);
    Owners reopened(f.disk, fresh);
    CHECK(reopened.roster.reopen() == DurableRosterError::none);
    CHECK(reopened.roster.hydrate_group(fresh) == DurableRosterError::none);
    CHECK(reopened.records.hydrate(crypto) == Error::none);
    f.second->restart();
    PairStart start{};
    PairReply reply{};
    PairConfirm confirm{};
    confirm.signature = value<kSignatureBytes>(71);
    std::optional<VerifiedPairBinding> proof, peer_proof;
    CHECK(fresh.pair_start(peer, value<32>(61), value<16>(62), 60010,
                            Signer(local), f.random, start) == PairOutcome::ok && start.epoch == 2);
    CHECK(f.second->pair_on_start(start, value<32>(63), value<16>(64), 60011,
                                   crypto, Signer(peer), f.random, reply) == PairOutcome::ok);
    CHECK(fresh.pair_on_reply(peer, reply, 60012, crypto, Signer(local), confirm, proof) ==
          PairOutcome::wrong_state);
    bool held = false;
    reopened.pair_kv.before_commit = [&] {
        CHECK(confirm.signature == value<kSignatureBytes>(71));
        held = true;
    };
    CHECK(reopened.records.finish_reply(reply, 60012, crypto, Signer(local), confirm, proof) == Error::none);
    CHECK(held && proof && proof->epoch() == 2);
    CHECK(f.second->pair_on_confirm(local, confirm, 60013, crypto, peer_proof) == PairOutcome::ok);
    GroupPairHistory facts{};
    CHECK(reopened.records.history(facts) == Error::none && facts.phase == GroupPairPhase::prepared &&
          facts.used_epoch == 2);
}

void malformed_pair_records() {
    Fixture f;
    CHECK(f.owner.records.provision() == Error::none);
    const auto proof = f.bind();
    CHECK(f.owner.records.prepare(proof) == Error::none);
    const auto original = f.disk.at("ot_group_pair/p00");
    for (unsigned variant = 0; variant < 10; ++variant) {
        auto malformed = original;
        switch (variant) {
            case 0: malformed.pop_back(); break;
            case 1: malformed.push_back(0); break;
            case 2: malformed[phase_offset] = 9; fix_crc(malformed); break;
            case 3: put32(malformed, used_offset, 2); fix_crc(malformed); break;
            case 4: put32(malformed, committed_offset, 1); fix_crc(malformed); break;
            case 5: malformed[evidence_offset] ^= 1; fix_crc(malformed); break;
            case 6:
                put32(malformed, evidence_offset + kBindingDomain.size() + common_epoch_offset, 0);
                fix_crc(malformed);
                break;
            case 7:
                malformed[evidence_offset + kBindingDomain.size()] = 0;
                fix_crc(malformed);
                break;
            case 8: malformed[3] = 1; fix_crc(malformed); break;
            case 9: malformed[evidence_offset + kBindingBytes] ^= 1; fix_crc(malformed); break;
        }
        f.disk["ot_group_pair/p00"] = malformed;
        GroupState fresh(PairHistoryMode::durable);
        initialize_live(fresh);
        Owners reopened(f.disk, fresh);
        CHECK(reopened.roster.reopen() == DurableRosterError::none);
        CHECK(reopened.roster.hydrate_group(fresh) == DurableRosterError::none);
        CHECK(reopened.records.hydrate(crypto) != Error::none);
        PairStart start{};
        CHECK(fresh.pair_start(peer, value<32>(71), value<16>(72), 1,
                                Signer(local), f.random, start) == PairOutcome::wrong_state);
        no_traffic(reopened.records);
    }
    f.disk["ot_group_pair/p00"] = original;
}

void maximum_epoch_hydration() {
    Fixture f;
    CHECK(f.owner.records.provision() == Error::none);
    const auto proof = f.bind();
    CHECK(f.owner.records.prepare(proof) == Error::none);
    const auto original = f.disk.at("ot_group_pair/p00");
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    for (const auto floor : {maximum - 1, maximum}) {
        auto final = original;
        final[phase_offset] = static_cast<std::uint8_t>(GroupPairPhase::committed);
        put32(final, used_offset, floor);
        put32(final, committed_offset, floor);
        put32(final, evidence_offset + kBindingDomain.size() + common_epoch_offset, floor);
        sign_stored_binding(final);  // Valid TEST-ONLY signatures over the synthetic final epoch.
        f.disk["ot_group_pair/p00"] = final;
        GroupState fresh(PairHistoryMode::durable);
        initialize_live(fresh);
        Owners reopened(f.disk, fresh);
        CHECK(reopened.roster.reopen() == DurableRosterError::none);
        CHECK(reopened.roster.hydrate_group(fresh) == DurableRosterError::none);
        CHECK(reopened.records.hydrate(crypto) == Error::none);
        PairStart start{};
        const auto result = fresh.pair_start(peer, value<32>(81), value<16>(82), 1,
                                             Signer(local), f.random, start);
        if (floor == maximum) {
            CHECK(result == PairOutcome::epoch_exhausted);
            CHECK(fresh.pair_status(peer)->next_epoch == 0);
        } else {
            CHECK(result == PairOutcome::ok && start.epoch == maximum && start.purpose == PairPurpose::rekey);
        }
        no_traffic(reopened.records);
    }
}

void inactive_roster_hydration() {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        Fixture f;
        CHECK(f.owner.records.provision() == Error::none);
        if (scenario == 1) {
            auto fork = f.held;
            fork.location_rule = LocationRule::required;
            fork = signed_roster(fork);
            std::array<std::uint8_t, kMaximumRosterBytes> bytes{};
            const auto count = encode(fork, bytes.data(), bytes.size());
            CHECK(f.owner.roster.offer(bytes.data(), count).outcome == Outcome::conflict);
        } else if (scenario == 0) {
            CHECK(f.owner.roster.stop_trusting() == DurableRosterError::none);
        } else {
            f.offer(remove(f.held, local), Outcome::accepted_removed_self, Outcome::accepted);
        }
        GroupState fresh(PairHistoryMode::durable);
        initialize_live(fresh);
        Owners reopened(f.disk, fresh);
        CHECK(reopened.roster.reopen() == DurableRosterError::none);
        CHECK(reopened.roster.hydrate_group(fresh) == DurableRosterError::refused);
        CHECK(reopened.records.hydrate(crypto) == Error::unavailable);
        no_traffic(reopened.records);
    }
}


void malformed_signed_start_history() {
    Fixture f(PairHistoryMode::durable);
    CHECK(f.owner.records.provision() == Error::none);
    CHECK(f.owner.roster.hydrate_group(f.first) == DurableRosterError::none);
    CHECK(f.owner.records.hydrate(crypto) == Error::none);
    PairStart start{};
    PairReply reply{};
    CHECK(f.second->pair_start(local, value<32>(101), value<16>(102), 10,
                                Signer(peer), f.random, start) == PairOutcome::ok);
    CHECK(f.owner.records.answer(start, value<32>(103), value<16>(104), 11,
                                  crypto, Signer(local), f.random, reply) == Error::none);
    const auto original = f.disk.at("ot_group_pair/p00");
    for (unsigned variant = 0; variant < 3; ++variant) {
        auto altered = original;
        if (variant == 0) put32(altered, used_offset, 1);
        if (variant == 1) altered[evidence_offset + kStartBytes] ^= 1;
        if (variant == 2) put32(altered, evidence_offset + kStartDomain.size() + common_epoch_offset, 0);
        fix_crc(altered);
        f.disk["ot_group_pair/p00"] = altered;
        GroupState fresh(PairHistoryMode::durable);
        initialize_live(fresh);
        Owners reopened(f.disk, fresh);
        CHECK(reopened.roster.reopen() == DurableRosterError::none);
        CHECK(reopened.roster.hydrate_group(fresh) == DurableRosterError::none);
        CHECK(reopened.records.hydrate(crypto) != Error::none);
        CHECK(fresh.pair_start(peer, value<32>(105), value<16>(106), 12,
                                Signer(local), f.random, start) == PairOutcome::wrong_state);
    }
}

}  // namespace

int main() {
    static_assert(!std::is_default_constructible_v<VerifiedPairStart>);
    lost_confirm_catchup();
    catchup_write_failures();
    fresh_initiator_persists_before_confirm();
    malformed_pair_records();
    maximum_epoch_hydration();
    malformed_signed_start_history();
    inactive_roster_hydration();
    enrollment_and_reopen();
    roster_change_and_reuse();
    interruptions();
    required_records();
    same_epoch_rollback();
    active_lane_failures();
    std::cout << "PASS 13 group pair records groups incl 52 pair-write interruptions\n";
}
