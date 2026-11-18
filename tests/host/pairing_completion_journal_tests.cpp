#include "opentrail/pairing_completion_journal.hpp"
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "fake_secure_random.hpp"
#include <functional>
#include <optional>

using namespace invitation_lifecycle_test;
namespace {
using Journal = PairingCompletionJournal;
using Phase = PairingCompletionPhase;
using Kind = PairingCompletionKind;
using Receipt = PairingCompletionReceipt;

struct Identity {
    Storage media;
    security::test_support::FakeSecureRandomSource random;
    EnrollmentIdentityStore owner{media, random};
    InvitationKey key{};
    explicit Identity(unsigned seed) {
        std::array<std::uint8_t, 64> entropy{};
        entropy.fill(static_cast<std::uint8_t>(seed));
        CHECK(random.load_bytes(entropy.data(), entropy.size()));
        random.set_state(security::EntropyState::ready);
        CHECK(owner.initialize() && owner.public_key(key));
    }
};
struct Pair {
    Identity a{1}, b{81};
    Storage media_a, media_b;
    std::optional<Journal> journal_a, journal_b;
    explicit Pair(bool begun = true) {
        journal_a.emplace(media_a); journal_b.emplace(media_b);
        CHECK(journal_a->initialize() && journal_b->initialize());
        PairingCompletionRecord record{};
        record.transaction.fill(33); record.transcript.fill(44);
        record.group = 17; record.epoch = 1; record.generation = 1;
        record.role = InvitationRole::initiator; record.peer = b.key; record.decision.fill(51);
        if (begun) CHECK(journal_a->begin(record));
        record.role = InvitationRole::responder; record.peer = a.key; record.decision.fill(71);
        if (begun) CHECK(journal_b->begin(record));
    }
    void prepare() { CHECK(journal_a->prepare() && journal_b->prepare()); }
    Receipt from_a(Kind kind) { Receipt out{}; CHECK(journal_a->export_receipt(kind, a.owner, out)); return out; }
    Receipt from_b(Kind kind) { Receipt out{}; CHECK(journal_b->export_receipt(kind, b.owner, out)); return out; }
    void exchange_votes() {
        prepare();
        CHECK(journal_a->accept_peer(from_b(Kind::prepared)));
        CHECK(journal_b->accept_peer(from_a(Kind::prepared)));
    }
    void commit() {
        exchange_votes();
        CHECK(journal_a->mark_completing() && journal_b->mark_completing());
        CHECK(journal_a->mark_committed() && journal_b->mark_committed());
    }
    void restart_a() {
        journal_a.reset(); media_a.clear();
        journal_a.emplace(media_a); CHECK(journal_a->initialize());
    }
};
unsigned mutations(const Storage& storage) {
    unsigned count = 0;
    for (const auto op : storage.trace) count += op != 'r';
    return count;
}
Receipt signed_receipt(Identity& identity, Receipt receipt) {
    const auto bytes = pairing_receipt_signing_bytes(receipt);
    std::array<std::uint8_t, 64> signature{};
    CHECK(identity.owner.sign(bytes.data(), bytes.size(), signature));
    std::memcpy(receipt.data() + 36, signature.data(), signature.size());
    return receipt;
}

enum class Transition { begin, prepare, peer_vote, completing, committed, peer_commit, peer_confirmed, aborted, replacement };
struct Attempt {
    Transition transition;
    Pair pair;
    PairingCompletionRecord next{};
    Receipt receipt{};
    explicit Attempt(Transition action) : transition(action), pair(action != Transition::begin) {
        next.transaction.fill(33); next.transcript.fill(44); next.decision.fill(51);
        next.group = 17; next.generation = 1; next.epoch = 1;
        next.role = InvitationRole::initiator; next.peer = pair.b.key;
        switch (transition) {
            case Transition::begin:
            case Transition::prepare:
            case Transition::aborted: break;
            case Transition::peer_vote:
                pair.prepare();
                receipt = pair.from_b(Kind::prepared);
                break;
            case Transition::completing:
                pair.exchange_votes();
                break;
            case Transition::committed:
                pair.exchange_votes();
                CHECK(pair.journal_a->mark_completing());
                break;
            case Transition::peer_commit:
                pair.commit();
                receipt = pair.from_b(Kind::committed);
                break;
            case Transition::peer_confirmed:
                pair.commit();
                CHECK(pair.journal_a->accept_peer(pair.from_b(Kind::committed)));
                break;
            case Transition::replacement:
                CHECK(pair.journal_a->abort());
                next.generation = 2; next.transaction[0] ^= 1; next.decision[0] ^= 1;
                break;
        }
    }
    bool apply() {
        switch (transition) {
            case Transition::begin:
            case Transition::replacement: return pair.journal_a->begin(next);
            case Transition::prepare: return pair.journal_a->prepare();
            case Transition::peer_vote:
            case Transition::peer_commit: return pair.journal_a->accept_peer(receipt);
            case Transition::completing: return pair.journal_a->mark_completing();
            case Transition::committed: return pair.journal_a->mark_committed();
            case Transition::peer_confirmed: return pair.journal_a->mark_peer_confirmed();
            case Transition::aborted: return pair.journal_a->abort();
        }
        return false;
    }
};
bool same_snapshot(const PairingCompletionSnapshot& a, const PairingCompletionSnapshot& b) {
    return a.phase == b.phase && a.transaction == b.transaction && a.peer == b.peer && a.generation == b.generation;
}

using MediaBytes = std::array<std::array<std::array<std::uint8_t, 64>, 2>, 5>;
MediaBytes media_bytes(const Storage& media) {
    MediaBytes result{};
    for (unsigned d = 0; d < 5; ++d) for (unsigned bank = 0; bank < 2; ++bank)
        result[d][bank] = media.memory.slot_bytes(static_cast<Domain>(d), bank);
    return result;
}
unsigned m2_refusal_failures = 0;
void m2_refuses_unchanged(Storage& media, const char* label) {
    const auto before = media_bytes(media);
    media.trace.clear();
    Journal journal(media);
    const bool accepted = journal.initialize();
    const bool unchanged = before == media_bytes(media);
    const auto changed = mutations(media);
    std::cout << "M2 " << label << " accepted=" << accepted << " unchanged=" << unchanged
              << " mutations=" << changed << std::endl;
    CHECK(unchanged && changed == 0);
    if (accepted || journal.snapshot().phase != Phase::uncertain) ++m2_refusal_failures;
}
void m2_wrong_first_magic() {
    Storage media;
    media.memory.corrupt_byte(static_cast<Domain>(0), 0, 0, 0xff ^ 'X');
    m2_refuses_unchanged(media, "wrong-first-magic");
}
std::array<std::uint8_t, 320> m2_first_bytes(Pair& pair) {
    std::array<std::uint8_t, 320> result{};
    for (unsigned d = 0; d < 5; ++d) {
        const auto& slot = pair.media_a.memory.slot_bytes(static_cast<Domain>(d), 0);
        std::memcpy(result.data() + d * 64, slot.data(), 64);
    }
    return result;
}
void m2_checksum(std::array<std::uint8_t, 320>& bytes) {
    InvitationKey hash{}; crypto_hash_sha256(hash.data(), bytes.data(), 304);
    std::memcpy(bytes.data() + 304, hash.data(), 12);
}
void m2_payload(Storage& media, unsigned bank, const std::array<std::uint8_t, 320>& bytes,
                unsigned prefix = 316) {
    for (unsigned d = 0; d < 5 && d * 64 < prefix; ++d) {
        const auto remaining = prefix - d * 64;
        const auto count = remaining < 64 ? remaining : 64;
        CHECK(media.memory.write_slot(static_cast<Domain>(d), bank, 0,
              {bytes.data() + d * 64, count}) == Error::none);
    }
}
void m2_mixed_bank() {
    {
        Pair pair;
        pair.media_a.memory.corrupt_byte(static_cast<Domain>(0), 1, 0, 0xff ^ 'X');
        m2_refuses_unchanged(pair.media_a, "valid-older-plus-unrelated-invalid");
    }
    for (const bool latest_second : {false, true}) {
        Pair pair, foreign;
        if (latest_second) pair.prepare();
        auto bytes = m2_first_bytes(foreign);
        bytes[8] = latest_second ? 3 : 2; // exact next-bank parity, unrelated binding
        bytes[48] ^= 1;
        m2_checksum(bytes);
        const unsigned bank = latest_second ? 0 : 1;
        for (unsigned d = 0; d < 5; ++d) CHECK(pair.media_a.memory.erase_slot(static_cast<Domain>(d), bank) == Error::none);
        m2_payload(pair.media_a, bank, bytes);
        m2_refuses_unchanged(pair.media_a, latest_second ? "valid-newer-plus-foreign-prefix" :
                                                          "valid-older-plus-foreign-prefix");
    }
}
void m2_hostile_first_shapes() {
    Pair pair;
    const auto canonical = m2_first_bytes(pair);
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        auto bytes = canonical;
        if (scenario == 0) bytes[4] ^= 1; // schema
        if (scenario == 1) bytes[8] = 2;  // first sequence
        if (scenario == 2) { bytes[6] = static_cast<std::uint8_t>(Phase::prepared); bytes[7] = Journal::kLocalPrepared; }
        if (scenario == 3) bytes[5] = 7;  // role
        if (scenario == 4) bytes[36] = 1; // reserved
        if (scenario == 5) bytes[176] = 1; // an unvoted first descriptor has no peer signature
        if (scenario == 6) bytes[304] ^= 1; // complete payload with wrong checksum
        if (scenario == 7) std::memset(bytes.data(), 0xff, 64); // write-order gap
        Storage media; m2_payload(media, 0, bytes);
        const auto label = std::string("hostile-first-shape-") + std::to_string(scenario);
        m2_refuses_unchanged(media, label.c_str());
    }
}
void m2_marker_progress() {
    constexpr std::array<std::uint8_t, 4> marker{0x44, 0x50, 0x43, 0x32};
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        const bool mixed = scenario >= 6;
        const auto style = mixed ? scenario - 6 : scenario;
        Pair pair(!mixed ? false : true);
        const unsigned bank = mixed ? 1 : 0;
        const std::uint8_t first = 'O';
        CHECK(pair.media_a.memory.write_slot(static_cast<Domain>(0), bank, 0, {&first, 1}) == Error::none);
        auto partial = marker;
        unsigned installed = style < 3 ? style + 1 : 3;
        if (style == 3) installed = 2;
        CHECK(pair.media_a.memory.write_slot(static_cast<Domain>(4), bank, 60,
              {partial.data(), installed}) == Error::none);
        if (style >= 3) {
            const std::array<std::uint8_t, 2> zeros{};
            const unsigned clear = style == 5 ? 2 : 1;
            CHECK(pair.media_a.memory.write_slot(static_cast<Domain>(4), bank, 60,
                  {zeros.data(), clear}) == Error::none);
        }
        const auto label = std::string("early-marker-") + std::to_string(scenario);
        m2_refuses_unchanged(pair.media_a, label.c_str());
    }
}
void m2_retired_candidate_prefixes() {
    for (const bool mixed : {false, true}) for (unsigned cleared = 1; cleared <= 4; ++cleared) {
        Pair pair(mixed);
        const unsigned bank = mixed ? 1 : 0;
        const std::uint8_t first = 'O'; const std::array<std::uint8_t, 4> zeros{};
        CHECK(pair.media_a.memory.write_slot(static_cast<Domain>(0), bank, 0, {&first, 1}) == Error::none);
        CHECK(pair.media_a.memory.write_slot(static_cast<Domain>(4), bank, 60, {zeros.data(), cleared}) == Error::none);
        const auto before = media_bytes(pair.media_a); pair.media_a.trace.clear();
        Journal journal(pair.media_a);
        CHECK(journal.initialize());
        CHECK(journal.snapshot().phase == (mixed ? Phase::confirmed : Phase::none));
        CHECK(media_bytes(pair.media_a) == before && mutations(pair.media_a) == 0);
    }
}
void m2_first_write_prefixes() {
    Pair pair;
    for (const unsigned role : {1U, 2U}) for (const bool aborted : {false, true}) {
        auto bytes = m2_first_bytes(pair);
        bytes[5] = static_cast<std::uint8_t>(role);
        bytes[6] = static_cast<std::uint8_t>(aborted ? Phase::aborted : Phase::confirmed);
        bytes[7] = aborted ? Journal::kLocalAborted : 0;
        m2_checksum(bytes);
        for (unsigned prefix = 0; prefix <= 316; ++prefix) {
            Storage media; m2_payload(media, 0, bytes, prefix);
            const auto before = media_bytes(media);
            Journal journal(media);
            CHECK(journal.initialize() && journal.snapshot().phase == Phase::none);
            CHECK(media_bytes(media) == before && mutations(media) == 0);
        }
        for (unsigned marker_prefix = 1; marker_prefix < 4; ++marker_prefix) {
            Storage media; m2_payload(media, 0, bytes);
            CHECK(media.memory.write_slot(static_cast<Domain>(4), 0, 60,
                  {bytes.data() + 316, marker_prefix}) == Error::none);
            const auto before = media_bytes(media);
            Journal journal(media);
            CHECK(journal.initialize() && journal.snapshot().phase == Phase::none);
            CHECK(media_bytes(media) == before && mutations(media) == 0);
        }
    }
}


}
int main(int argc, char** argv) {
    if (argc == 2) {
        const std::string name = argv[1];
        if (name == "wrong-first-magic") m2_wrong_first_magic();
        else if (name == "mixed-bank") m2_mixed_bank();
        else if (name == "hostile-first-shapes") m2_hostile_first_shapes();
        else if (name == "first-write-prefixes") m2_first_write_prefixes();
        else if (name == "marker-progress") m2_marker_progress();
        else if (name == "retired-candidate-prefixes") m2_retired_candidate_prefixes();
        else return 2;
        CHECK(m2_refusal_failures == 0);
        std::cout << "PASS named " << name << "\n";
        return 0;
    }
    unsigned groups = 0;
    {
        Pair pair;
        Receipt unavailable{};
        CHECK(pair.journal_a->snapshot().phase == Phase::confirmed);
        CHECK(!pair.journal_a->export_receipt(Kind::prepared, pair.a.owner, unavailable));
        CHECK(!pair.journal_a->export_receipt(Kind::committed, pair.a.owner, unavailable));
        CHECK(!pair.journal_a->mark_completing());
        pair.exchange_votes();
        const auto prepared = pair.from_a(Kind::prepared);
        const auto count = mutations(pair.media_a);
        CHECK(pair.journal_a->prepare());
        CHECK(pair.journal_a->accept_peer(pair.from_b(Kind::prepared)));
        CHECK(mutations(pair.media_a) == count);
        CHECK(pair.from_a(Kind::prepared) == prepared);
        CHECK(!pair.journal_a->abort());
        CHECK(!pair.journal_a->mark_committed());
        CHECK(!pair.journal_a->mark_peer_confirmed());
        CHECK(pair.journal_a->mark_completing() && pair.journal_b->mark_completing());
        CHECK(pair.journal_a->mark_committed() && pair.journal_b->mark_committed());
        const auto committed = pair.from_b(Kind::committed);
        CHECK(pair.journal_a->accept_peer(committed) && pair.journal_a->mark_peer_confirmed());
        CHECK(pair.journal_b->snapshot().phase == Phase::committed);
        CHECK(pair.journal_a->snapshot().phase == Phase::peer_confirmed);
        pair.restart_a();
        CHECK(pair.journal_a->snapshot().phase == Phase::peer_confirmed);
        CHECK(pair.from_a(Kind::prepared) == prepared);
        CHECK(pair.journal_a->accept_peer(committed));
        CHECK(pair.journal_b->accept_peer(pair.from_a(Kind::committed)));
        CHECK(pair.journal_b->mark_peer_confirmed());
        ++groups;
    }
    {
        Pair pair;
        pair.prepare();
        const auto valid = pair.from_a(Kind::prepared);
        const auto before = mutations(pair.media_b);
        for (unsigned byte : {0U, 1U, 2U, 3U, 4U, 35U, 36U, 99U}) {
            auto altered = valid; altered[byte] ^= 1;
            CHECK(!pair.journal_b->accept_peer(altered));
        }
        Identity stranger{27};
        CHECK(!pair.journal_b->accept_peer(signed_receipt(stranger, valid)));
        auto foreign = valid; foreign[4] ^= 1;
        CHECK(!pair.journal_b->accept_peer(signed_receipt(pair.a, foreign)));
        auto wrong_role = valid; wrong_role[2] = static_cast<std::uint8_t>(InvitationRole::responder);
        CHECK(!pair.journal_b->accept_peer(signed_receipt(pair.a, wrong_role)));
        auto premature = valid; premature[1] = static_cast<std::uint8_t>(Kind::committed);
        CHECK(!pair.journal_b->accept_peer(signed_receipt(pair.a, premature)));
        CHECK(mutations(pair.media_b) == before);
        CHECK(pair.journal_b->snapshot().phase == Phase::prepared);
        CHECK(pair.journal_b->accept_peer(valid));
        auto contradiction = valid; contradiction[1] = static_cast<std::uint8_t>(Kind::aborted);
        CHECK(!pair.journal_b->accept_peer(signed_receipt(pair.a, contradiction)));
        ++groups;
    }
    {
        Pair pair;
        CHECK(pair.journal_a->prepare() && pair.journal_b->abort());
        const auto positive = pair.from_a(Kind::prepared);
        const auto negative = pair.from_b(Kind::aborted);
        CHECK(pair.journal_a->accept_peer(negative) && pair.journal_b->accept_peer(positive));
        CHECK(pair.journal_a->snapshot().phase == Phase::aborted);
        CHECK(pair.journal_b->snapshot().phase == Phase::aborted);
        CHECK(!pair.journal_a->abort() && !pair.journal_b->prepare());
        Receipt unchanged{};
        CHECK(!pair.journal_a->export_receipt(Kind::aborted, pair.a.owner, unchanged));
        CHECK(!pair.journal_b->export_receipt(Kind::prepared, pair.b.owner, unchanged));
        CHECK(!pair.journal_a->mark_completing() && !pair.journal_b->mark_completing());
        pair.restart_a();
        CHECK(pair.journal_a->snapshot().phase == Phase::aborted);
        CHECK(pair.from_a(Kind::prepared) == positive);
        ++groups;
    }
    {
        // A failure after the complete marker must restore that exact vote.
        for (const auto fault : {Fault::write_after, Fault::sync_after, Fault::read_error}) {
            Pair pair;
            const unsigned nth = fault == Fault::write_after ? 6 : fault == Fault::sync_after ? 11 : 31;
            pair.media_a.arm(fault, nth);
            CHECK(!pair.journal_a->prepare());
            pair.restart_a();
            CHECK(pair.journal_a->snapshot().phase == Phase::prepared);
            CHECK(pair.journal_a->locally_prepared());
        }
        ++groups;
    }
    {
        Pair pair;
        CHECK(pair.journal_a->prepare());
        // Latest bank is slot1. Its commit marker remains complete.
        pair.media_a.memory.corrupt_byte(static_cast<Domain>(0), 1, 8, 1);
        pair.journal_a.reset(); pair.journal_a.emplace(pair.media_a);
        CHECK(!pair.journal_a->initialize());
        CHECK(pair.journal_a->snapshot().phase == Phase::uncertain);
        ++groups;
    }
    {
        struct Hook final : persistence::PersistentStorage {
            Storage inner;
            std::function<void()> hook = [] {};
            persistence::StorageReadResult read_slot(Domain d, std::size_t s, persistence::MutableStorageByteView out) override {
                hook(); return inner.read_slot(d, s, out);
            }
            Error erase_slot(Domain d, std::size_t s) override { hook(); return inner.erase_slot(d, s); }
            Error write_slot(Domain d, std::size_t s, std::size_t at, persistence::StorageByteView in) override {
                hook(); return inner.write_slot(d, s, at, in);
            }
            Error sync_slot(Domain d, std::size_t s) override { hook(); return inner.sync_slot(d, s); }
        } media;
        Journal journal(media);
        CHECK(journal.initialize());
        Pair pair;
        PairingCompletionRecord record{};
        CHECK(pair.journal_a->load(record) && journal.begin(record));
        bool called = false;
        media.hook = [&] { if (!called) { called = true; CHECK(!journal.prepare()); } };
        CHECK(!journal.prepare());
        CHECK(journal.snapshot().phase == Phase::uncertain);
        ++groups;
    }
    {
        // A pending vote cannot be replaced by another request or generation.
        Pair pair;
        PairingCompletionRecord next{};
        CHECK(pair.journal_a->load(next));
        next.generation = 2; next.transaction[0] ^= 1; next.decision[0] ^= 1;
        CHECK(!pair.journal_a->begin(next));
        CHECK(pair.journal_a->prepare());
        CHECK(!pair.journal_a->begin(next));
        CHECK(pair.journal_b->abort());
        CHECK(pair.journal_a->accept_peer(pair.from_b(Kind::aborted)));
        CHECK(pair.journal_a->begin(next));
        pair.restart_a();
        CHECK(pair.journal_a->snapshot().phase == Phase::confirmed);
        CHECK(pair.journal_a->snapshot().generation == 2);
        CHECK(pair.journal_a->snapshot().transaction == next.transaction);

        Pair completed;
        CHECK(completed.journal_a->load(next));
        completed.commit();
        CHECK(completed.journal_a->accept_peer(completed.from_b(Kind::committed)));
        CHECK(completed.journal_a->mark_peer_confirmed());
        next.transaction[0] ^= 1; next.decision[0] ^= 1;
        CHECK(!completed.journal_a->begin(next));  // the same generation stays consumed
        next.generation = 2;
        CHECK(completed.journal_a->begin(next));
        completed.restart_a();
        CHECK(completed.journal_a->snapshot().phase == Phase::confirmed);
        CHECK(completed.journal_a->snapshot().generation == 2);
        ++groups;
    }
    {
        // Every persistent transition is interrupted at every matching I/O.
        // Restart must select exactly its predecessor or complete successor.
        for (const auto transition : {Transition::begin, Transition::prepare, Transition::peer_vote,
                                     Transition::completing, Transition::committed, Transition::peer_commit,
                                     Transition::peer_confirmed, Transition::aborted, Transition::replacement}) {
            Attempt baseline(transition);
            const auto before = baseline.pair.journal_a->snapshot();
            baseline.pair.media_a.trace.clear();
            CHECK(baseline.apply());
            const auto after = baseline.pair.journal_a->snapshot();
            for (const auto fault : faults) {
                const auto operations = baseline.pair.media_a.count(operation(fault));
                CHECK(operations != 0);
                for (unsigned nth = 1; nth <= operations; ++nth) {
                    Attempt attempt(transition);
                    attempt.pair.media_a.arm(fault, nth);
                    CHECK(!attempt.apply());
                    CHECK(attempt.pair.journal_a->snapshot().phase == Phase::uncertain);
                    Receipt refused{};
                    for (const auto kind : {Kind::prepared, Kind::committed, Kind::aborted})
                        CHECK(!attempt.pair.journal_a->export_receipt(kind, attempt.pair.a.owner, refused));
                    attempt.pair.journal_a.reset();
                    attempt.pair.media_a.clear();
                    attempt.pair.journal_a.emplace(attempt.pair.media_a);
                    const bool restored = attempt.pair.journal_a->initialize();
                    const auto observed = attempt.pair.journal_a->snapshot();
                    CHECK(restored);
                    CHECK(same_snapshot(observed, before) || same_snapshot(observed, after));
                    if (observed.phase == Phase::none) {
                        // No first vote survived. The next normal begin may
                        // replace only this logical empty descriptor.
                        CHECK(transition == Transition::begin);
                        CHECK(!attempt.pair.journal_a->locally_prepared());
                        CHECK(!attempt.pair.journal_a->has_peer_committed());
                        for (const auto kind : {Kind::prepared, Kind::committed, Kind::aborted})
                            CHECK(!attempt.pair.journal_a->export_receipt(kind, attempt.pair.a.owner, refused));
                        auto next = attempt.next;
                        ++next.generation;
                        next.transaction[0] ^= 1;
                        CHECK(attempt.pair.journal_a->begin(next));
                        attempt.pair.journal_a.reset();
                        attempt.pair.journal_a.emplace(attempt.pair.media_a);
                        CHECK(attempt.pair.journal_a->initialize());
                        CHECK(attempt.pair.journal_a->snapshot().phase == Phase::confirmed);
                        CHECK(attempt.pair.journal_a->snapshot().generation == next.generation);
                        continue;
                    }
                    PairingCompletionRecord retained{};
                    CHECK(attempt.pair.journal_a->load(retained));
                    const bool committed = observed.phase == Phase::committed || observed.phase == Phase::peer_confirmed;
                    CHECK(attempt.pair.journal_a->export_receipt(Kind::committed, attempt.pair.a.owner, refused) == committed);
                    const bool prepared = (retained.flags & Journal::kLocalPrepared) != 0;
                    CHECK(attempt.pair.journal_a->export_receipt(Kind::prepared, attempt.pair.a.owner, refused) == prepared);
                    const bool aborted = (retained.flags & Journal::kLocalAborted) != 0;
                    CHECK(attempt.pair.journal_a->export_receipt(Kind::aborted, attempt.pair.a.owner, refused) == aborted);
                }
            }
        }
        ++groups;
    }
    {
        // Positive reservation follows durable, verified candidate bytes.
        // Refusal leaves the predecessor confirmed decision after restart.
        Pair pair;
        struct Reservation {
            Storage* media{};
            unsigned writes_before{};
            unsigned calls{};
            bool allowed{};
            static bool decide(void* context) {
                auto& self = *static_cast<Reservation*>(context);
                ++self.calls;
                CHECK(self.media->count('w') >= self.writes_before + 5);
                return self.allowed;
            }
        } reservation{&pair.media_a, pair.media_a.count('w'), 0, false};
        CHECK(!pair.journal_a->prepare(nullptr, &reservation, &Reservation::decide));
        CHECK(reservation.calls == 1);
        pair.restart_a();
        CHECK(pair.journal_a->snapshot().phase == Phase::confirmed);
        CHECK(!pair.journal_a->locally_prepared());
        Receipt refused{};
        CHECK(!pair.journal_a->export_receipt(Kind::prepared, pair.a.owner, refused));
        reservation.writes_before = pair.media_a.count('w');
        reservation.allowed = true;
        CHECK(pair.journal_a->prepare(nullptr, &reservation, &Reservation::decide));
        CHECK(reservation.calls == 2);
        pair.restart_a();
        CHECK(pair.journal_a->snapshot().phase == Phase::prepared);
        CHECK(pair.journal_a->locally_prepared());
        ++groups;
    }
    {
        // Logical empty is restricted to the canonical first-write layout.
        // Complete corrupt descriptors, invalid markers and second-bank
        // evidence cannot be converted into permission for a fresh attempt.
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            Pair pair(scenario == 0);
            if (scenario == 0)
                pair.media_a.memory.corrupt_byte(static_cast<Domain>(0), 0, 8, 1);
            else if (scenario == 1)
                pair.media_a.memory.corrupt_byte(static_cast<Domain>(4), 0, 60, 1);
            else
                pair.media_a.memory.corrupt_byte(static_cast<Domain>(0), 1, 0, 1);
            pair.journal_a.reset();
            pair.journal_a.emplace(pair.media_a);
            const auto count = mutations(pair.media_a);
            CHECK(!pair.journal_a->initialize());
            CHECK(pair.journal_a->snapshot().phase == Phase::uncertain);
            CHECK(mutations(pair.media_a) == count);
            Receipt refused{};
            for (const auto kind : {Kind::prepared, Kind::committed, Kind::aborted})
                CHECK(!pair.journal_a->export_receipt(kind, pair.a.owner, refused));
        }
        ++groups;
    }
    m2_wrong_first_magic(); ++groups;
    m2_mixed_bank(); ++groups;
    m2_hostile_first_shapes(); ++groups;
    m2_marker_progress(); ++groups;
    m2_retired_candidate_prefixes(); ++groups;
    m2_first_write_prefixes(); ++groups;
    CHECK(m2_refusal_failures == 0);
    std::cout << "PASS " << groups << " pairing completion journal groups\n";
    return 0;
}
