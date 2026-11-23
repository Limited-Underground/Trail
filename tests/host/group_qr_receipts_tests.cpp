#include "opentrail/group_qr_receipts.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
namespace p = opentrail::persistence;
namespace r = opentrail::identity::roster;
namespace qr = r::qr;
using Error = p::GroupQrReceiptError;
using KvError = p::PersistentKvBackendError;
using Kind = qr::Kind;

void check(bool value, const char* expression, int line) {
    if (value) return;
    std::cerr << "FAIL " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

class MemoryKv final : public p::PersistentKvBackend {
public:
    enum class Cut { none, before_commit, after_commit };
    Cut cut{Cut::none};
    unsigned commits{};
    std::map<std::string, std::vector<std::uint8_t>> records;

    KvError read_blob(const char* partition, const char* ns, const char* key, std::uint8_t* out,
                      std::size_t capacity, std::size_t& size) override {
        CHECK(std::string(partition) == "ot_state" && std::string(ns) == "ot_group_qrr");
        const auto found = records.find(key);
        if (found == records.end()) return KvError::not_found;
        size = found->second.size();
        if (size > capacity) return KvError::invalid_argument;
        std::copy(found->second.begin(), found->second.end(), out);
        return KvError::none;
    }
    KvError write_blob(const char*, const char*, const char* key, const std::uint8_t* bytes,
                       std::size_t size) override {
        staged_key_ = key;
        staged_.assign(bytes, bytes + size);
        return KvError::none;
    }
    KvError erase_key(const char*, const char*, const char*) override { return KvError::invalid_argument; }
    KvError commit(const char*, const char*) override {
        ++commits;
        const auto fault = cut;
        cut = Cut::none;
        if (fault == Cut::before_commit) { staged_.clear(); return KvError::io_failure; }
        records[staged_key_] = staged_;
        staged_.clear();
        return fault == Cut::after_commit ? KvError::io_failure : KvError::none;
    }

private:
    std::string staged_key_;
    std::vector<std::uint8_t> staged_;
};

r::PublicKey key(std::uint8_t b) { r::PublicKey k{}; k.fill(b); return k; }
const auto kAdmin = key(0xa1);
const auto kJoiner = key(0x1a);

qr::Retained retained(std::uint8_t invite, Kind kind, qr::Side side = qr::Side::admin) {
    qr::Retained value{};
    value.decision.scope.fill(0x51);
    value.decision.transaction.fill(static_cast<std::uint8_t>(0x60 + invite));
    value.decision.manifest.fill(0x71);
    value.decision.side = side;
    value.decision.kind = kind;
    value.state.joiner = kJoiner;
    value.state.admin = kAdmin;
    value.state.roster_digest.fill(0x33);
    value.state.transcript.fill(0x44);
    value.state.invite_id.fill(invite);
    value.state.group = 77;
    value.state.roster_version = 3;
    value.state.epoch = 1;
    value.state.rule = r::LocationRule::preferred;
    value.local = side == qr::Side::admin ? kAdmin : kJoiner;
    value.peer = side == qr::Side::admin ? kJoiner : kAdmin;
    return value;
}

qr::Nonce invite(std::uint8_t b) { qr::Nonce n{}; n.fill(b); return n; }
constexpr std::uint64_t kDeadline = 1'000;

void first_decision_consumes_the_invite_and_survives_reopen() {
    MemoryKv kv;
    p::GroupQrReceipts store(kv, kAdmin);
    CHECK(!store.fresh_unclaimed(invite(1)));  // unloaded fails closed
    CHECK(store.reopen() == Error::missing);
    CHECK(store.fresh_unclaimed(invite(1)));
    const auto prepared = retained(1, Kind::prepared);
    CHECK(!store.readback(prepared));
    CHECK(store.persist(prepared, kDeadline, 10) == Error::none);
    CHECK(!store.fresh_unclaimed(invite(1)) && store.fresh_unclaimed(invite(2)));
    CHECK(store.readback(prepared) && !store.readback(retained(1, Kind::committed)));
    CHECK(kv.records.at("receipts").size() == p::GroupQrReceipts::kRecordBytes && kv.commits == 1);

    p::GroupQrReceipts reopened(kv, kAdmin);
    CHECK(reopened.reopen() == Error::none);
    std::uint64_t deadline = 0;
    const auto found = reopened.find(invite(1), &deadline);
    CHECK(found && deadline == kDeadline && reopened.readback(prepared) && !reopened.fresh_unclaimed(invite(1)));
}

void decisions_only_advance_the_same_exact_transaction() {
    MemoryKv kv;
    p::GroupQrReceipts store(kv, kAdmin);
    CHECK(store.reopen() == Error::missing);
    CHECK(store.persist(retained(1, Kind::committed), kDeadline, 10) == Error::refused);  // commit needs prepare
    CHECK(store.persist(retained(1, Kind::prepared), kDeadline, 10) == Error::none);
    CHECK(store.persist(retained(1, Kind::prepared), kDeadline, 20) == Error::none && kv.commits == 1);  // no-op

    // Same invite ID, anything else different: refused, invite stays consumed.
    auto other = retained(1, Kind::prepared);
    other.state.transcript.fill(0x45);
    CHECK(store.persist(other, kDeadline, 10) == Error::refused);
    other = retained(1, Kind::prepared);
    other.decision.scope.fill(0x52);
    CHECK(store.persist(other, kDeadline, 10) == Error::refused);
    other = retained(1, Kind::prepared);
    other.state.joiner = key(0x1b);
    other.peer = key(0x1b);
    CHECK(store.persist(other, kDeadline, 10) == Error::refused);
    CHECK(store.persist(retained(1, Kind::prepared), kDeadline + 1, 10) == Error::refused);  // original deadline

    CHECK(store.persist(retained(1, Kind::committed), kDeadline, 30) == Error::none && kv.commits == 2);
    CHECK(store.readback(retained(1, Kind::committed)));
    CHECK(store.persist(retained(1, Kind::committed), kDeadline, 40) == Error::none && kv.commits == 2);
    CHECK(store.persist(retained(1, Kind::aborted), kDeadline, 40) == Error::refused);
    CHECK(store.persist(retained(1, Kind::prepared), kDeadline, 40) == Error::refused);

    CHECK(store.persist(retained(2, Kind::prepared), kDeadline, 10) == Error::none);
    CHECK(store.persist(retained(2, Kind::aborted), kDeadline, 50) == Error::none);
    CHECK(store.persist(retained(2, Kind::committed), kDeadline, 50) == Error::refused);
}

void positive_markers_are_refused_at_the_deadline() {
    MemoryKv kv;
    p::GroupQrReceipts store(kv, kAdmin);
    CHECK(store.reopen() == Error::missing);
    CHECK(store.persist(retained(1, Kind::prepared), kDeadline, kDeadline) == Error::refused);
    CHECK(store.fresh_unclaimed(invite(1)) && kv.commits == 0);
    CHECK(store.persist(retained(1, Kind::prepared), kDeadline, kDeadline - 1) == Error::none);
    CHECK(store.persist(retained(1, Kind::committed), kDeadline, kDeadline + 5) == Error::refused);
    CHECK(store.persist(retained(1, Kind::aborted), kDeadline, kDeadline + 5) == Error::none);
    CHECK(store.persist(retained(3, Kind::aborted), kDeadline, kDeadline + 5) == Error::none);  // abort-only record
}

void full_record_evicts_only_finished_decisions() {
    MemoryKv kv;
    p::GroupQrReceipts store(kv, kAdmin);
    CHECK(store.reopen() == Error::missing);
    for (std::uint8_t i = 1; i <= 4; ++i) CHECK(store.persist(retained(i, Kind::prepared), kDeadline, 10) == Error::none);
    CHECK(store.persist(retained(5, Kind::prepared), kDeadline, 10) == Error::refused);  // all in flight
    CHECK(store.persist(retained(3, Kind::committed), kDeadline, 10) == Error::none);
    CHECK(store.persist(retained(2, Kind::aborted), kDeadline, 10) == Error::none);
    CHECK(store.persist(retained(5, Kind::prepared), kDeadline, 10) == Error::none);
    // The oldest finished decision (invite 2, written before 3) gave way.
    CHECK(store.fresh_unclaimed(invite(2)) && !store.fresh_unclaimed(invite(3)));
    CHECK(store.readback(retained(1, Kind::prepared)) && store.readback(retained(4, Kind::prepared)));
    p::GroupQrReceipts reopened(kv, kAdmin);
    CHECK(reopened.reopen() == Error::none && reopened.readback(retained(5, Kind::prepared)) &&
          reopened.readback(retained(3, Kind::committed)) && reopened.fresh_unclaimed(invite(2)));
}

void interrupted_writes_need_reopen_and_keep_prior_records() {
    for (const auto cut : {MemoryKv::Cut::before_commit, MemoryKv::Cut::after_commit}) {
        MemoryKv kv;
        p::GroupQrReceipts store(kv, kAdmin);
        CHECK(store.reopen() == Error::missing);
        CHECK(store.persist(retained(1, Kind::prepared), kDeadline, 10) == Error::none);
        kv.cut = cut;
        CHECK(store.persist(retained(2, Kind::prepared), kDeadline, 10) == Error::storage_failure);
        // Uncertain: nothing answers until reopen.
        CHECK(!store.fresh_unclaimed(invite(3)) && !store.readback(retained(1, Kind::prepared)));
        CHECK(store.persist(retained(3, Kind::prepared), kDeadline, 10) == Error::storage_failure);
        CHECK(store.reopen() == Error::none);
        CHECK(store.readback(retained(1, Kind::prepared)));
        if (cut == MemoryKv::Cut::before_commit) {
            CHECK(store.fresh_unclaimed(invite(2)));
            CHECK(store.persist(retained(2, Kind::prepared), kDeadline, 10) == Error::none);
        } else {
            CHECK(!store.fresh_unclaimed(invite(2)) && store.readback(retained(2, Kind::prepared)));
        }
    }
}

void malformed_decisions_and_foreign_records_are_refused() {
    MemoryKv kv;
    p::GroupQrReceipts store(kv, kAdmin);
    CHECK(store.reopen() == Error::missing);
    CHECK(store.persist(retained(1, Kind::prepared, qr::Side::joiner), kDeadline, 10) == Error::invalid);  // not local
    auto bad = retained(1, Kind::prepared);
    bad.peer = kAdmin;
    CHECK(store.persist(bad, kDeadline, 10) == Error::invalid);
    bad = retained(1, Kind::prepared);
    bad.state.invite_id = {};
    CHECK(store.persist(bad, kDeadline, 10) == Error::invalid);
    CHECK(store.persist(retained(1, Kind::confirmed), kDeadline, 10) == Error::invalid);
    CHECK(store.persist(retained(1, Kind::prepared), 0, 0) == Error::invalid);
    CHECK(kv.commits == 0);

    CHECK(store.persist(retained(1, Kind::prepared), kDeadline, 10) == Error::none);
    p::GroupQrReceipts joiner_view(kv, kJoiner);
    CHECK(joiner_view.reopen() == Error::invalid_record);
    CHECK(!joiner_view.fresh_unclaimed(invite(9)));
    kv.records.at("receipts")[100] ^= 1;
    p::GroupQrReceipts corrupt(kv, kAdmin);
    CHECK(corrupt.reopen() == Error::invalid_record && !corrupt.fresh_unclaimed(invite(9)));

    MemoryKv joiner_kv;
    p::GroupQrReceipts joiner(joiner_kv, kJoiner);
    CHECK(joiner.reopen() == Error::missing);
    CHECK(joiner.persist(retained(1, Kind::prepared, qr::Side::joiner), kDeadline, 10) == Error::none);
}
}  // namespace

int main() {
    first_decision_consumes_the_invite_and_survives_reopen();
    decisions_only_advance_the_same_exact_transaction();
    positive_markers_are_refused_at_the_deadline();
    full_record_evicts_only_finished_decisions();
    interrupted_writes_need_reopen_and_keep_prior_records();
    malformed_decisions_and_foreign_records_are_refused();
    std::cout << "PASS: 6 durable QR receipt groups\n";
}
