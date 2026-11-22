#include "opentrail/group_qr_invite.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace opentrail::identity::roster::qr {
namespace {

constexpr std::array<std::uint8_t, 4> kQrMagic{'O', 'T', 'G', 'Q'};
constexpr std::array<std::uint8_t, 4> kRadioMagic{'O', 'T', 'G', 'J'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kExpiryPolicy = 1;
constexpr std::size_t kDescriptorPrefix = kDescriptorBaseBytes - kSignatureBytes;
constexpr std::size_t kSignatureInputLimit = 320;
constexpr std::size_t kHashInputLimit = 640;
static_assert(kStartBytes == kHeaderBytes + 32 + 4 + 32 + 32 + 32 + 16 + 64);
static_assert(kReplyBytes == kHeaderBytes + 32 + 32 + 32 + 16 + 64);
static_assert(kBindingBytes == kHeaderBytes + 32 + 64);
static_assert(kReceiptBytes == kHeaderBytes + 3 * 32 + 64);
static_assert(kRecoveryChallengeBytes == kHeaderBytes + 2 * 32 + 16 + 64);
static_assert(kRecoveryReplyBytes == kHeaderBytes + 3 * 32 + 2 * 16 + 64);
static_assert(kReplyBytes <= kStartBytes && kReceiptBytes <= kStartBytes &&
              kRecoveryReplyBytes <= kStartBytes && kStartBytes <= kRadioLimit);

bool valid(Side side) { return side == Side::admin || side == Side::joiner; }
Side other(Side side) { return side == Side::joiner ? Side::admin : Side::joiner; }
std::size_t size_of(Kind kind) {
    switch (kind) {
    case Kind::start: return kStartBytes;
    case Kind::reply: return kReplyBytes;
    case Kind::admission: case Kind::confirmed: return kBindingBytes;
    case Kind::prepared: case Kind::committed: case Kind::aborted: return kReceiptBytes;
    case Kind::recovery_challenge: return kRecoveryChallengeBytes;
    case Kind::recovery_prepared: case Kind::recovery_committed: case Kind::recovery_aborted:
        return kRecoveryReplyBytes;
    }
    return 0;
}
bool stage(Kind kind) { return kind == Kind::prepared || kind == Kind::committed || kind == Kind::aborted; }
Kind recovery_kind(Kind stage_kind) {
    return stage_kind == Kind::prepared ? Kind::recovery_prepared :
           stage_kind == Kind::committed ? Kind::recovery_committed : Kind::recovery_aborted;
}
std::string_view domain(Kind kind) {
    switch (kind) {
    case Kind::start: return "OpenTrail QR PairStart v1";
    case Kind::reply: return "OpenTrail QR PairReply v1";
    case Kind::admission: return "OpenTrail QR Admission v1";
    case Kind::confirmed: return "OpenTrail QR Confirmed v1";
    case Kind::prepared: return "OpenTrail QR Prepared v1";
    case Kind::committed: return "OpenTrail QR Committed v1";
    case Kind::aborted: return "OpenTrail QR Aborted v1";
    case Kind::recovery_challenge: return "OpenTrail QR Recovery Challenge v1";
    case Kind::recovery_prepared: return "OpenTrail QR Recovery Prepared v1";
    case Kind::recovery_committed: return "OpenTrail QR Recovery Committed v1";
    case Kind::recovery_aborted: return "OpenTrail QR Recovery Aborted v1";
    }
    return {};
}
template<std::size_t N>
bool nonzero(const std::array<std::uint8_t, N>& value) {
    return std::any_of(value.begin(), value.end(), [](std::uint8_t byte) { return byte != 0; });
}
template<std::size_t N>
void put(std::uint8_t* out, const std::array<std::uint8_t, N>& value) {
    std::copy(value.begin(), value.end(), out);
}
template<std::size_t N>
void get(const std::uint8_t* input, std::array<std::uint8_t, N>& value) {
    std::copy_n(input, N, value.begin());
}
void put_int(std::uint8_t* out, std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = bytes; i != 0; --i) {
        out[i - 1] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
}
std::uint64_t get_int(const std::uint8_t* input, std::size_t bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value = (value << 8) | input[i];
    return value;
}
bool valid_label(const Descriptor& value) {
    if (value.label_bytes == 0 || value.label_bytes > value.label.size() ||
        value.label[0] == 0x20 || value.label[value.label_bytes - 1] == 0x20 ||
        std::any_of(value.label.begin() + value.label_bytes, value.label.end(),
                    [](std::uint8_t byte) { return byte != 0; })) return false;
    std::size_t offset = 0, units = 0;
    while (offset < value.label_bytes) {
        const auto first = value.label[offset++];
        std::uint32_t point = first, minimum = 0;
        unsigned remaining = 0;
        if (first >= 0xc2 && first <= 0xdf) { point = first & 0x1f; minimum = 0x80; remaining = 1; }
        else if (first >= 0xe0 && first <= 0xef) { point = first & 0x0f; minimum = 0x800; remaining = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { point = first & 0x07; minimum = 0x10000; remaining = 3; }
        else if (first >= 0x80) return false;
        if (remaining > value.label_bytes - offset) return false;
        for (unsigned i = 0; i < remaining; ++i) {
            const auto byte = value.label[offset++];
            if ((byte & 0xc0) != 0x80) return false;
            point = (point << 6) | (byte & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) ||
            point < 0x20 || (point >= 0x7f && point <= 0x9f) ||
            (point >= 0x202a && point <= 0x202e) || (point >= 0x2066 && point <= 0x2069)) return false;
        units += point >= 0x10000 ? 2 : 1;
        if (units > kNameUnits) return false;
    }
    return true;
}
bool descriptor_shape(const Descriptor& value) {
    return value.group && value.roster_version && value.roster_version != std::numeric_limits<std::uint32_t>::max() &&
           nonzero(value.admin) && nonzero(value.roster_digest) && nonzero(value.invite_id) &&
           value.rule <= LocationRule::required && valid_label(value);
}
struct Span {
    const std::uint8_t* ptr;
    std::size_t size;
};
template<std::size_t N>
Span span(const std::array<std::uint8_t, N>& value) { return {value.data(), value.size()}; }
Span span(const RadioFrame& value) { return {value.bytes.data(), value.size}; }
bool hash(const RosterCrypto& crypto, std::string_view label, std::initializer_list<Span> fields, Digest& out) {
    std::array<std::uint8_t, kHashInputLimit> input{};
    std::copy(label.begin(), label.end(), input.begin());
    auto size = label.size();
    for (const auto& field : fields) {
        if (field.size > input.size() - size) return false;
        std::copy_n(field.ptr, field.size, input.data() + size);
        size += field.size;
    }
    Digest candidate{};
    if (!crypto.digest(input.data(), size, candidate) || !nonzero(candidate)) return false;
    out = candidate;
    return true;
}
bool sign_bytes(std::string_view label, std::uint8_t* bytes, std::size_t unsigned_size,
                const RosterSigner& signer) {
    if (label.empty() || label.size() + unsigned_size > kSignatureInputLimit) return false;
    std::array<std::uint8_t, kSignatureInputLimit> input{};
    std::copy(label.begin(), label.end(), input.begin());
    std::copy_n(bytes, unsigned_size, input.data() + label.size());
    Signature signature{};
    if (!signer.sign(input.data(), label.size() + unsigned_size, signature) || !nonzero(signature)) return false;
    put(bytes + unsigned_size, signature);
    return true;
}
bool verify_bytes(std::string_view label, const std::uint8_t* bytes, std::size_t unsigned_size,
                  const PublicKey& signer, const RosterCrypto& crypto) {
    if (label.empty() || label.size() + unsigned_size > kSignatureInputLimit || !nonzero(signer)) return false;
    std::array<std::uint8_t, kSignatureInputLimit> input{};
    std::copy(label.begin(), label.end(), input.begin());
    std::copy_n(bytes, unsigned_size, input.data() + label.size());
    Signature signature{};
    get(bytes + unsigned_size, signature);
    return nonzero(signature) && crypto.verify(signature, input.data(), label.size() + unsigned_size, signer);
}
constexpr std::string_view kDescriptorDomain = "OpenTrail QR Descriptor Signature v1";

RadioFrame frame(Kind kind, Side side) {
    RadioFrame result{};
    result.size = size_of(kind);
    put(result.bytes.data(), kRadioMagic);
    result.bytes[4] = kVersion;
    result.bytes[5] = static_cast<std::uint8_t>(kind);
    result.bytes[6] = static_cast<std::uint8_t>(side);
    return result;
}
bool header(const RadioFrame& value, Kind kind, Side side) {
    return valid(side) && (kind != Kind::start || side == Side::joiner) &&
           (kind != Kind::reply || side == Side::admin) && size_of(kind) != 0 && value.size == size_of(kind) &&
           std::equal(kRadioMagic.begin(), kRadioMagic.end(), value.bytes.begin()) &&
           value.bytes[4] == kVersion && value.bytes[5] == static_cast<std::uint8_t>(kind) &&
           value.bytes[6] == static_cast<std::uint8_t>(side) && value.bytes[7] == 0;
}
bool signed_frame(RadioFrame candidate, Kind kind, const RosterSigner& signer, RadioFrame& out) {
    if (!sign_bytes(domain(kind), candidate.bytes.data(), candidate.size - kSignatureBytes, signer)) return false;
    out = candidate;
    return true;
}
bool signature_of(const RadioFrame& value, Kind kind, Side side, const PublicKey& signer,
                  const RosterCrypto& crypto) {
    return header(value, kind, side) &&
           verify_bytes(domain(kind), value.bytes.data(), value.size - kSignatureBytes, signer, crypto);
}
bool same(const RadioFrame& a, const RadioFrame& b) {
    return a.size == b.size && a.size <= a.bytes.size() &&
           std::equal(a.bytes.begin(), a.bytes.begin() + a.size, b.bytes.begin());
}
bool valid_fresh(const Fresh& value) {
    return nonzero(value.ephemeral) && nonzero(value.challenge) && nonzero(value.boot);
}
void put_fresh(std::uint8_t* p, const Fresh& value) {
    put(p, value.ephemeral); put(p + 32, value.challenge); put(p + 64, value.boot);
}
void get_fresh(const std::uint8_t* p, Fresh& value) {
    get(p, value.ephemeral); get(p + 32, value.challenge); get(p + 64, value.boot);
}
bool distinct(const Fresh& a, const Fresh& b) {
    return a.ephemeral != b.ephemeral && a.challenge != b.challenge && a.boot != b.boot;
}
bool valid_decision(const Decision& value) {
    return stage(value.kind) && valid(value.side) && nonzero(value.scope) &&
           nonzero(value.transaction) && nonzero(value.manifest);
}
bool same_context(const Decision& a, const Decision& b) {
    return a.scope == b.scope && a.transaction == b.transaction && a.manifest == b.manifest;
}
bool positive_final(Kind kind) {
    return kind == Kind::confirmed || kind == Kind::prepared || kind == Kind::committed;
}

}  // namespace

bool Timing::deadline(std::uint64_t& out) const {
    if (invitation_issued < request_started || request_started > std::numeric_limits<std::uint64_t>::max() - kRequestMs ||
        invitation_issued > std::numeric_limits<std::uint64_t>::max() - kInviteMs ||
        invitation_issued >= request_started + kRequestMs) return false;
    out = std::min(request_started + kRequestMs, invitation_issued + kInviteMs);
    return true;
}

bool issue_descriptor(const Descriptor& input, const RosterCrypto& crypto,
                      const RosterSigner& signer, DescriptorBytes& out) {
    const auto value = input;
    Fingerprint fingerprint{};
    if (!descriptor_shape(value) || value.admin != signer.public_key() ||
        !crypto.fingerprint(value.admin, fingerprint) || !nonzero(fingerprint)) return false;
    DescriptorBytes candidate{};
    auto* p = candidate.bytes.data();
    put(p, kQrMagic);
    p[4] = kVersion;
    p[5] = 1;  // QR rendezvous only.
    p[6] = static_cast<std::uint8_t>(Role::member);
    put(p + 8, value.admin);
    put_int(p + 40, value.group, 8);
    put_int(p + 48, value.roster_version, 4);
    put(p + 52, value.roster_digest);
    put(p + 84, value.invite_id);
    p[100] = static_cast<std::uint8_t>(value.rule);
    p[101] = kExpiryPolicy;
    p[102] = value.label_bytes;
    std::copy_n(value.label.begin(), value.label_bytes, p + kDescriptorPrefix);
    candidate.size = kDescriptorBaseBytes + value.label_bytes;
    if (!sign_bytes(kDescriptorDomain, p, candidate.size - kSignatureBytes, signer)) return false;
    out = candidate;
    return true;
}

bool verify_descriptor(const DescriptorBytes& input, const RosterCrypto& crypto, Descriptor& out) {
    const auto encoded = input;
    const auto* p = encoded.bytes.data();
    if (encoded.size < kDescriptorBaseBytes || encoded.size > encoded.bytes.size() ||
        !std::equal(kQrMagic.begin(), kQrMagic.end(), p) || p[4] != kVersion || p[5] != 1 ||
        p[6] != static_cast<std::uint8_t>(Role::member) || p[7] != 0 || p[101] != kExpiryPolicy ||
        p[102] > kNameBytes || encoded.size != kDescriptorBaseBytes + p[102]) return false;
    Descriptor value{};
    get(p + 8, value.admin);
    value.group = get_int(p + 40, 8);
    value.roster_version = static_cast<std::uint32_t>(get_int(p + 48, 4));
    get(p + 52, value.roster_digest);
    get(p + 84, value.invite_id);
    value.rule = static_cast<LocationRule>(p[100]);
    value.label_bytes = p[102];
    std::copy_n(p + kDescriptorPrefix, value.label_bytes, value.label.begin());
    Fingerprint fingerprint{};
    if (!descriptor_shape(value) || !crypto.fingerprint(value.admin, fingerprint) || !nonzero(fingerprint) ||
        !verify_bytes(kDescriptorDomain, p, encoded.size - kSignatureBytes, value.admin, crypto)) return false;
    out = value;
    return true;
}

bool scope_digest(const DescriptorBytes& input, const PublicKey& candidate,
                  const RosterCrypto& crypto, Digest& out) {
    const auto encoded = input;
    const auto joiner = candidate;
    Descriptor value{};
    Fingerprint initiator{}, responder{};
    Digest ticket{};
    constexpr std::array<std::uint8_t, 2> roles{
        static_cast<std::uint8_t>(Side::joiner), static_cast<std::uint8_t>(Side::admin)};
    return verify_descriptor(encoded, crypto, value) && joiner != value.admin &&
           crypto.fingerprint(joiner, initiator) && crypto.fingerprint(value.admin, responder) &&
           nonzero(initiator) && nonzero(responder) && initiator != responder &&
           hash(crypto, "OpenTrail QR Exact Descriptor v1", {{encoded.bytes.data(), encoded.size}}, ticket) &&
           hash(crypto, "OpenTrail QR Qualified Scope v1",
                {span(ticket), span(roles), span(joiner), span(initiator), span(value.admin), span(responder)}, out);
}

bool sign_start(const Start& input, const RosterCrypto& crypto, const RosterSigner& signer, RadioFrame& out) {
    const auto value = input;
    Fingerprint fingerprint{};
    if (value.joiner != signer.public_key() || !crypto.fingerprint(value.joiner, fingerprint) ||
        !nonzero(fingerprint) || !nonzero(value.scope) || !value.epoch || !valid_fresh(value.fresh)) return false;
    auto candidate = frame(Kind::start, Side::joiner);
    auto* p = candidate.bytes.data();
    put(p + 8, value.scope);
    put_int(p + 40, value.epoch, 4);
    put(p + 44, value.joiner);
    put_fresh(p + 76, value.fresh);
    return signed_frame(candidate, Kind::start, signer, out);
}

bool verify_start(const RadioFrame& input, const RosterCrypto& crypto, Start& out) {
    const auto encoded = input;
    if (!header(encoded, Kind::start, Side::joiner)) return false;
    const auto* p = encoded.bytes.data();
    Start value{};
    get(p + 8, value.scope);
    value.epoch = static_cast<std::uint32_t>(get_int(p + 40, 4));
    get(p + 44, value.joiner);
    get_fresh(p + 76, value.fresh);
    Fingerprint fingerprint{};
    if (!value.epoch || !nonzero(value.scope) || !valid_fresh(value.fresh) ||
        !crypto.fingerprint(value.joiner, fingerprint) || !nonzero(fingerprint) ||
        !signature_of(encoded, Kind::start, Side::joiner, value.joiner, crypto)) return false;
    out = value;
    return true;
}

bool resolve_start(const RadioFrame& encoded, const LocalInvites& invitations,
                   const PublicKey& admin, std::uint64_t now, const RosterCrypto& crypto, std::size_t& slot) {
    Start start{};
    if (!verify_start(encoded, crypto, start)) return false;
    std::optional<std::size_t> matched;
    std::array<std::uint64_t, kInvites> groups{};
    for (std::size_t i = 0; i < invitations.size(); ++i) {
        const auto& invitation = invitations[i];
        std::uint64_t end = 0;
        Descriptor value{};
        Digest expected{};
        if (!invitation.issued) continue;
        if (!invitation.timing.deadline(end)) return false;
        if (now < invitation.timing.invitation_issued || now >= end) continue;
        if (!verify_descriptor(*invitation.issued, crypto, value) || value.admin != admin ||
            std::find(groups.begin(), groups.begin() + i, value.group) != groups.begin() + i) return false;
        groups[i] = value.group;
        if (!scope_digest(*invitation.issued, start.joiner, crypto, expected) || expected != start.scope) continue;
        if (matched.has_value()) return false;
        matched = i;
    }
    if (!matched.has_value()) return false;
    slot = *matched;
    return true;
}

bool IssuedInvites::issue(const Descriptor& input, Timing timing, std::uint64_t now,
                          const RosterCrypto& crypto, const RosterSigner& signer, DescriptorBytes& out) {
    const auto descriptor = input;
    std::uint64_t end = 0;
    if (busy_ || now < last_ || now != timing.invitation_issued || !timing.deadline(end)) return false;
    last_ = now;
    Entry* available = nullptr;
    for (auto& entry : entries_) {
        std::uint64_t existing_end = 0;
        if (!entry.wire.size || (entry.timing.deadline(existing_end) && now >= existing_end)) {
            if (!available) available = &entry;
        } else if (entry.group == descriptor.group) return false;
    }
    if (!available) return false;
    busy_ = true;
    DescriptorBytes encoded{};
    const bool signed_ok = issue_descriptor(descriptor, crypto, signer, encoded);
    busy_ = false;
    if (!signed_ok) return false;
    *available = {encoded, timing, descriptor.group};
    out = encoded;
    return true;
}

bool IssuedInvites::revoke(const DescriptorBytes& exact) {
    if (busy_ || !exact.size || exact.size > exact.bytes.size()) return false;
    for (auto& entry : entries_) {
        if (entry.wire.size == exact.size &&
            std::equal(exact.bytes.begin(), exact.bytes.begin() + exact.size, entry.wire.bytes.begin())) {
            entry = {};
            return true;
        }
    }
    return false;
}

LocalInvites IssuedInvites::view() const {
    LocalInvites result{};
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].wire.size) result[i] = {&entries_[i].wire, entries_[i].timing};
    }
    return result;
}

bool public_manifest(const PublicState& value, const Digest& scope, const RosterCrypto& crypto, Digest& out) {
    if (!value.group || !value.roster_version || !value.epoch || !nonzero(scope) || !nonzero(value.invite_id) ||
        !nonzero(value.joiner) || !nonzero(value.admin) || value.joiner == value.admin ||
        !nonzero(value.roster_digest) || !nonzero(value.transcript) || value.rule > LocationRule::required)
        return false;
    std::array<std::uint8_t, 17> fields{};
    put_int(fields.data(), value.group, 8);
    put_int(fields.data() + 8, value.roster_version, 4);
    put_int(fields.data() + 12, value.epoch, 4);
    fields[16] = static_cast<std::uint8_t>(value.rule);
    return hash(crypto, "OpenTrail QR Public Pair Manifest v1",
                {span(scope), span(fields), span(value.invite_id), span(value.roster_digest), span(value.joiner),
                 span(value.admin), span(value.transcript)}, out);
}

bool sign_reply(const RadioFrame& input, const Fresh& input_fresh, const RosterCrypto& crypto,
                const RosterSigner& signer, RadioFrame& out) {
    const auto start_frame = input;
    const auto fresh = input_fresh;
    Start start{};
    Fingerprint admin{};
    Digest start_hash{};
    if (!verify_start(start_frame, crypto, start) || signer.public_key() == start.joiner ||
        !crypto.fingerprint(signer.public_key(), admin) || !nonzero(admin) || !valid_fresh(fresh) ||
        !distinct(start.fresh, fresh) ||
        !hash(crypto, "OpenTrail QR Exact Start v1", {span(start_frame)}, start_hash)) return false;
    auto candidate = frame(Kind::reply, Side::admin);
    put(candidate.bytes.data() + 8, start_hash);
    put_fresh(candidate.bytes.data() + 40, fresh);
    return signed_frame(candidate, Kind::reply, signer, out);
}

bool verify_reply(const RadioFrame& input, const PublicKey& admin, const RosterCrypto& crypto, Reply& out) {
    const auto encoded = input;
    if (!signature_of(encoded, Kind::reply, Side::admin, admin, crypto)) return false;
    Reply value{};
    get(encoded.bytes.data() + 8, value.start_hash);
    get_fresh(encoded.bytes.data() + 40, value.fresh);
    if (!nonzero(value.start_hash) || !valid_fresh(value.fresh)) return false;
    out = value;
    return true;
}

bool verify_receipt(const RadioFrame& input, const PublicKey& signer, const RosterCrypto& crypto, Decision& out) {
    const auto encoded = input;
    Decision value{};
    const auto* p = encoded.bytes.data();
    value.kind = static_cast<Kind>(p[5]);
    value.side = static_cast<Side>(p[6]);
    if (!stage(value.kind) || !signature_of(encoded, value.kind, value.side, signer, crypto)) return false;
    get(p + 8, value.scope);
    get(p + 40, value.transaction);
    get(p + 72, value.manifest);
    if (!valid_decision(value)) return false;
    out = value;
    return true;
}

bool Copies::offer(const RadioFrame& value, const Timing& timing, std::uint64_t now) {
    const auto kind = static_cast<Kind>(value.bytes[5]);
    const auto side = static_cast<Side>(value.bytes[6]);
    std::uint64_t end = 0;
    if (now < last_) return false;
    last_ = now;
    if (!header(value, kind, side) || !timing.deadline(end) ||
        now < timing.invitation_issued || now >= end) return false;
    if (timing_ && (timing_->request_started != timing.request_started ||
                    timing_->invitation_issued != timing.invitation_issued)) return false;
    const auto index = static_cast<std::size_t>(kind) - 1;
    if (frame_[index].size && !same(frame_[index], value)) return false;
    const auto copies = accepted_[index];
    const unsigned limit = kind == Kind::start ? 70 : 4;
    const auto floor = positive_final(kind) ? (copies ? kLaterCopyMs : kFirstCopyMs) : 1;
    const auto spacing = kind == Kind::start ? 5000u : (side == Side::joiner ? 2300u : 3000u);
    if (copies == limit || end - now < floor || (copies && now - accepted_at_[index] < spacing)) return false;
    timing_ = timing;
    frame_[index] = value;
    return true;
}

bool Copies::accepted(const RadioFrame& value, const Timing& timing, std::uint64_t now) {
    const auto kind = static_cast<Kind>(value.bytes[5]);
    if (!header(value, kind, static_cast<Side>(value.bytes[6]))) return false;
    const auto index = static_cast<std::size_t>(kind) - 1;
    if (!frame_[index].size || !offer(value, timing, now)) return false;
    ++accepted_[index];
    accepted_at_[index] = now;
    return true;
}


Session::Session(Side local, Timing timing, const RosterCrypto& crypto, const RosterSigner& signer,
                 Authority& authority)
    : local_(local), timing_(timing), crypto_(crypto), signer_(signer), authority_(authority) {}

bool Session::live(std::uint64_t reserve) {
    if (!active_ || failed_) return false;
    const auto now = authority_.now();
    if (now < last_ || now < timing_.invitation_issued || now >= deadline_ ||
        !authority_.current(descriptor_, joiner_, epoch_) || failed_ || signer_.public_key() != local_key_) {
        failed_ = true;
        return false;
    }
    last_ = now;
    return deadline_ - now >= reserve;
}

bool Session::begin(const DescriptorBytes& input, const RadioFrame& start_input, const RadioFrame& reply_input) {
    Operation op(*this);
    if (!op) return false;
    const auto encoded = input;
    const auto start_frame = start_input;
    const auto reply_frame = reply_input;
    if (!valid(local_) || active_ || failed_ || !timing_.deadline(deadline_)) return false;
    const auto observed = authority_.now();
    Descriptor descriptor{};
    Start start{};
    Reply reply{};
    Digest expected{}, start_hash{}, origin{};
    if (observed < timing_.invitation_issued || observed >= deadline_ ||
        !verify_descriptor(encoded, crypto_, descriptor) ||
        !verify_start(start_frame, crypto_, start) || !verify_reply(reply_frame, descriptor.admin, crypto_, reply) ||
        !scope_digest(encoded, start.joiner, crypto_, expected) || expected != start.scope ||
        !hash(crypto_, "OpenTrail QR Exact Start v1", {span(start_frame)}, start_hash) ||
        start_hash != reply.start_hash || !distinct(start.fresh, reply.fresh) ||
        signer_.public_key() != (local_ == Side::joiner ? start.joiner : descriptor.admin) ||
        !hash(crypto_, "OpenTrail QR Authenticated Origin v1",
              {span(expected), span(start_frame), span(reply_frame)}, origin)) return false;
    if (local_ == Side::admin) {
        const auto issued = authority_.issued_invites();
        std::size_t match = 0;
        if (!resolve_start(start_frame, issued, descriptor.admin, observed, crypto_, match)) return false;
        const auto& local = issued[match];
        if (local.timing.request_started != timing_.request_started ||
            local.timing.invitation_issued != timing_.invitation_issued ||
            local.issued->size != encoded.size ||
            !std::equal(encoded.bytes.begin(), encoded.bytes.begin() + encoded.size, local.issued->bytes.begin()))
            return false;
    }
    if (!authority_.fresh_unclaimed(descriptor.invite_id) || failed_) return false;
    descriptor_ = descriptor;
    peer_ = local_ == Side::admin ? start.joiner : descriptor.admin;
    joiner_ = start.joiner;
    local_key_ = local_ == Side::admin ? descriptor.admin : start.joiner;
    initiator_ = start.fresh;
    responder_ = reply.fresh;
    epoch_ = start.epoch;
    scope_ = expected;
    origin_ = origin;
    last_ = observed;
    active_ = true;
    return live();
}

bool Session::local_binding(Kind kind, const Digest& digest, RadioFrame& out) {
    auto& stored = kind == Kind::admission ? local_admission_ : local_confirmation_;
    if (!nonzero(digest) || !live()) return false;
    if (stored.size) { out = stored; return true; }
    const auto review = kind == Kind::admission ? Review::fingerprints : Review::transcript;
    const auto reserve = kind == Kind::confirmed ? kFirstCreationMs : 1;
    if (!live(reserve) || !authority_.approve(review, local_, descriptor_, peer_, digest) || !live(reserve))
        return false;
    auto next = frame(kind, local_);
    put(next.bytes.data() + 8, digest);
    RadioFrame signed_message{};
    if (!signed_frame(next, kind, signer_, signed_message) || !live(reserve)) return false;
    stored = signed_message;
    out = stored;
    return true;
}

bool Session::peer_binding(Kind kind, const Digest& digest, const RadioFrame& input) {
    const auto value = input;
    if (!nonzero(digest) || !live() ||
        !signature_of(value, kind, other(local_), peer_, crypto_) ||
        !std::equal(digest.begin(), digest.end(), value.bytes.begin() + 8) || !live()) return false;
    if (kind == Kind::admission) {
        if (peer_admission_ && !same(peer_admission_frame_, value)) return false;
        peer_admission_frame_ = value;
        peer_admission_ = true;
    } else {
        if (peer_confirmation_ && !same(peer_confirmation_frame_, value)) return false;
        peer_confirmation_frame_ = value;
        peer_confirmation_ = true;
    }
    return true;
}

bool Session::admission(RadioFrame& out) {
    Operation op(*this);
    return op && local_binding(Kind::admission, origin_, out);
}
bool Session::accept_admission(const RadioFrame& value) {
    Operation op(*this);
    return op && peer_binding(Kind::admission, origin_, value);
}

bool Session::enter_transcript() {
    Operation op(*this);
    if (!op || !live() || !local_admission_.size || !peer_admission_) return false;
    if (nonzero(transcript_)) return true;
    const auto& first = local_ == Side::joiner ? local_admission_ : peer_admission_frame_;
    const auto& second = local_ == Side::admin ? local_admission_ : peer_admission_frame_;
    Digest admitted{}, handshake{}, transcript{}, manifest{}, transaction{};
    if (!hash(crypto_, "OpenTrail QR Admission Origin v1",
              {span(scope_), span(origin_), span(first), span(second)}, admitted) ||
        !authority_.verified_handshake(admitted, initiator_.ephemeral, responder_.ephemeral, handshake) ||
        !nonzero(handshake) ||
        !hash(crypto_, "OpenTrail QR Transcript v1", {span(admitted), span(handshake)}, transcript)) return false;
    const PublicState state{joiner_, descriptor_.admin, descriptor_.roster_digest, transcript,
                            descriptor_.invite_id, descriptor_.group, descriptor_.roster_version,
                            epoch_, descriptor_.rule};
    if (!public_manifest(state, scope_, crypto_, manifest) ||
        !hash(crypto_, "OpenTrail QR Transaction v1", {span(scope_), span(transcript), span(manifest)}, transaction) ||
        !live()) return false;
    state_ = state;
    transcript_ = transcript;
    manifest_ = manifest;
    transaction_ = transaction;
    return true;
}

bool Session::confirmation(RadioFrame& out) {
    Operation op(*this);
    return op && local_binding(Kind::confirmed, transcript_, out);
}
bool Session::accept_confirmation(const RadioFrame& value) {
    Operation op(*this);
    return op && peer_binding(Kind::confirmed, transcript_, value);
}

bool Session::decision(Kind kind, Decision& out) const {
    const Decision expected{scope_, transaction_, manifest_, local_, kind};
    if (!valid_decision(expected)) return false;
    out = expected;
    return true;
}

Retained Session::retained(const Decision& value) const { return {value, state_, local_key_, peer_}; }

bool Session::receipt(Kind kind, RadioFrame& out) {
    Operation op(*this);
    if (!op) return false;
    Decision expected{};
    if (!live() || !decision(kind, expected)) return false;
    if (local_decision_ && local_decision_->kind == kind) {
        if (!authority_.readback(retained(*local_decision_)) || !live()) { failed_ = true; return false; }
        out = staged_receipt_;
        return true;
    }
    if (local_decision_ && local_decision_->kind != Kind::prepared) return false;
    if (kind == Kind::aborted) {
        if (peer_decision_ && peer_decision_->kind == Kind::committed) return false;
    } else {
        if (!local_confirmation_.size || !peer_confirmation_ || !live(kLaterCreationMs) ||
            (peer_decision_ && peer_decision_->kind == Kind::aborted)) return false;
        if (kind == Kind::committed && (!local_decision_ || !peer_decision_)) return false;
    }
    if (local_decision_ && !authority_.readback(retained(*local_decision_))) { failed_ = true; return false; }

    // Persist through the trusted owner's exact marker deadline. An ambiguous
    // outcome is terminal for this live session regardless of what survived.
    if (!authority_.persist(retained(expected), deadline_) || !authority_.readback(retained(expected)) || !live()) {
        failed_ = true;
        return false;
    }
    auto candidate = frame(kind, local_);
    put(candidate.bytes.data() + 8, expected.scope);
    put(candidate.bytes.data() + 40, expected.transaction);
    put(candidate.bytes.data() + 72, expected.manifest);
    RadioFrame signed_value{};
    if (!signed_frame(candidate, kind, signer_, signed_value) || !authority_.readback(retained(expected)) || !live()) {
        failed_ = true;
        return false;
    }
    local_decision_ = expected;
    staged_receipt_ = signed_value;
    out = signed_value;
    return true;
}

bool Session::accept_receipt(const RadioFrame& input) {
    Operation op(*this);
    if (!op) return false;
    const auto value = input;
    Decision received{}, expected{};
    if (!live() || !verify_receipt(value, peer_, crypto_, received) ||
        !decision(received.kind, expected) || !same_context(received, expected) || received.side != other(local_))
        return false;
    if (peer_decision_ && peer_decision_->kind == received.kind) return same(peer_receipt_, value);
    if (peer_decision_ && peer_decision_->kind != Kind::prepared) return false;
    if (received.kind == Kind::aborted) {
        if (local_decision_ && local_decision_->kind == Kind::committed) return false;
    } else {
        if (!local_confirmation_.size || !peer_confirmation_ ||
            (local_decision_ && local_decision_->kind == Kind::aborted)) return false;
        if (received.kind == Kind::committed && (!peer_decision_ || !local_decision_ ||
            !authority_.readback(retained(*local_decision_)))) return false;
    }
    if (!live()) return false;
    peer_decision_ = received;
    peer_receipt_ = value;
    return true;
}

bool Session::shape_successor(const Roster& current, std::string_view alias, Roster& next) {
    Digest digest{};
    if (local_ != Side::admin || failed_ || !local_decision_ || !peer_decision_ ||
        local_decision_->kind != Kind::committed || peer_decision_->kind != Kind::committed ||
        !authority_.readback(retained(*local_decision_)) || current.member_count == 0 ||
        current.members[0].key != descriptor_.admin || current.group_id != descriptor_.group ||
        current.version != descriptor_.roster_version || current.location_rule != descriptor_.rule ||
        !roster_digest(current, crypto_, digest) || digest != descriptor_.roster_digest) return false;
    return add_member(current, crypto_, joiner_, alias, next);
}

bool Session::successor(const Roster& current, std::string_view alias, Roster& out) {
    Operation op(*this);
    Roster next{};
    if (!op || membership_complete_ || !live() || !shape_successor(current, alias, next)) return false;
    std::array<std::uint8_t, kMaximumBodyBytes> body{};
    const auto size = encode_body(next, body.data(), body.size());
    Digest digest{};
    if (!size || !hash(crypto_, "OpenTrail QR Successor Body v1", {{body.data(), size}}, digest) ||
        (nonzero(successor_digest_) && digest != successor_digest_) || !live()) return false;
    successor_digest_ = digest;
    out = next;
    return true;
}

bool Session::membership(const Roster& previous, std::string_view alias,
                         const std::uint8_t* bytes, std::size_t size) {
    Operation op(*this);
    if (!op) return false;
    const auto now = authority_.now();
    if (!active_ || failed_ || now < last_ || now >= deadline_ || membership_complete_ ||
        !nonzero(successor_digest_) || !bytes) return false;
    last_ = now;
    Roster expected{}, observed{};
    std::array<std::uint8_t, kMaximumBodyBytes> expected_body{}, actual_body{};
    if (!shape_successor(previous, alias, expected) || decode(bytes, size, observed) != FormatError::none ||
        !verify_signature(observed, descriptor_.admin, crypto_)) return false;
    const auto expected_size = encode_body(expected, expected_body.data(), expected_body.size());
    const auto actual_size = encode_body(observed, actual_body.data(), actual_body.size());
    Digest digest{};
    if (!expected_size || expected_size != actual_size ||
        !std::equal(expected_body.begin(), expected_body.begin() + expected_size, actual_body.begin()) ||
        !hash(crypto_, "OpenTrail QR Successor Body v1", {{actual_body.data(), actual_size}}, digest) ||
        digest != successor_digest_ || !authority_.successor_readback(bytes, size)) return false;
    const auto after = authority_.now();
    if (failed_ || after < last_ || after >= deadline_) { failed_ = true; return false; }
    last_ = after;
    membership_complete_ = true;
    return true;
}

namespace {
bool valid_retained(const Retained& value, const RosterCrypto& crypto) {
    const auto& decision = value.decision;
    const auto& state = value.state;
    Digest computed{}, transaction{};
    return valid_decision(decision) &&
           value.local == (decision.side == Side::admin ? state.admin : state.joiner) &&
           value.peer == (decision.side == Side::admin ? state.joiner : state.admin) &&
           public_manifest(state, decision.scope, crypto, computed) && computed == decision.manifest &&
           hash(crypto, "OpenTrail QR Transaction v1",
                {span(decision.scope), span(state.transcript), span(decision.manifest)}, transaction) &&
           transaction == decision.transaction;
}

bool check_challenge(const Decision& retained, const RadioFrame& challenge, Side side,
                     const PublicKey& signer, const RosterCrypto& crypto, Nonce& nonce) {
    if (!signature_of(challenge, Kind::recovery_challenge, side, signer, crypto)) return false;
    const auto* p = challenge.bytes.data();
    get(p + 72, nonce);
    return nonzero(nonce) && std::equal(retained.scope.begin(), retained.scope.end(), p + 8) &&
           std::equal(retained.transaction.begin(), retained.transaction.end(), p + 40);
}
}  // namespace

bool recovery_reply(const Retained& input, const RadioFrame& challenge_input,
                    const Nonce& fresh_input, const RosterCrypto& crypto, const RosterSigner& signer,
                    Authority& authority, RadioFrame& out) {
    const auto exact = input;
    const auto challenge = challenge_input;
    const auto fresh = fresh_input;
    const auto& retained = exact.decision;
    Nonce nonce{};
    if (!valid_retained(exact, crypto) || signer.public_key() != exact.local || !nonzero(fresh) ||
        !authority.readback(exact) ||
        !check_challenge(retained, challenge, other(retained.side), exact.peer, crypto, nonce) ||
        fresh == nonce) return false;
    const auto kind = recovery_kind(retained.kind);
    auto response = frame(kind, retained.side);
    auto* p = response.bytes.data();
    put(p + 8, retained.scope);
    put(p + 40, retained.transaction);
    put(p + 72, retained.manifest);
    put(p + 104, nonce);
    put(p + 120, fresh);
    RadioFrame candidate{};
    if (!signed_frame(response, kind, signer, candidate) || !authority.readback(exact)) return false;
    out = candidate;
    return true;
}

RecoveryProbe::RecoveryProbe(const Retained& retained, const RosterCrypto& crypto,
                             const RosterSigner& signer, Authority& authority)
    : retained_(retained), crypto_(crypto), signer_(signer), authority_(authority) {}

bool RecoveryProbe::challenge(const Nonce& nonce, RadioFrame& out) {
    const auto& expected = retained_.decision;
    if (pending_.size || consumed_ || !valid_retained(retained_, crypto_) || !nonzero(nonce) ||
        retained_.local != signer_.public_key() || retained_.local == retained_.peer ||
        !authority_.readback(retained_)) return false;
    auto candidate = frame(Kind::recovery_challenge, expected.side);
    put(candidate.bytes.data() + 8, expected.scope);
    put(candidate.bytes.data() + 40, expected.transaction);
    put(candidate.bytes.data() + 72, nonce);
    RadioFrame signed_challenge{};
    if (!signed_frame(candidate, Kind::recovery_challenge, signer_, signed_challenge) ||
        !authority_.readback(retained_)) return false;
    pending_ = signed_challenge;
    out = pending_;
    return true;
}

bool RecoveryProbe::observe(const RadioFrame& input, Kind& peer_stage) {
    const auto reply = input;
    const auto& expected = retained_.decision;
    if (!pending_.size || consumed_ || !authority_.readback(retained_)) return false;
    const auto* p = reply.bytes.data();
    const auto kind = static_cast<Kind>(p[5]);
    if (kind != Kind::recovery_prepared && kind != Kind::recovery_committed && kind != Kind::recovery_aborted)
        return false;
    if (!signature_of(reply, kind, other(expected.side), retained_.peer, crypto_) ||
        !std::equal(expected.scope.begin(), expected.scope.end(), p + 8) ||
        !std::equal(expected.transaction.begin(), expected.transaction.end(), p + 40) ||
        !std::equal(expected.manifest.begin(), expected.manifest.end(), p + 72) ||
        (expected.kind == Kind::committed && kind == Kind::recovery_aborted) ||
        (expected.kind == Kind::aborted && kind == Kind::recovery_committed)) return false;
    Nonce nonce{}, fresh{}, expected_nonce{};
    get(p + 104, nonce);
    get(p + 120, fresh);
    get(pending_.bytes.data() + 72, expected_nonce);
    if (nonce != expected_nonce || fresh == nonce || !nonzero(fresh) || !authority_.readback(retained_)) return false;
    consumed_ = true;
    peer_stage = kind == Kind::recovery_prepared ? Kind::prepared :
                 kind == Kind::recovery_committed ? Kind::committed : Kind::aborted;
    return true;
}

}  // namespace opentrail::identity::roster::qr
