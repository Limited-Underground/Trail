#include "opentrail/group_roster.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace opentrail::identity::roster {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'T', 'G', 'R'};

bool all_zero(const Digest& digest) {
    return std::all_of(digest.begin(), digest.end(), [](std::uint8_t b) { return b == 0; });
}

// UTF-8 per RFC 3629: no overlong forms, no surrogates, at most U+10FFFF.
// C0 controls, DEL and C1 controls (U+0080..U+009F) are refused (aliases are display text).
bool valid_alias(const std::uint8_t* bytes, std::size_t size) {
    if (size == 0 || size > kMaximumAliasBytes) return false;
    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t b = bytes[i];
        if (b < 0x20 || b == 0x7F) return false;
        if (b < 0x80) { ++i; continue; }
        std::size_t extra = 0;
        std::uint32_t code = 0;
        std::uint32_t minimum = 0;
        if ((b & 0xE0) == 0xC0) { extra = 1; code = b & 0x1FU; minimum = 0x80; }
        else if ((b & 0xF0) == 0xE0) { extra = 2; code = b & 0x0FU; minimum = 0x800; }
        else if ((b & 0xF8) == 0xF0) { extra = 3; code = b & 0x07U; minimum = 0x10000; }
        else return false;
        if (i + extra >= size) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const std::uint8_t c = bytes[i + k];
            if ((c & 0xC0) != 0x80) return false;
            code = (code << 6) | (c & 0x3FU);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
        if (code <= 0x9F) return false;  // C1 controls
        i += extra + 1;
    }
    return true;
}

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24); p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8); p[3] = static_cast<std::uint8_t>(v);
}
void put64(std::uint8_t* p, std::uint64_t v) {
    put32(p, static_cast<std::uint32_t>(v >> 32)); put32(p + 4, static_cast<std::uint32_t>(v));
}
std::uint32_t get32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}
std::uint64_t get64(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(get32(p)) << 32) | get32(p + 4);
}

std::size_t body_size(const Roster& roster) {
    std::size_t size = kHeaderBytes;
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        size += kMemberFixedBytes + roster.members[i].alias_bytes;
    }
    return size;
}

bool same_alias(const Member& a, const Member& b) {
    return a.alias_bytes == b.alias_bytes &&
           std::memcmp(a.alias.data(), b.alias.data(), a.alias_bytes) == 0;
}

const Member* find(const Roster& roster, const PublicKey& key) {
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        if (roster.members[i].key == key) return &roster.members[i];
    }
    return nullptr;
}

bool set_alias(Member& member, std::string_view alias) {
    if (alias.empty() || alias.size() > kMaximumAliasBytes) return false;
    member.alias.fill(0);
    std::memcpy(member.alias.data(), alias.data(), alias.size());
    member.alias_bytes = static_cast<std::uint8_t>(alias.size());
    return valid_alias(member.alias.data(), member.alias_bytes);
}

// Common start of every successor: version + 1, chained to the digest of the supplied
// signed roster (its own admin's signature is checked, so it is a real signed record).
bool successor(const Roster& current, const RosterCrypto& crypto, Roster& out) {
    Digest current_digest{};
    if (validate(current) != FormatError::none ||
        current.version == std::numeric_limits<std::uint32_t>::max() ||
        !verify_signature(current, current.members[0].key, crypto) ||
        !roster_digest(current, crypto, current_digest)) {
        return false;
    }
    out = current;
    out.version = current.version + 1;
    out.previous = current_digest;
    out.signature.fill(0);
    return true;
}

}  // namespace

FormatError validate(const Roster& roster) {
    if (roster.group_id == 0) return FormatError::group_id;
    if (roster.version == 0) return FormatError::version;
    if ((roster.version == 1) != all_zero(roster.previous)) return FormatError::previous_digest;
    if (roster.location_rule != LocationRule::off && roster.location_rule != LocationRule::preferred &&
        roster.location_rule != LocationRule::required) {
        return FormatError::location_rule;
    }
    if (roster.member_count == 0 || roster.member_count > kMaximumMembers ||
        (roster.version == 1 && roster.member_count != 1)) {
        return FormatError::member_count;
    }
    std::uint32_t previous_added = 0;
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        const Member& m = roster.members[i];
        const Role expected = i == 0 ? Role::admin : Role::member;
        if (m.role != expected) return FormatError::role;
        if ((i == 0 && m.added_in != 1) || m.added_in <= previous_added || m.added_in > roster.version) {
            return FormatError::added_in;
        }
        previous_added = m.added_in;
        if (!valid_alias(m.alias.data(), m.alias_bytes)) return FormatError::alias;
        for (std::size_t j = 0; j < i; ++j) {
            if (roster.members[j].key == m.key) return FormatError::duplicate_identity;
            if (same_alias(roster.members[j], m)) return FormatError::duplicate_alias;
        }
    }
    return FormatError::none;
}

std::size_t encode_body(const Roster& roster, std::uint8_t* out, std::size_t capacity) {
    if (validate(roster) != FormatError::none) return 0;
    const std::size_t size = body_size(roster);
    if (out == nullptr || capacity < size) return 0;
    std::uint8_t* p = out;
    std::memcpy(p, kMagic.data(), kMagic.size()); p += kMagic.size();
    *p++ = kFormatVersion;
    put64(p, roster.group_id); p += 8;
    put32(p, roster.version); p += 4;
    std::memcpy(p, roster.previous.data(), kDigestBytes); p += kDigestBytes;
    *p++ = static_cast<std::uint8_t>(roster.location_rule);
    *p++ = roster.member_count;
    for (std::size_t i = 0; i < roster.member_count; ++i) {
        const Member& m = roster.members[i];
        *p++ = static_cast<std::uint8_t>(m.role);
        put32(p, m.added_in); p += 4;
        std::memcpy(p, m.key.data(), kPublicKeyBytes); p += kPublicKeyBytes;
        *p++ = m.alias_bytes;
        std::memcpy(p, m.alias.data(), m.alias_bytes); p += m.alias_bytes;
    }
    return size;
}

std::size_t encode(const Roster& roster, std::uint8_t* out, std::size_t capacity) {
    if (validate(roster) != FormatError::none || capacity < body_size(roster) + kSignatureBytes) return 0;
    const std::size_t body = encode_body(roster, out, capacity);
    if (body == 0) return 0;
    std::memcpy(out + body, roster.signature.data(), kSignatureBytes);
    return body + kSignatureBytes;
}

FormatError decode(const std::uint8_t* bytes, std::size_t size, Roster& out) {
    if (bytes == nullptr || size < kHeaderBytes + kSignatureBytes || size > kMaximumRosterBytes) {
        return FormatError::length;
    }
    Roster r{};
    const std::uint8_t* p = bytes;
    const std::uint8_t* const body_end = bytes + size - kSignatureBytes;
    if (std::memcmp(p, kMagic.data(), kMagic.size()) != 0) return FormatError::magic;
    p += kMagic.size();
    if (*p++ != kFormatVersion) return FormatError::format_version;
    r.group_id = get64(p); p += 8;
    r.version = get32(p); p += 4;
    std::memcpy(r.previous.data(), p, kDigestBytes); p += kDigestBytes;
    const std::uint8_t rule = *p++;
    if (rule > static_cast<std::uint8_t>(LocationRule::required)) return FormatError::location_rule;
    r.location_rule = static_cast<LocationRule>(rule);
    r.member_count = *p++;
    if (r.member_count == 0 || r.member_count > kMaximumMembers) return FormatError::member_count;
    for (std::size_t i = 0; i < r.member_count; ++i) {
        if (static_cast<std::size_t>(body_end - p) < kMemberFixedBytes) return FormatError::length;
        Member& m = r.members[i];
        const std::uint8_t role = *p++;
        if (role != static_cast<std::uint8_t>(Role::admin) && role != static_cast<std::uint8_t>(Role::member)) {
            return FormatError::role;
        }
        m.role = static_cast<Role>(role);
        m.added_in = get32(p); p += 4;
        std::memcpy(m.key.data(), p, kPublicKeyBytes); p += kPublicKeyBytes;
        m.alias_bytes = *p++;
        if (m.alias_bytes == 0 || m.alias_bytes > kMaximumAliasBytes) return FormatError::alias;
        if (static_cast<std::size_t>(body_end - p) < m.alias_bytes) return FormatError::length;
        std::memcpy(m.alias.data(), p, m.alias_bytes); p += m.alias_bytes;
    }
    if (p != body_end) return FormatError::length;
    std::memcpy(r.signature.data(), body_end, kSignatureBytes);
    const FormatError error = validate(r);
    if (error != FormatError::none) return error;
    out = r;
    return FormatError::none;
}

bool roster_digest(const Roster& roster, const RosterCrypto& crypto, Digest& out) {
    std::array<std::uint8_t, kDigestDomain.size() + kMaximumRosterBytes> input{};
    std::memcpy(input.data(), kDigestDomain.data(), kDigestDomain.size());
    const std::size_t size = encode(roster, input.data() + kDigestDomain.size(), kMaximumRosterBytes);
    if (size == 0) return false;
    Digest result{};
    if (!crypto.digest(input.data(), kDigestDomain.size() + size, result) || all_zero(result)) return false;
    out = result;
    return true;
}

DigestPrefix digest_prefix(const Digest& digest) {
    DigestPrefix prefix{};
    std::copy_n(digest.begin(), kDigestPrefixBytes, prefix.begin());
    return prefix;
}

bool group_name(const Roster& roster, const RosterCrypto& crypto, GroupName& out) {
    if (validate(roster) != FormatError::none) return false;
    GroupName name{};
    if (!crypto.fingerprint(roster.members[0].key, name.admin)) return false;
    name.group_id = roster.group_id;
    out = name;
    return true;
}

namespace {
std::size_t signing_input(const Roster& roster, std::array<std::uint8_t, kSignatureDomain.size() + kMaximumBodyBytes>& input) {
    std::memcpy(input.data(), kSignatureDomain.data(), kSignatureDomain.size());
    const std::size_t body = encode_body(roster, input.data() + kSignatureDomain.size(), kMaximumBodyBytes);
    return body == 0 ? 0 : kSignatureDomain.size() + body;
}
}  // namespace

bool verify_signature(const Roster& roster, const PublicKey& admin, const RosterCrypto& crypto) {
    std::array<std::uint8_t, kSignatureDomain.size() + kMaximumBodyBytes> input{};
    const std::size_t size = signing_input(roster, input);
    return size != 0 && crypto.verify(roster.signature, input.data(), size, admin);
}

bool create_first(std::uint64_t group_id, const PublicKey& admin, std::string_view alias,
                  LocationRule rule, Roster& out) {
    Roster r{};
    r.group_id = group_id;
    r.version = 1;
    r.location_rule = rule;
    r.member_count = 1;
    r.members[0].role = Role::admin;
    r.members[0].added_in = 1;
    r.members[0].key = admin;
    if (!set_alias(r.members[0], alias) || validate(r) != FormatError::none) return false;
    out = r;
    return true;
}

bool add_member(const Roster& current, const RosterCrypto& crypto, const PublicKey& key,
                std::string_view alias, Roster& out) {
    Roster next{};
    if (!successor(current, crypto, next) || next.member_count >= kMaximumMembers) return false;
    Member& m = next.members[next.member_count];
    m = Member{};
    m.role = Role::member;
    m.added_in = next.version;
    m.key = key;
    if (!set_alias(m, alias)) return false;
    ++next.member_count;
    if (validate(next) != FormatError::none) return false;
    out = next;
    return true;
}

bool remove_member(const Roster& current, const RosterCrypto& crypto, const PublicKey& key,
                   Roster& out) {
    Roster next{};
    if (!successor(current, crypto, next)) return false;
    for (std::size_t i = 1; i < next.member_count; ++i) {  // member 0 (admin) is never removed
        if (next.members[i].key == key) {
            for (std::size_t j = i; j + 1 < next.member_count; ++j) next.members[j] = next.members[j + 1];
            --next.member_count;
            next.members[next.member_count] = Member{};
            if (validate(next) != FormatError::none) return false;
            out = next;
            return true;
        }
    }
    return false;
}

bool change_location_rule(const Roster& current, const RosterCrypto& crypto,
                          LocationRule rule, Roster& out) {
    Roster next{};
    if (!successor(current, crypto, next)) return false;
    next.location_rule = rule;
    if (validate(next) != FormatError::none) return false;
    out = next;
    return true;
}

bool sign(Roster& roster, const RosterSigner& signer) {
    if (validate(roster) != FormatError::none || roster.members[0].key != signer.public_key()) return false;
    std::array<std::uint8_t, kSignatureDomain.size() + kMaximumBodyBytes> input{};
    const std::size_t size = signing_input(roster, input);
    Signature signature{};
    if (size == 0 || !signer.sign(input.data(), size, signature)) return false;
    roster.signature = signature;
    return true;
}

// ---------------------------------------------------------------- RosterState

bool RosterState::initialize(const PublicKey& admin, std::uint64_t group_id, const PublicKey& own,
                             std::uint32_t minimum_version) {
    if (state_ != State::uninitialized || group_id == 0 || minimum_version == 0) return false;
    admin_ = admin;
    own_ = own;
    group_id_ = group_id;
    minimum_version_ = minimum_version;
    state_ = State::awaiting_roster;
    return true;
}

Outcome RosterState::block(ConflictKind kind, const Digest& offered) {
    conflict_ = kind;
    conflict_digest_ = offered;
    state_ = State::conflict;
    return Outcome::conflict;
}

// Across any number of versions: a member that was added at or before the held version
// and is still listed must appear in the held roster with the same identity, role and
// added-in version. (Each addition is stamped with the version that adds it, so anything
// else is a fork.) A member added after the held version may be new, or a held member
// that was removed and added again; both are consistent.
bool RosterState::consistent_history(const Roster& next) const {
    for (std::size_t i = 0; i < next.member_count; ++i) {
        const Member& m = next.members[i];
        if (m.added_in > held_.version) continue;
        const Member* held = find(held_, m.key);
        if (held == nullptr || held->added_in != m.added_in || held->role != m.role) return false;
    }
    return true;
}

bool RosterState::contradicts_known(std::uint32_t version, const Digest& digest) const {
    for (const Known& k : known_) {
        if (k.version != 0 && k.version == version && k.digest != digest) return true;
    }
    return false;
}

void RosterState::remember(std::uint32_t version, const Digest& digest) {
    for (const Known& k : known_) {
        if (k.version == version && k.digest == digest) return;
    }
    known_[known_next_] = Known{version, digest};
    known_next_ = (known_next_ + 1) % known_.size();
}

Outcome RosterState::offer(const std::uint8_t* bytes, std::size_t size, const RosterCrypto& crypto) {
    switch (state_) {
        case State::uninitialized: return Outcome::not_initialized;
        case State::left:
        case State::distrusted: return Outcome::not_member;
        case State::conflict: return Outcome::blocked_by_conflict;
        case State::awaiting_roster:
        case State::active: break;
    }
    Roster next{};
    if (decode(bytes, size, next) != FormatError::none) return Outcome::rejected_malformed;
    // Authority first: nothing unsigned by the pinned admin can change state or cause a conflict.
    if (!verify_signature(next, admin_, crypto)) return Outcome::rejected_signature;
    if (next.group_id != group_id_ || next.members[0].key != admin_) return Outcome::rejected_group;
    // Fingerprints are recomputed from the listed keys (the design carries none).
    std::array<Fingerprint, kMaximumMembers> fingerprints{};
    for (std::size_t i = 0; i < next.member_count; ++i) {
        if (!crypto.fingerprint(next.members[i].key, fingerprints[i])) return Outcome::crypto_failure;
        for (std::size_t j = 0; j < i; ++j) {
            if (fingerprints[j] == fingerprints[i]) return Outcome::rejected_identity;
        }
    }
    Digest digest{};
    if (!roster_digest(next, crypto, digest)) return Outcome::crypto_failure;
    for (const Digest& resolved : resolved_) {
        if (resolved == digest) return Outcome::resolved_replay;
    }

    if (!has_roster_) {
        if (next.version < minimum_version_) return Outcome::stale;
    } else {
        if (next.version < held_.version) {
            // An older roster can still prove a fork against versions this board has seen.
            if (contradicts_known(next.version, digest) ||
                (next.version > 1 && contradicts_known(next.version - 1, next.previous))) {
                return block(ConflictKind::known_version_mismatch, digest);
            }
            return Outcome::stale;
        }
        if (next.version == held_.version) {
            return digest == held_digest_ ? Outcome::duplicate
                                          : block(ConflictKind::same_version_different_roster, digest);
        }
        if (next.version == held_.version + 1 && next.previous != held_digest_) {
            return block(ConflictKind::chain_break, digest);
        }
        if (!consistent_history(next)) return block(ConflictKind::history_mismatch, digest);
    }

    const bool listed = find(next, own_) != nullptr;
    if (!has_roster_ && !listed) return Outcome::rejected_not_listed;
    held_ = next;
    held_digest_ = digest;
    held_fingerprints_ = fingerprints;
    {  // keep acceptances only for members still listed with the same added-in version
        std::size_t kept = 0;
        for (std::size_t i = 0; i < accepted_count_; ++i) {
            const Member* m = find(held_, accepted_[i].key);
            if (m != nullptr && m->added_in == accepted_[i].added_in) accepted_[kept++] = accepted_[i];
        }
        for (std::size_t i = kept; i < accepted_.size(); ++i) accepted_[i] = Accepted{};
        accepted_count_ = kept;
    }
    has_roster_ = true;
    remember(next.version, digest);
    if (next.version > 1) remember(next.version - 1, next.previous);
    if (!listed) {
        state_ = State::left;
        return Outcome::accepted_removed_self;
    }
    state_ = State::active;
    return Outcome::accepted;
}

bool RosterState::keep_current() {
    if (state_ != State::conflict) return false;
    resolved_[resolved_next_] = conflict_digest_;
    resolved_next_ = (resolved_next_ + 1) % resolved_.size();
    conflict_ = ConflictKind::none;
    conflict_digest_ = Digest{};
    state_ = State::active;
    return true;
}

bool RosterState::accept_member(const PublicKey& key) {
    if (state_ != State::active || key == own_) return false;
    const Member* peer = find(held_, key);
    const Member* self = find(held_, own_);
    if (peer == nullptr || self == nullptr || peer->added_in <= self->added_in) return false;
    if (accepted(key)) return true;
    if (accepted_count_ >= accepted_.size()) return false;
    accepted_[accepted_count_++] = Accepted{key, peer->added_in};
    return true;
}

bool RosterState::accepted(const PublicKey& key) const {
    const Member* peer = find(held_, key);
    if (peer == nullptr) return false;
    for (std::size_t i = 0; i < accepted_count_; ++i) {
        if (accepted_[i].key == key && accepted_[i].added_in == peer->added_in) return true;
    }
    return false;
}

bool RosterState::key_for(const Fingerprint& fingerprint, PublicKey& out) const {
    if (!has_roster_) return false;
    for (std::size_t i = 0; i < held_.member_count; ++i) {
        if (held_fingerprints_[i] == fingerprint) {
            out = held_.members[i].key;
            return true;
        }
    }
    return false;
}

void RosterState::stop_trusting() {
    if (state_ == State::uninitialized) return;
    conflict_ = ConflictKind::none;
    state_ = State::distrusted;
}

}  // namespace opentrail::identity::roster
