#include "opentrail/group_pair_records.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "opentrail/evaluation_replay_store.hpp"
#include "opentrail/outbound_counter_lease_store.hpp"

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
        const auto outcome = writes == cut ? fault : Fault::none;
        if (outcome == Fault::unapplied) return KvError::io_failure;
        disk_[pending_key_] = std::move(pending_);
        pending_.clear();
        failed_readback_ = outcome == Fault::readback;
        return outcome == Fault::applied ? KvError::io_failure : KvError::none;
    }

    std::size_t writes{0}, cut{0};
    Fault fault{Fault::none};

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

    Fixture() {
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

}  // namespace

int main() {
    enrollment_and_reopen();
    roster_change_and_reuse();
    interruptions();
    required_records();
    std::cout << "PASS 4 group pair records groups incl 32 pair-write interruptions\n";
}
