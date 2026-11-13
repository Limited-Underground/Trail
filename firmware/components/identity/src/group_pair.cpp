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

// ------------------------------------------------------------------ owner basics

std::optional<PairOwner> PairOwner::create(const RosterState& roster, const PublicKey& peer) {
    if (roster.state_ != State::active || !roster.has_roster_ || peer == roster.own_) return std::nullopt;
    const std::size_t si = index_of(roster.held_, roster.own_);
    const std::size_t pi = index_of(roster.held_, peer);
    if (si == kMaximumMembers || pi == kMaximumMembers) return std::nullopt;
    const Member& self = roster.held_.members[si];
    const Member& other = roster.held_.members[pi];
    // Introduced pairs only: each member's pair with the admin is the in-person first pairing.
    if (self.role == Role::admin || other.role == Role::admin) return std::nullopt;
    PairOwner owner;
    owner.group_.admin = roster.held_fingerprints_[0];
    owner.group_.group_id = roster.held_.group_id;
    owner.self_key_ = roster.own_;
    owner.peer_key_ = peer;
    owner.self_added_ = self.added_in;
    owner.peer_added_ = other.added_in;
    return std::optional<PairOwner>(std::move(owner));
}

std::uint32_t PairOwner::next_epoch() const {
    return used_ == std::numeric_limits<std::uint32_t>::max() ? 0 : used_ + 1;
}

bool PairOwner::acceptable(std::uint32_t epoch) const {
    const std::uint64_t e = epoch, u = used_;
    return e == u + 1 || e == u + 2;  // D-072: the next number, or catch up by one
}

bool PairOwner::fresh_local(const Ephemeral& eph, const Challenge& challenge) const {
    if (all_zero(eph) || all_zero(challenge)) return false;
    for (std::size_t i = 0; i < kRememberedLocalValues; ++i) {
        if (own_challenges_[i] == challenge || own_ephemerals_[i] == eph) return false;
    }
    return true;
}

void PairOwner::remember_local(const Ephemeral& eph, const Challenge& challenge) {
    own_challenges_[own_next_] = challenge;
    own_ephemerals_[own_next_] = eph;
    own_next_ = (own_next_ + 1) % kRememberedLocalValues;
}

void PairOwner::drop_attempt() {
    step_ = PairStep::none;
    start_ = PairStart{};
    reply_ = PairReply{};
    confirm_ = PairConfirm{};
    attempt_time_ = deadline_ = 0;
}

bool PairOwner::attempt_expired(std::uint64_t now_ms) {
    if (now_ms < attempt_time_ || now_ms > deadline_) {  // clock regression fails closed
        drop_attempt();
        return true;
    }
    return false;
}

// Rechecks the live roster: active, both members still listed with the same added-in
// versions (the incarnation this owner was made for), same group.
PairOutcome PairOwner::live(const RosterState& roster, Live& out) {
    if (stale_) return PairOutcome::stale_owner;
    if (roster.state_ != State::active || !roster.has_roster_ || roster.own_ != self_key_) {
        return PairOutcome::roster_not_active;
    }
    const std::size_t si = index_of(roster.held_, self_key_);
    const std::size_t pi = index_of(roster.held_, peer_key_);
    if (pi == kMaximumMembers || si == kMaximumMembers ||
        roster.held_.members[pi].added_in != peer_added_ || roster.held_.members[si].added_in != self_added_ ||
        roster.held_.group_id != group_.group_id || roster.held_fingerprints_[0] != group_.admin) {
        stale_ = true;  // removed, or re-added as a new incarnation: this owner is finished
        drop_attempt();
        return PairOutcome::stale_owner;
    }
    out.self = &roster.held_.members[si];
    out.peer = &roster.held_.members[pi];
    out.self_fp = roster.held_fingerprints_[si];
    out.peer_fp = roster.held_fingerprints_[pi];
    return PairOutcome::ok;
}

// ------------------------------------------------------------------ initiator side

PairOutcome PairOwner::start(const RosterState& roster, const Ephemeral& eph, const Challenge& challenge,
                             const BootToken& boot, std::uint64_t now_ms, const RosterSigner& signer,
                             PairStart& out) {
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (signer.public_key() != self_key_) return PairOutcome::wrong_signer;
    if (step_ == PairStep::activation_possible) return PairOutcome::wrong_state;
    const PairPurpose purpose = next_purpose();
    if (purpose == PairPurpose::introduce && l.self->added_in < l.peer->added_in) return PairOutcome::wrong_role;
    const std::uint32_t epoch = next_epoch();
    if (epoch == 0) return PairOutcome::epoch_exhausted;
    if (all_zero(boot) || !fresh_local(eph, challenge)) return PairOutcome::bad_fresh_values;
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
    s.initiator_challenge = challenge;
    s.initiator_boot = boot;
    std::array<std::uint8_t, kStartBytes> bytes{};
    const std::size_t n = start_bytes(s, bytes.data(), bytes.size());
    if (n == 0 || !signer.sign(bytes.data(), n, s.signature)) return PairOutcome::crypto_failure;
    // A new start replaces any unfinished attempt; nothing was used by it.
    drop_attempt();
    role_ = PairRole::initiator;
    start_ = s;
    step_ = PairStep::awaiting_reply;
    attempt_time_ = now_ms;
    deadline_ = deadline_from(now_ms, kPairLifetimeMs);
    attempt_roster_version_ = s.roster_version;
    attempt_roster_digest_ = s.roster_digest;
    remember_local(eph, challenge);
    out = s;
    return PairOutcome::ok;
}

PairOutcome PairOwner::on_reply(const RosterState& roster, const PairReply& reply, std::uint64_t now_ms,
                                const RosterCrypto& crypto, const RosterSigner& signer, PairConfirm& out,
                                std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (step_ != PairStep::awaiting_reply || role_ != PairRole::initiator) return PairOutcome::wrong_state;
    if (roster.held_.version != attempt_roster_version_ || roster.held_digest_ != attempt_roster_digest_) {
        drop_attempt();
        return PairOutcome::roster_changed;
    }
    if (attempt_expired(now_ms)) return PairOutcome::expired;
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
    verified.emplace(VerifiedPairBinding(PairRole::initiator, start_, reply_, confirm_));
    out = confirm;
    return PairOutcome::ok;
}

// ------------------------------------------------------------------ responder side

PairOutcome PairOwner::on_start(const RosterState& roster, const PairStart& start, const Ephemeral& eph,
                                const Challenge& challenge, const BootToken& boot, std::uint64_t now_ms,
                                const RosterCrypto& crypto, const RosterSigner& signer, PairReply& out) {
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
    if (step_ == PairStep::awaiting_confirm && same_start(start, start_) && !attempt_expired(now_ms)) {
        out = reply_;
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
        all_zero(boot) || !fresh_local(eph, challenge) || challenge == start.initiator_challenge ||
        eph == start.initiator_ephemeral) {
        return PairOutcome::bad_fresh_values;
    }
    // Crossing starts: decided here, atomically, on the verified peer start.
    if (step_ == PairStep::awaiting_reply && start_wins(start_, start)) return PairOutcome::local_attempt_wins;
    if (step_ == PairStep::activation_possible) return PairOutcome::wrong_state;
    // Budget of distinct answers per window.
    std::size_t recent = 0;
    for (std::size_t i = 0; i < answer_count_; ++i) {
        if (now_ms >= answer_times_[i] && now_ms - answer_times_[i] < kAnswerWindowMs) ++recent;
    }
    if (recent >= kAnswersPerWindow) return PairOutcome::rate_limited;
    PairReply reply{};
    reply.responder_ephemeral = eph;
    reply.responder_challenge = challenge;
    reply.responder_boot = boot;
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(start, reply, bytes.data(), bytes.size());
    if (n == 0 || !signer.sign(bytes.data(), n, reply.signature)) return PairOutcome::crypto_failure;
    // Only now does anything change: the earlier unfinished attempt (own or answered) is
    // replaced; no epoch is used until the confirm verifies.
    drop_attempt();
    role_ = PairRole::responder;
    start_ = start;
    reply_ = reply;
    step_ = PairStep::awaiting_confirm;
    attempt_time_ = now_ms;
    deadline_ = deadline_from(now_ms, start.lifetime_ms);
    attempt_roster_version_ = start.roster_version;
    attempt_roster_digest_ = start.roster_digest;
    history_ = true;  // this board signed a binding over the peer's verified start
    answered_[answered_next_] = start.signature;
    answered_next_ = (answered_next_ + 1) % kRememberedStarts;
    answer_times_[answer_next_] = now_ms;
    answer_next_ = (answer_next_ + 1) % kAnswersPerWindow;
    answer_count_ = std::min(answer_count_ + 1, kAnswersPerWindow);
    remember_local(eph, challenge);
    out = reply;
    return PairOutcome::ok;
}

PairOutcome PairOwner::on_confirm(const RosterState& roster, const PairConfirm& confirm, std::uint64_t now_ms,
                                  const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified) {
    verified.reset();
    Live l;
    if (const PairOutcome o = live(roster, l); o != PairOutcome::ok) return o;
    if (step_ != PairStep::awaiting_confirm || role_ != PairRole::responder) return PairOutcome::wrong_state;
    if (roster.held_.version != attempt_roster_version_ || roster.held_digest_ != attempt_roster_digest_) {
        drop_attempt();
        return PairOutcome::roster_changed;
    }
    if (attempt_expired(now_ms)) return PairOutcome::expired;
    if (start_.purpose == PairPurpose::introduce && !roster.accepted(peer_key_)) {
        drop_attempt();
        return PairOutcome::no_consent;
    }
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(start_, reply_, bytes.data(), bytes.size());
    if (n == 0) return PairOutcome::crypto_failure;
    if (!crypto.verify(confirm.signature, bytes.data(), n, peer_key_)) return PairOutcome::bad_signature;
    confirm_ = confirm;
    step_ = PairStep::bound;
    used_ = start_.epoch;
    verified.emplace(VerifiedPairBinding(PairRole::responder, start_, reply_, confirm_));
    return PairOutcome::ok;
}

// ------------------------------------------------------------------ durable steps

bool PairOwner::activation_possible() {
    if (stale_ || step_ != PairStep::bound || start_.epoch != used_) return false;
    step_ = PairStep::activation_possible;
    return true;
}

bool PairOwner::commit() {
    if (stale_ || step_ != PairStep::activation_possible) return false;
    committed_ = start_.epoch;
    step_ = PairStep::committed;
    return true;
}

void PairOwner::restart() {
    if (step_ != PairStep::committed) drop_attempt();  // retired, never resumed
    answered_ = {};
    answered_next_ = 0;
    answer_times_ = {};
    answer_count_ = answer_next_ = 0;
}

void PairOwner::cancel() {
    if (step_ == PairStep::awaiting_reply || step_ == PairStep::awaiting_confirm || step_ == PairStep::bound) {
        drop_attempt();
    }
}

}  // namespace opentrail::identity::roster
