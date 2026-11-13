#include "opentrail/group_pair.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace opentrail::identity::roster {
namespace {

template <std::size_t N>
bool all_zero(const std::array<std::uint8_t, N>& value) {
    return std::all_of(value.begin(), value.end(), [](std::uint8_t b) { return b == 0; });
}

struct Writer {
    std::uint8_t* p;
    template <std::size_t N>
    void bytes(const std::array<std::uint8_t, N>& v) { std::memcpy(p, v.data(), N); p += N; }
    void text(std::string_view v) { std::memcpy(p, v.data(), v.size()); p += v.size(); }
    void u8(std::uint8_t v) { *p++ = v; }
    void u32(std::uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8) *p++ = static_cast<std::uint8_t>(v >> shift);
    }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v >> 32)); u32(static_cast<std::uint32_t>(v)); }
};

void common(Writer& w, const PairStart& s) {
    w.u8(static_cast<std::uint8_t>(s.purpose));
    w.bytes(s.group.admin);
    w.u64(s.group.group_id);
    w.u32(s.roster_version);
    w.bytes(s.roster_digest);
    w.u32(s.epoch);
    w.bytes(s.initiator);
    w.bytes(s.responder);
    w.u32(s.lifetime_ms);
}

bool same_group(const GroupName& a, const GroupName& b) {
    return a.admin == b.admin && a.group_id == b.group_id;
}

bool same_start(const PairStart& a, const PairStart& b) {
    return a.purpose == b.purpose && same_group(a.group, b.group) && a.roster_version == b.roster_version &&
           a.roster_digest == b.roster_digest && a.epoch == b.epoch && a.initiator == b.initiator &&
           a.responder == b.responder && a.lifetime_ms == b.lifetime_ms &&
           a.initiator_ephemeral == b.initiator_ephemeral && a.initiator_challenge == b.initiator_challenge &&
           a.initiator_boot == b.initiator_boot && a.signature == b.signature;
}

std::size_t index_of(const Roster& roster, const PublicKey& key) {
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        if (roster.members[i].key == key) return i;
    }
    return kMaximumMembers;
}

std::uint64_t deadline_from(std::uint64_t now, std::uint32_t lifetime) {
    return now > std::numeric_limits<std::uint64_t>::max() - lifetime ? std::numeric_limits<std::uint64_t>::max()
                                                                       : now + lifetime;
}

bool on_roster(const RosterState& roster, std::uint32_t version, const Digest& digest) {
    return roster.roster().version == version && roster.digest() == digest;
}

// Binding ids are unique across every GroupState in the program, so an id kept from one group
// can never name a binding of another group (or of a group created later in the same place).
std::atomic<std::uint64_t> next_binding_id{1};

// 64 bits: even one id per microsecond lasts over 500,000 years, so wrap is not handled.
std::uint64_t issue_binding_id() { return next_binding_id.fetch_add(1, std::memory_order_relaxed); }

// Marks a GroupState busy for the length of a pair step (no reentrant provider calls).
class Busy {
public:
    explicit Busy(bool& flag) : flag_(flag) { flag_ = true; }
    ~Busy() { flag_ = false; }
    Busy(const Busy&) = delete;
    Busy& operator=(const Busy&) = delete;
private:
    bool& flag_;
};

}  // namespace

// ------------------------------------------------------------------ helpers

std::size_t start_bytes(const PairStart& start, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kStartBytes) return 0;
    Writer w{out};
    w.text(kStartDomain);
    common(w, start);
    w.bytes(start.initiator_ephemeral);
    w.bytes(start.initiator_challenge);
    w.bytes(start.initiator_boot);
    return static_cast<std::size_t>(w.p - out);
}

std::size_t binding_bytes(const PairStart& start, const PairReply& reply, std::uint8_t* out,
                          std::size_t capacity) {
    if (out == nullptr || capacity < kBindingBytes) return 0;
    Writer w{out};
    w.text(kBindingDomain);
    common(w, start);
    w.bytes(start.initiator_ephemeral);
    w.bytes(reply.responder_ephemeral);
    w.bytes(start.initiator_challenge);
    w.bytes(reply.responder_challenge);
    w.bytes(start.initiator_boot);
    w.bytes(reply.responder_boot);
    return static_cast<std::size_t>(w.p - out);
}

bool start_wins(const PairStart& mine, const PairStart& theirs) {
    if (mine.epoch != theirs.epoch) return mine.epoch > theirs.epoch;
    return std::lexicographical_compare(mine.initiator.begin(), mine.initiator.end(), theirs.initiator.begin(),
                                        theirs.initiator.end());
}

// ------------------------------------------------------------------ PairRecord basics

std::uint32_t PairRecord::next_epoch() const {
    return used_ == std::numeric_limits<std::uint32_t>::max() ? 0 : used_ + 1;
}

bool PairRecord::acceptable(std::uint32_t epoch) const {
    const std::uint64_t e = epoch, u = used_;
    return e == u + 1 || e == u + 2;  // D-072/D-074: the next number, or one past it (catch-up)
}

bool PairRecord::new_challenge(RandomSource& rng, Challenge& out) {
    Challenge c{};
    if (!rng.fill(c.data(), c.size()) || all_zero(c)) return false;
    for (const Challenge& seen : own_challenges_) {
        if (seen == c) return false;
    }
    own_challenges_[own_next_] = c;
    own_next_ = (own_next_ + 1) % kRememberedChallenges;
    out = c;
    return true;
}

// Expired work never takes part in anything. Clock regression fails closed.
void PairRecord::expire(std::uint64_t now_ms) {
    if (own_live_ && (now_ms < own_time_ || now_ms > own_deadline_)) own_live_ = false;
    for (Pending& p : pending_) {
        if (p.live && (now_ms < p.time || now_ms > p.deadline)) p = Pending{};
    }
}

void PairRecord::drop_unfinished() {
    own_live_ = false;
    pending_ = {};
    if (stage_ == PairStep::bound) stage_ = PairStep::none;
}

bool PairRecord::within_budget(const std::array<std::uint64_t, kAnswersPerWindow>& times, std::size_t count,
                               std::uint64_t now_ms) {
    std::size_t recent = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (now_ms >= times[i] && now_ms - times[i] < kAnswerWindowMs) ++recent;
    }
    return recent < times.size();
}

void PairRecord::spend(std::array<std::uint64_t, kAnswersPerWindow>& times, std::size_t& next, std::size_t& count,
                       std::uint64_t now_ms) {
    times[next] = now_ms;
    next = (next + 1) % times.size();
    count = std::min(count + 1, times.size());
}

// A new verified binding replaces any earlier binding stage, own start and pending answers.
void PairRecord::bind(std::uint64_t id, PairRole role, const PairStart& s, const PairReply& r, const PairConfirm& c,
                      std::optional<VerifiedPairBinding>& verified) {
    stage_ = PairStep::bound;
    bound_id_ = id;
    bound_start_ = s;
    used_ = s.epoch;
    history_ = true;
    own_live_ = false;
    pending_ = {};
    verified.emplace(VerifiedPairBinding(id, role, s, r, c));
}

// Rechecks the live roster: active, both members still listed with the same added-in
// versions (the membership this record was made for), same group.
PairOutcome PairRecord::live(const RosterState& roster, Live& out) const {
    if (roster.state_ != State::active || !roster.has_roster_ || roster.own_ != self_key_) {
        return PairOutcome::roster_not_active;
    }
    const std::size_t si = index_of(roster.held_, self_key_);
    const std::size_t pi = index_of(roster.held_, peer_key_);
    if (pi == kMaximumMembers || si == kMaximumMembers ||
        roster.held_.members[pi].added_in != peer_added_ || roster.held_.members[si].added_in != self_added_ ||
        roster.held_.group_id != group_.group_id || roster.held_fingerprints_[0] != group_.admin) {
        return PairOutcome::no_pair;
    }
    out.self = &roster.held_.members[si];
    out.peer = &roster.held_.members[pi];
    out.self_fp = roster.held_fingerprints_[si];
    out.peer_fp = roster.held_fingerprints_[pi];
    return PairOutcome::ok;
}

// ------------------------------------------------------------------ initiator side

PairOutcome PairRecord::start(const RosterState& roster, const Ephemeral& eph, const BootToken& boot,
                              std::uint64_t now_ms, const RosterSigner& signer, RandomSource& rng, PairStart& out) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (signer.public_key() != self_key_) return PairOutcome::wrong_signer;
    expire(now_ms);
    const PairPurpose purpose = history_ ? PairPurpose::rekey : PairPurpose::introduce;
    if (purpose == PairPurpose::introduce && l.self->added_in < l.peer->added_in) return PairOutcome::wrong_role;
    const std::uint32_t epoch = next_epoch();
    if (epoch == 0) return PairOutcome::epoch_exhausted;
    if (all_zero(eph) || all_zero(boot)) return PairOutcome::bad_fresh_values;
    PairStart s{};
    s.purpose = purpose;
    s.group = group_;
    s.roster_version = roster.held_.version;
    s.roster_digest = roster.held_digest_;
    s.epoch = epoch;
    s.initiator = l.self_fp;
    s.responder = l.peer_fp;
    s.lifetime_ms = kPairLifetimeMs;
    s.initiator_ephemeral = eph;
    s.initiator_boot = boot;
    if (!new_challenge(rng, s.initiator_challenge)) return PairOutcome::crypto_failure;
    std::array<std::uint8_t, kStartBytes> bytes{};
    const std::size_t n = start_bytes(s, bytes.data(), bytes.size());
    if (n == 0 || !signer.sign(bytes.data(), n, s.signature)) return PairOutcome::crypto_failure;
    // A new own start replaces an unfinished own start (nothing used). A binding stage, including
    // activation intent, stays until a new binding verifies.
    own_live_ = true;
    own_start_ = s;
    own_time_ = now_ms;
    own_deadline_ = deadline_from(now_ms, kPairLifetimeMs);
    out = s;
    return PairOutcome::ok;
}

PairOutcome PairRecord::on_reply(const RosterState& roster, const PairReply& reply, std::uint64_t now_ms,
                                 const RosterCrypto& crypto, const RosterSigner& signer, std::uint64_t id,
                                 PairConfirm& out, std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (!own_live_) return PairOutcome::wrong_state;
    if (!on_roster(roster, own_start_.roster_version, own_start_.roster_digest)) {
        own_live_ = false;
        return PairOutcome::roster_changed;
    }
    if (now_ms < own_time_ || now_ms > own_deadline_) {
        own_live_ = false;
        return PairOutcome::expired;
    }
    if (signer.public_key() != self_key_) return PairOutcome::wrong_signer;
    const PairStart s = own_start_;  // verified, signed and issued from this one copy
    if (all_zero(reply.responder_ephemeral) || all_zero(reply.responder_challenge) ||
        all_zero(reply.responder_boot) || reply.responder_challenge == s.initiator_challenge ||
        reply.responder_ephemeral == s.initiator_ephemeral) {
        return PairOutcome::bad_fresh_values;
    }
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(s, reply, bytes.data(), bytes.size());
    if (n == 0) return PairOutcome::crypto_failure;
    // A forged or replayed reply does not end the attempt; the deadline does.
    if (!crypto.verify(reply.signature, bytes.data(), n, peer_key_)) return PairOutcome::bad_signature;
    PairConfirm confirm{};
    if (!signer.sign(bytes.data(), n, confirm.signature)) return PairOutcome::crypto_failure;
    bind(id, PairRole::initiator, s, reply, confirm, verified);
    out = confirm;
    return PairOutcome::ok;
}

// ------------------------------------------------------------------ responder side

PairOutcome PairRecord::on_start(const RosterState& roster, const PairStart& start, const Ephemeral& eph,
                                 const BootToken& boot, std::uint64_t now_ms, const RosterCrypto& crypto,
                                 const RosterSigner& signer, RandomSource& rng, PairReply& out) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (signer.public_key() != self_key_) return PairOutcome::wrong_signer;
    if (!same_group(start.group, group_) || start.initiator != l.peer_fp || start.responder != l.self_fp ||
        (start.purpose != PairPurpose::introduce && start.purpose != PairPurpose::rekey)) {
        return PairOutcome::not_for_this_pair;
    }
    if (start.lifetime_ms == 0 || start.lifetime_ms > kPairLifetimeMs) return PairOutcome::bad_lifetime;
    std::array<std::uint8_t, kStartBytes> sbytes{};
    const std::size_t sn = start_bytes(start, sbytes.data(), sbytes.size());
    if (sn == 0) return PairOutcome::crypto_failure;
    // Everything below acts only on a start signed by the listed peer.
    if (!crypto.verify(start.signature, sbytes.data(), sn, peer_key_)) return PairOutcome::bad_signature;
    expire(now_ms);
    for (Pending& p : pending_) {
        if (!p.live || !same_start(start, p.start)) continue;
        if (!on_roster(roster, p.start.roster_version, p.start.roster_digest)) {
            p = Pending{};  // answered under an older roster: never resent
            break;
        }
        if (!within_budget(resend_times_, resend_count_, now_ms)) return PairOutcome::rate_limited;
        spend(resend_times_, resend_next_, resend_count_, now_ms);
        out = p.reply;
        return PairOutcome::resent_reply;
    }
    for (const Signature& seen : answered_) {
        if (seen == start.signature) return PairOutcome::duplicate_start;
    }
    if (start.roster_version > roster.held_.version) return PairOutcome::peer_roster_newer;
    if (start.roster_version < roster.held_.version) return PairOutcome::peer_roster_older;
    if (start.roster_digest != roster.held_digest_) return PairOutcome::peer_roster_differs;
    if (start.purpose == PairPurpose::introduce) {
        if (l.peer->added_in < l.self->added_in) return PairOutcome::wrong_role;  // newer member starts
        if (!roster.accepted(peer_key_)) return PairOutcome::no_consent;
    } else if (!history_) {
        return PairOutcome::wrong_purpose;
    }
    if (!acceptable(start.epoch)) return PairOutcome::bad_epoch;
    if (all_zero(start.initiator_ephemeral) || all_zero(start.initiator_challenge) || all_zero(start.initiator_boot) ||
        all_zero(eph) || all_zero(boot) || eph == start.initiator_ephemeral) {
        return PairOutcome::bad_fresh_values;
    }
    // Crossing starts, decided on the verified peer start (an expired own start already dropped).
    if (own_live_) {
        const bool peer_introduces_against_rekey =
            start.purpose == PairPurpose::introduce && own_start_.purpose == PairPurpose::rekey;
        const bool own_introduces_against_rekey =
            own_start_.purpose == PairPurpose::introduce && start.purpose == PairPurpose::rekey;
        if (!peer_introduces_against_rekey && (own_introduces_against_rekey || start_wins(own_start_, start))) {
            return PairOutcome::local_attempt_wins;
        }
    }
    // A slot: free, or holding an answer for an older epoch. A live answer is never displaced.
    std::size_t slot = pending_.size();
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (!pending_[i].live || pending_[i].start.epoch < start.epoch) { slot = i; break; }
    }
    if (slot == pending_.size()) return PairOutcome::rate_limited;
    if (!within_budget(answer_times_, answer_count_, now_ms)) return PairOutcome::rate_limited;
    PairReply reply{};
    reply.responder_ephemeral = eph;
    reply.responder_boot = boot;
    if (!new_challenge(rng, reply.responder_challenge) || reply.responder_challenge == start.initiator_challenge) {
        return PairOutcome::crypto_failure;
    }
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(start, reply, bytes.data(), bytes.size());
    if (n == 0 || !signer.sign(bytes.data(), n, reply.signature)) return PairOutcome::crypto_failure;
    // Only now does anything change. An own unanswered start loses. The binding stage, including
    // activation intent, stays until the new binding verifies.
    own_live_ = false;
    // D-074 catch-up: a start signed by the peer at used + 2 counts as proof that the peer used
    // the number between.
    if (static_cast<std::uint64_t>(start.epoch) == static_cast<std::uint64_t>(used_) + 2) used_ = start.epoch - 1;
    history_ = true;  // this board signed a binding over the peer's verified start
    spend(answer_times_, answer_next_, answer_count_, now_ms);
    for (Pending& p : pending_) {
        if (p.live && p.start.epoch < start.epoch) p = Pending{};
    }
    Pending& p = pending_[slot];
    p.live = true;
    p.start = start;
    p.reply = reply;
    p.time = now_ms;
    p.deadline = deadline_from(now_ms, start.lifetime_ms);
    answered_[answered_next_] = start.signature;
    answered_next_ = (answered_next_ + 1) % kRememberedStarts;
    out = reply;
    return PairOutcome::ok;
}

PairOutcome PairRecord::on_confirm(const RosterState& roster, const PairConfirm& confirm, std::uint64_t now_ms,
                                   const RosterCrypto& crypto, std::uint64_t id,
                                   std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    expire(now_ms);
    bool any = false;
    for (Pending& p : pending_) {
        if (!p.live) continue;
        if (!on_roster(roster, p.start.roster_version, p.start.roster_digest) || p.start.epoch <= used_ ||
            (p.start.purpose == PairPurpose::introduce && !roster.accepted(peer_key_))) {
            p = Pending{};
            continue;
        }
        any = true;
        std::array<std::uint8_t, kBindingBytes> bytes{};
        const std::size_t n = binding_bytes(p.start, p.reply, bytes.data(), bytes.size());
        if (n == 0 || !crypto.verify(confirm.signature, bytes.data(), n, peer_key_)) continue;
        const PairStart s = p.start;
        const PairReply r = p.reply;
        bind(id, PairRole::responder, s, r, confirm, verified);
        return PairOutcome::ok;
    }
    return any ? PairOutcome::bad_signature : PairOutcome::wrong_state;
}

// ------------------------------------------------------------------ durable steps

PairOutcome PairRecord::activation_possible(const RosterState& roster, std::uint64_t id) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (stage_ != PairStep::bound || bound_id_ != id || bound_start_.epoch != used_) return PairOutcome::wrong_state;
    if (!on_roster(roster, bound_start_.roster_version, bound_start_.roster_digest)) return PairOutcome::roster_changed;
    stage_ = PairStep::activation_possible;
    return PairOutcome::ok;
}

PairOutcome PairRecord::commit(const RosterState& roster, std::uint64_t id) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    // A D-074 catch-up past this binding proves the peer used a later epoch: the intent stays
    // for recovery, but the older binding never commits.
    if (stage_ != PairStep::activation_possible || bound_id_ != id || bound_start_.epoch != used_) {
        return PairOutcome::wrong_state;
    }
    if (!on_roster(roster, bound_start_.roster_version, bound_start_.roster_digest)) return PairOutcome::roster_changed;
    committed_ = bound_start_.epoch;
    stage_ = PairStep::committed;
    return PairOutcome::ok;
}

// A new roster was accepted and this pair is still the same membership: work made under the
// old roster cannot finish. ACTIVATION_POSSIBLE stays until recovery (it cannot commit).
void PairRecord::roster_applied(const RosterState& roster) {
    if (own_live_ && !on_roster(roster, own_start_.roster_version, own_start_.roster_digest)) own_live_ = false;
    if (stage_ == PairStep::bound && !on_roster(roster, bound_start_.roster_version, bound_start_.roster_digest)) {
        stage_ = PairStep::none;
    }
    for (Pending& p : pending_) {
        if (p.live && !on_roster(roster, p.start.roster_version, p.start.roster_digest)) p = Pending{};
    }
}

void PairRecord::restart() {
    own_live_ = false;
    pending_ = {};
    if (stage_ == PairStep::bound || stage_ == PairStep::activation_possible) stage_ = PairStep::none;  // never resumed
    answered_ = {};
    answered_next_ = 0;
    answer_times_ = {};
    answer_next_ = answer_count_ = 0;
    resend_times_ = {};
    resend_next_ = resend_count_ = 0;
}

PairStatus PairRecord::status() const {
    PairStatus s;
    s.binding = stage_;
    s.binding_id = stage_ == PairStep::none ? 0 : bound_id_;
    s.step = stage_;
    for (const Pending& p : pending_) {
        if (p.live) s.step = PairStep::awaiting_confirm;
    }
    if (own_live_) s.step = PairStep::awaiting_reply;
    s.used_epoch = used_;
    s.committed_epoch = committed_;
    s.next_epoch = next_epoch();
    s.history = history_;
    s.next_purpose = history_ ? PairPurpose::rekey : PairPurpose::introduce;
    return s;
}

// ------------------------------------------------------------------ GroupState

bool GroupState::initialize(const PublicKey& admin, std::uint64_t group_id, const PublicKey& own,
                            std::uint32_t minimum_version) {
    if (busy_) return false;
    return roster_.initialize(admin, group_id, own, minimum_version);
}

Outcome GroupState::offer(const std::uint8_t* bytes, std::size_t size, const RosterCrypto& crypto) {
    if (busy_) return Outcome::busy;
    Busy guard(busy_);
    const Outcome o = roster_.offer(bytes, size, crypto);
    if (o == Outcome::accepted || o == Outcome::accepted_removed_self) apply_roster();
    if (o == Outcome::conflict) {
        for (std::optional<PairRecord>& r : pairs_) {
            if (r) r->drop_unfinished();  // work interrupted by a conflict is not resumed
        }
    }
    return o;
}

bool GroupState::keep_current() {
    if (busy_) return false;
    return roster_.keep_current();
}

void GroupState::stop_trusting() {
    if (busy_) return;
    roster_.stop_trusting();
    pairs_ = {};  // the design deletes all pairs in the group
}

bool GroupState::accept_member(const PublicKey& key) {
    if (busy_) return false;
    return roster_.accept_member(key);
}

void GroupState::apply_roster() {
    for (std::optional<PairRecord>& r : pairs_) {
        if (!r) continue;
        PairRecord::Live l;
        if (roster_.state() != State::active || r->live(roster_, l) != PairOutcome::ok) {
            r.reset();  // removed, re-added (new membership) or this board left: record ends
        } else {
            r->roster_applied(roster_);
        }
    }
}

const PairRecord* GroupState::find(const PublicKey& peer) const {
    for (const std::optional<PairRecord>& r : pairs_) {
        if (r && r->peer_key_ == peer) return &*r;
    }
    return nullptr;
}

PairRecord* GroupState::record(const PublicKey& peer, bool create) {
    for (std::optional<PairRecord>& r : pairs_) {
        if (r && r->peer_key_ == peer) return &*r;
    }
    if (!create || roster_.state() != State::active || !roster_.has_roster() || peer == roster_.own_) return nullptr;
    const Roster& held = roster_.roster();
    const std::size_t si = index_of(held, roster_.own_);
    const std::size_t pi = index_of(held, peer);
    if (si == kMaximumMembers || pi == kMaximumMembers) return nullptr;
    // Introduced pairs only: each member's pair with the admin is the in-person first pairing.
    if (held.members[si].role == Role::admin || held.members[pi].role == Role::admin) return nullptr;
    for (std::optional<PairRecord>& r : pairs_) {
        if (r) continue;
        r.emplace(PairRecord{});
        r->group_.admin = roster_.fingerprints()[0];
        r->group_.group_id = held.group_id;
        r->self_key_ = roster_.own_;
        r->peer_key_ = peer;
        r->self_added_ = held.members[si].added_in;
        r->peer_added_ = held.members[pi].added_in;
        return &*r;
    }
    return nullptr;
}

// Every pair step copies its inputs before any provider runs (no caller alias can change them).
PairOutcome GroupState::pair_start(const PublicKey& peer_in, const Ephemeral& eph_in, const BootToken& boot_in,
                                   std::uint64_t now_ms, const RosterSigner& signer, RandomSource& rng, PairStart& out) {
    if (busy_) return PairOutcome::busy;
    Busy guard(busy_);
    const PublicKey peer = peer_in;
    const Ephemeral eph = eph_in;
    const BootToken boot = boot_in;
    if (roster_.state() != State::active) return PairOutcome::roster_not_active;
    PairRecord* r = record(peer, true);
    return r == nullptr ? PairOutcome::no_pair : r->start(roster_, eph, boot, now_ms, signer, rng, out);
}

PairOutcome GroupState::pair_on_start(const PairStart& start_in, const Ephemeral& eph_in, const BootToken& boot_in,
                                      std::uint64_t now_ms, const RosterCrypto& crypto, const RosterSigner& signer,
                                      RandomSource& rng, PairReply& out) {
    if (busy_) return PairOutcome::busy;
    Busy guard(busy_);
    const PairStart start = start_in;
    const Ephemeral eph = eph_in;
    const BootToken boot = boot_in;
    if (roster_.state() != State::active) return PairOutcome::roster_not_active;
    PublicKey peer{};
    if (!roster_.key_for(start.initiator, peer)) return PairOutcome::no_pair;
    PairRecord* r = record(peer, true);
    return r == nullptr ? PairOutcome::no_pair : r->on_start(roster_, start, eph, boot, now_ms, crypto, signer, rng, out);
}

PairOutcome GroupState::pair_on_reply(const PublicKey& peer_in, const PairReply& reply_in, std::uint64_t now_ms,
                                      const RosterCrypto& crypto, const RosterSigner& signer, PairConfirm& out,
                                      std::optional<VerifiedPairBinding>& verified) {
    // Inputs are copied before the output is cleared: an input may live inside `verified`.
    const PublicKey peer = peer_in;
    const PairReply reply = reply_in;
    verified.reset();
    if (busy_) return PairOutcome::busy;
    Busy guard(busy_);
    PairRecord* r = record(peer, false);
    if (r == nullptr) return PairOutcome::no_pair;
    return r->on_reply(roster_, reply, now_ms, crypto, signer, issue_binding_id(), out, verified);
}

PairOutcome GroupState::pair_on_confirm(const PublicKey& peer_in, const PairConfirm& confirm_in, std::uint64_t now_ms,
                                        const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified) {
    const PublicKey peer = peer_in;
    const PairConfirm confirm = confirm_in;
    verified.reset();
    if (busy_) return PairOutcome::busy;
    Busy guard(busy_);
    PairRecord* r = record(peer, false);
    if (r == nullptr) return PairOutcome::no_pair;
    return r->on_confirm(roster_, confirm, now_ms, crypto, issue_binding_id(), verified);
}

PairOutcome GroupState::pair_activation_possible(const PublicKey& peer, std::uint64_t binding_id) {
    if (busy_) return PairOutcome::busy;
    PairRecord* r = record(peer, false);
    return r == nullptr ? PairOutcome::no_pair : r->activation_possible(roster_, binding_id);
}

PairOutcome GroupState::pair_commit(const PublicKey& peer, std::uint64_t binding_id) {
    if (busy_) return PairOutcome::busy;
    PairRecord* r = record(peer, false);
    return r == nullptr ? PairOutcome::no_pair : r->commit(roster_, binding_id);
}

void GroupState::pair_cancel(const PublicKey& peer) {
    if (busy_) return;
    PairRecord* r = record(peer, false);
    if (r != nullptr) r->drop_unfinished();
}

void GroupState::restart() {
    if (busy_) return;
    for (std::optional<PairRecord>& r : pairs_) {
        if (r) r->restart();
    }
}

std::optional<PairStatus> GroupState::pair_status(const PublicKey& peer) const {
    const PairRecord* r = find(peer);
    return r == nullptr ? std::nullopt : std::optional<PairStatus>(r->status());
}

}  // namespace opentrail::identity::roster
