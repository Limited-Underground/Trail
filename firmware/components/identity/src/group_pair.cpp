#include "opentrail/group_pair.hpp"

#include <algorithm>
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
    return e == u + 1 || e == u + 2;  // D-072: the next number, or one past it (catch-up)
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

void PairRecord::drop_attempt() {
    step_ = PairStep::none;
    start_ = PairStart{};
    reply_ = PairReply{};
    confirm_ = PairConfirm{};
    time_ = deadline_ = 0;
}

// Expired attempts never take part in anything (own start, pending answers). Clock regression
// fails closed.
void PairRecord::expire(std::uint64_t now_ms) {
    if (step_ == PairStep::awaiting_reply && (now_ms < time_ || now_ms > deadline_)) drop_attempt();
    for (Pending& p : pending_) {
        if (p.live && (now_ms < p.time || now_ms > p.deadline)) p = Pending{};
    }
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
    if (step_ == PairStep::activation_possible) return PairOutcome::wrong_state;
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
    // A new own start replaces an unfinished own attempt; nothing was used by it.
    if (step_ == PairStep::awaiting_reply || step_ == PairStep::bound) drop_attempt();
    role_ = PairRole::initiator;
    start_ = s;
    step_ = PairStep::awaiting_reply;
    time_ = now_ms;
    deadline_ = deadline_from(now_ms, kPairLifetimeMs);
    out = s;
    return PairOutcome::ok;
}

PairOutcome PairRecord::on_reply(const RosterState& roster, const PairReply& reply, std::uint64_t now_ms,
                                 const RosterCrypto& crypto, const RosterSigner& signer, PairConfirm& out,
                                 std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (step_ != PairStep::awaiting_reply) return PairOutcome::wrong_state;
    if (!on_roster(roster, start_.roster_version, start_.roster_digest)) {
        drop_attempt();
        return PairOutcome::roster_changed;
    }
    if (now_ms < time_ || now_ms > deadline_) {
        drop_attempt();
        return PairOutcome::expired;
    }
    if (signer.public_key() != self_key_) return PairOutcome::wrong_signer;
    if (all_zero(reply.responder_ephemeral) || all_zero(reply.responder_challenge) ||
        all_zero(reply.responder_boot) || reply.responder_challenge == start_.initiator_challenge ||
        reply.responder_ephemeral == start_.initiator_ephemeral) {
        return PairOutcome::bad_fresh_values;
    }
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(start_, reply, bytes.data(), bytes.size());
    if (n == 0) return PairOutcome::crypto_failure;
    // A forged or replayed reply does not end the attempt; the deadline does.
    if (!crypto.verify(reply.signature, bytes.data(), n, peer_key_)) return PairOutcome::bad_signature;
    PairConfirm confirm{};
    if (!signer.sign(bytes.data(), n, confirm.signature)) return PairOutcome::crypto_failure;
    reply_ = reply;
    confirm_ = confirm;
    step_ = PairStep::bound;
    used_ = start_.epoch;
    history_ = true;
    pending_ = {};
    verified.emplace(VerifiedPairBinding(PairRole::initiator, start_, reply_, confirm_));
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
    // Crossing starts, decided on the verified peer start (own expired start already dropped).
    if (step_ == PairStep::awaiting_reply) {
        const bool peer_introduces_against_rekey =
            start.purpose == PairPurpose::introduce && start_.purpose == PairPurpose::rekey;
        const bool own_introduces_against_rekey =
            start_.purpose == PairPurpose::introduce && start.purpose == PairPurpose::rekey;
        if (!peer_introduces_against_rekey && (own_introduces_against_rekey || start_wins(start_, start))) {
            return PairOutcome::local_attempt_wins;
        }
    }
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
    // Only now does anything change. An own unanswered start loses; a bound or
    // activation-possible attempt that is not committed is retired (the peer has moved on).
    if (step_ == PairStep::awaiting_reply || step_ == PairStep::bound || step_ == PairStep::activation_possible) {
        drop_attempt();
    }
    // D-072 catch-up: a start at used + 2 proves the peer used the number between.
    if (static_cast<std::uint64_t>(start.epoch) == static_cast<std::uint64_t>(used_) + 2) used_ = start.epoch - 1;
    history_ = true;  // this board signed a binding over the peer's verified start
    spend(answer_times_, answer_next_, answer_count_, now_ms);
    // Keep earlier pending answers for the same epoch (a live answer is never displaced by a
    // replayed one while the budget lasts); drop answers for older epochs.
    for (Pending& p : pending_) {
        if (p.live && p.start.epoch < start.epoch) p = Pending{};
    }
    std::size_t slot = 0;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (!pending_[i].live) { slot = i; break; }
        if (pending_[i].time < oldest) { oldest = pending_[i].time; slot = i; }
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
                                   const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified) {
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
        if (step_ == PairStep::awaiting_reply || step_ == PairStep::bound) drop_attempt();
        role_ = PairRole::responder;
        start_ = p.start;
        reply_ = p.reply;
        confirm_ = confirm;
        step_ = PairStep::bound;
        used_ = start_.epoch;
        pending_ = {};
        verified.emplace(VerifiedPairBinding(PairRole::responder, start_, reply_, confirm_));
        return PairOutcome::ok;
    }
    return any ? PairOutcome::bad_signature : PairOutcome::wrong_state;
}

// ------------------------------------------------------------------ durable steps

PairOutcome PairRecord::activation_possible(const RosterState& roster, std::uint32_t epoch) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (step_ != PairStep::bound || start_.epoch != epoch || epoch != used_) return PairOutcome::wrong_state;
    if (!on_roster(roster, start_.roster_version, start_.roster_digest)) return PairOutcome::roster_changed;
    step_ = PairStep::activation_possible;
    return PairOutcome::ok;
}

PairOutcome PairRecord::commit(const RosterState& roster, std::uint32_t epoch) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (step_ != PairStep::activation_possible || start_.epoch != epoch) return PairOutcome::wrong_state;
    if (!on_roster(roster, start_.roster_version, start_.roster_digest)) return PairOutcome::roster_changed;
    committed_ = epoch;
    step_ = PairStep::committed;
    return PairOutcome::ok;
}

// A new roster was accepted and this pair is still the same membership: attempts made under
// the old roster cannot finish. ACTIVATION_POSSIBLE stays until recovery (it cannot commit).
void PairRecord::roster_applied(const RosterState& roster) {
    if ((step_ == PairStep::awaiting_reply || step_ == PairStep::bound) &&
        !on_roster(roster, start_.roster_version, start_.roster_digest)) {
        drop_attempt();
    }
    for (Pending& p : pending_) {
        if (p.live && !on_roster(roster, p.start.roster_version, p.start.roster_digest)) p = Pending{};
    }
}

void PairRecord::restart() {
    if (step_ != PairStep::committed) drop_attempt();  // retired, never resumed
    pending_ = {};
    answered_ = {};
    answered_next_ = 0;
    answer_times_ = {};
    answer_next_ = answer_count_ = 0;
    resend_times_ = {};
    resend_next_ = resend_count_ = 0;
}

PairStatus PairRecord::status() const {
    PairStatus s;
    s.step = step_;
    if (s.step == PairStep::none) {
        for (const Pending& p : pending_) {
            if (p.live) s.step = PairStep::awaiting_confirm;
        }
    }
    s.used_epoch = used_;
    s.committed_epoch = committed_;
    s.next_epoch = next_epoch();
    s.history = history_;
    s.next_purpose = history_ ? PairPurpose::rekey : PairPurpose::introduce;
    return s;
}

// ------------------------------------------------------------------ GroupState

GroupState::GroupState(GroupState&& other) noexcept : roster_(other.roster_), pairs_(std::move(other.pairs_)) {
    other.roster_ = RosterState{};
    other.pairs_ = {};
}

GroupState& GroupState::operator=(GroupState&& other) noexcept {
    if (this != &other) {
        roster_ = other.roster_;
        pairs_ = std::move(other.pairs_);
        other.roster_ = RosterState{};
        other.pairs_ = {};
    }
    return *this;
}

bool GroupState::initialize(const PublicKey& admin, std::uint64_t group_id, const PublicKey& own,
                            std::uint32_t minimum_version) {
    return roster_.initialize(admin, group_id, own, minimum_version);
}

Outcome GroupState::offer(const std::uint8_t* bytes, std::size_t size, const RosterCrypto& crypto) {
    const Outcome o = roster_.offer(bytes, size, crypto);
    if (o == Outcome::accepted || o == Outcome::accepted_removed_self) apply_roster();
    return o;
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

PairOutcome GroupState::pair_start(const PublicKey& peer, const Ephemeral& eph, const BootToken& boot,
                                   std::uint64_t now_ms, const RosterSigner& signer, RandomSource& rng, PairStart& out) {
    if (roster_.state() != State::active) return PairOutcome::roster_not_active;
    PairRecord* r = record(peer, true);
    return r == nullptr ? PairOutcome::no_pair : r->start(roster_, eph, boot, now_ms, signer, rng, out);
}

PairOutcome GroupState::pair_on_start(const PairStart& start, const Ephemeral& eph, const BootToken& boot,
                                      std::uint64_t now_ms, const RosterCrypto& crypto, const RosterSigner& signer,
                                      RandomSource& rng, PairReply& out) {
    if (roster_.state() != State::active) return PairOutcome::roster_not_active;
    PublicKey peer{};
    if (!roster_.key_for(start.initiator, peer)) return PairOutcome::no_pair;
    PairRecord* r = record(peer, true);
    return r == nullptr ? PairOutcome::no_pair : r->on_start(roster_, start, eph, boot, now_ms, crypto, signer, rng, out);
}

PairOutcome GroupState::pair_on_reply(const PublicKey& peer, const PairReply& reply, std::uint64_t now_ms,
                                      const RosterCrypto& crypto, const RosterSigner& signer, PairConfirm& out,
                                      std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    PairRecord* r = record(peer, false);
    return r == nullptr ? PairOutcome::no_pair : r->on_reply(roster_, reply, now_ms, crypto, signer, out, verified);
}

PairOutcome GroupState::pair_on_confirm(const PublicKey& peer, const PairConfirm& confirm, std::uint64_t now_ms,
                                        const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    PairRecord* r = record(peer, false);
    return r == nullptr ? PairOutcome::no_pair : r->on_confirm(roster_, confirm, now_ms, crypto, verified);
}

PairOutcome GroupState::pair_activation_possible(const PublicKey& peer, std::uint32_t epoch) {
    PairRecord* r = record(peer, false);
    return r == nullptr ? PairOutcome::no_pair : r->activation_possible(roster_, epoch);
}

PairOutcome GroupState::pair_commit(const PublicKey& peer, std::uint32_t epoch) {
    PairRecord* r = record(peer, false);
    return r == nullptr ? PairOutcome::no_pair : r->commit(roster_, epoch);
}

void GroupState::pair_cancel(const PublicKey& peer) {
    PairRecord* r = record(peer, false);
    if (r != nullptr && (r->step_ == PairStep::awaiting_reply || r->step_ == PairStep::bound)) r->drop_attempt();
}

void GroupState::restart() {
    for (std::optional<PairRecord>& r : pairs_) {
        if (r) r->restart();
    }
}

std::optional<PairStatus> GroupState::pair_status(const PublicKey& peer) const {
    const PairRecord* r = find(peer);
    return r == nullptr ? std::nullopt : std::optional<PairStatus>(r->status());
}

}  // namespace opentrail::identity::roster
