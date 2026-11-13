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

bool valid_lifetime(std::uint32_t lifetime_ms) {
    return lifetime_ms > 0 && lifetime_ms <= kMaximumPairLifetimeMs;
}

const Member* listed(const Roster& roster, const PublicKey& key) {
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        if (roster.members[i].key == key) return &roster.members[i];
    }
    return nullptr;
}

std::size_t index_of(const Roster& roster, const PublicKey& key) {
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        if (roster.members[i].key == key) return i;
    }
    return kMaximumMembers;
}

// Purpose against the durable record. A rekey needs an earlier attempt with this peer
// (committed, or reserved and interrupted: the peer may have committed it). The initiator
// starts a first pairing (introduce) until it has committed one; a responder answers an
// introduce in any case, since its authority still requires acceptance and the role order.
// This lets two boards that disagree after an interruption always reach a fresh epoch.
bool rekey_fits(const PairLedger& ledger) {
    return ledger.committed_epoch() != 0 || ledger.highest_epoch() != 0;
}
bool initiator_purpose_fits(PairPurpose purpose, const PairLedger& ledger) {
    return purpose == PairPurpose::introduce ? ledger.committed_epoch() == 0 : rekey_fits(ledger);
}
bool responder_purpose_fits(PairPurpose purpose, const PairLedger& ledger) {
    return purpose == PairPurpose::introduce || rekey_fits(ledger);
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

bool local_start_wins(const Fingerprint& local, const Fingerprint& peer) {
    return std::lexicographical_compare(local.begin(), local.end(), peer.begin(), peer.end());
}

// ------------------------------------------------------------------ PairLedger

std::uint32_t PairLedger::next_epoch() const {
    return highest_ == std::numeric_limits<std::uint32_t>::max() ? 0 : highest_ + 1;
}

bool PairLedger::acceptable(std::uint32_t epoch) const {
    return epoch != 0 && epoch > highest_;
}

bool PairLedger::reserve(std::uint32_t epoch) {
    if (!acceptable(epoch)) return false;
    before_attempt_ = highest_;
    highest_ = epoch;   // stored and read back before anything is sent
    attempt_ = epoch;
    step_ = PairStep::prepared;
    return true;
}

bool PairLedger::activation_possible(std::uint32_t epoch) {
    if (step_ != PairStep::prepared || epoch != attempt_) return false;
    step_ = PairStep::activation_possible;
    return true;
}

bool PairLedger::commit(std::uint32_t epoch) {
    if (step_ != PairStep::activation_possible || epoch != attempt_) return false;
    committed_ = epoch;
    step_ = PairStep::committed;
    return true;
}

void PairLedger::restart() {
    if (step_ == PairStep::prepared || step_ == PairStep::activation_possible) {
        attempt_ = 0;               // retired, never resumed; its epoch stays used
        step_ = PairStep::none;
    }
}

bool PairLedger::release(std::uint32_t epoch) {
    if (step_ != PairStep::prepared || attempt_ != epoch || highest_ != epoch) return false;
    highest_ = before_attempt_;
    attempt_ = 0;
    step_ = PairStep::none;
    return true;
}

// ------------------------------------------------------------------ authority

bool RosterState::pair_authority(const PublicKey& peer, PairPurpose purpose,
                                 std::optional<PairAuthority>& out) const {
    if (state_ != State::active || !has_roster_ || peer == own_) return false;
    if (purpose != PairPurpose::introduce && purpose != PairPurpose::rekey) return false;
    const Member* self = listed(held_, own_);
    const Member* other = listed(held_, peer);
    if (self == nullptr || other == nullptr) return false;
    if (purpose == PairPurpose::introduce) {
        // Introduced pairs are between two non-admin members; each member's pair with the
        // admin is the in-person first pairing.
        if (self->role == Role::admin || other->role == Role::admin) return false;
        if (other->added_in > self->added_in && !accepted(peer)) return false;
    }
    PairAuthority a;
    a.purpose_ = purpose;
    a.group_.admin = held_fingerprints_[0];
    a.group_.group_id = held_.group_id;
    a.roster_version_ = held_.version;
    a.roster_digest_ = held_digest_;
    a.local_key_ = own_;
    a.peer_key_ = peer;
    a.local_fp_ = held_fingerprints_[index_of(held_, own_)];
    a.peer_fp_ = held_fingerprints_[index_of(held_, peer)];
    a.introduce_role_ = self->added_in > other->added_in ? PairRole::initiator : PairRole::responder;
    out.emplace(a);
    return true;
}

// ------------------------------------------------------------------ initiator

PairOutcome PairInitiator::start(const PairAuthority& authority, PairLedger& ledger, const Ephemeral& eph,
                                 const Challenge& challenge, const BootToken& boot, std::uint32_t lifetime_ms,
                                 std::uint64_t now_ms, const RosterSigner& signer, PairStart& out) {
    if (signer.public_key() != authority.local_key()) return PairOutcome::not_for_this_pair;
    if (authority.purpose() == PairPurpose::introduce && authority.introduce_role() != PairRole::initiator) {
        return PairOutcome::wrong_role;
    }
    if (!initiator_purpose_fits(authority.purpose(), ledger)) return PairOutcome::wrong_purpose;
    if (!valid_lifetime(lifetime_ms)) return PairOutcome::bad_lifetime;
    if (all_zero(eph) || all_zero(challenge) || all_zero(boot)) return PairOutcome::bad_fresh_values;
    const std::uint32_t epoch = ledger.next_epoch();
    if (epoch == 0) return PairOutcome::bad_epoch;
    PairStart s{};
    s.purpose = authority.purpose();
    s.group = authority.group();
    s.roster_version = authority.roster_version();
    s.roster_digest = authority.roster_digest();
    s.epoch = epoch;
    s.initiator = authority.local_fingerprint();
    s.responder = authority.peer_fingerprint();
    s.lifetime_ms = lifetime_ms;
    s.initiator_ephemeral = eph;
    s.initiator_challenge = challenge;
    s.initiator_boot = boot;
    std::array<std::uint8_t, kStartBytes> bytes{};
    const std::size_t n = start_bytes(s, bytes.data(), bytes.size());
    if (n == 0) return PairOutcome::crypto_failure;
    // Reserve (durably) before the start can leave this board.
    if (!ledger.reserve(epoch)) return PairOutcome::ledger_busy;
    if (!signer.sign(bytes.data(), n, s.signature)) return PairOutcome::crypto_failure;
    start_ = s;
    peer_key_ = authority.peer_key();
    deadline_ = now_ms + lifetime_ms;
    state_ = State::awaiting_reply;
    out = s;
    return PairOutcome::ok;
}

PairOutcome PairInitiator::on_reply(const PairReply& reply, std::uint64_t now_ms, const RosterCrypto& crypto,
                                    const RosterSigner& signer, PairConfirm& out,
                                    std::optional<VerifiedPairBinding>& verified) {
    if (state_ != State::awaiting_reply) return PairOutcome::wrong_state;
    if (now_ms > deadline_) {
        state_ = State::done;
        return PairOutcome::expired;
    }
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
    verified.emplace(VerifiedPairBinding(PairRole::initiator, start_, reply, confirm));
    state_ = State::done;
    out = confirm;
    return PairOutcome::ok;
}

bool PairInitiator::yield_to_peer(PairLedger& ledger) {
    if (state_ != State::awaiting_reply || !ledger.release(start_.epoch)) return false;
    state_ = State::done;
    return true;
}

// ------------------------------------------------------------------ responder

PairOutcome PairResponder::on_start(const PairAuthority& authority, PairLedger& ledger, const PairStart& start,
                                    const Ephemeral& eph, const Challenge& challenge, const BootToken& boot,
                                    std::uint64_t now_ms, const RosterCrypto& crypto, const RosterSigner& signer,
                                    PairReply& out) {
    if (signer.public_key() != authority.local_key()) return PairOutcome::not_for_this_pair;
    if (start.purpose != authority.purpose() || !same_group(start.group, authority.group()) ||
        start.initiator != authority.peer_fingerprint() || start.responder != authority.local_fingerprint()) {
        return PairOutcome::not_for_this_pair;
    }
    if (start.roster_version > authority.roster_version()) return PairOutcome::peer_roster_newer;
    if (start.roster_version < authority.roster_version()) return PairOutcome::peer_roster_older;
    if (start.roster_digest != authority.roster_digest()) return PairOutcome::peer_roster_differs;
    if (!valid_lifetime(start.lifetime_ms)) return PairOutcome::bad_lifetime;
    if (all_zero(start.initiator_ephemeral) || all_zero(start.initiator_challenge) ||
        all_zero(start.initiator_boot) || all_zero(eph) || all_zero(challenge) || all_zero(boot) ||
        challenge == start.initiator_challenge || eph == start.initiator_ephemeral) {
        return PairOutcome::bad_fresh_values;
    }
    std::array<std::uint8_t, kStartBytes> sbytes{};
    const std::size_t sn = start_bytes(start, sbytes.data(), sbytes.size());
    if (sn == 0) return PairOutcome::crypto_failure;
    // Only a start signed by the listed peer's identity can reserve an epoch or use the
    // rate limit; anything else changes nothing.
    if (!crypto.verify(start.signature, sbytes.data(), sn, authority.peer_key())) return PairOutcome::bad_signature;
    if (start.purpose == PairPurpose::introduce && authority.introduce_role() != PairRole::responder) {
        return PairOutcome::wrong_role;
    }
    if (!responder_purpose_fits(start.purpose, ledger)) return PairOutcome::wrong_purpose;
    if (!ledger.acceptable(start.epoch)) return PairOutcome::bad_epoch;
    if (answered_ && now_ms < last_answer_ms_ + kStartAnswerIntervalMs) return PairOutcome::rate_limited;
    PairReply reply{};
    reply.responder_ephemeral = eph;
    reply.responder_challenge = challenge;
    reply.responder_boot = boot;
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(start, reply, bytes.data(), bytes.size());
    if (n == 0) return PairOutcome::crypto_failure;
    if (!ledger.reserve(start.epoch)) return PairOutcome::ledger_busy;
    if (!signer.sign(bytes.data(), n, reply.signature)) return PairOutcome::crypto_failure;
    answered_ = true;
    last_answer_ms_ = now_ms;
    start_ = start;
    reply_ = reply;
    peer_key_ = authority.peer_key();
    deadline_ = now_ms + start.lifetime_ms;
    state_ = State::awaiting_confirm;
    out = reply;
    return PairOutcome::ok;
}

PairOutcome PairResponder::on_confirm(const PairConfirm& confirm, std::uint64_t now_ms,
                                      const RosterCrypto& crypto, std::optional<VerifiedPairBinding>& verified) {
    if (state_ != State::awaiting_confirm) return PairOutcome::wrong_state;
    if (now_ms > deadline_) {
        state_ = State::done;
        return PairOutcome::expired;
    }
    std::array<std::uint8_t, kBindingBytes> bytes{};
    const std::size_t n = binding_bytes(start_, reply_, bytes.data(), bytes.size());
    if (n == 0) return PairOutcome::crypto_failure;
    if (!crypto.verify(confirm.signature, bytes.data(), n, peer_key_)) return PairOutcome::bad_signature;
    verified.emplace(VerifiedPairBinding(PairRole::responder, start_, reply_, confirm));
    state_ = State::done;
    return PairOutcome::ok;
}

}  // namespace opentrail::identity::roster
