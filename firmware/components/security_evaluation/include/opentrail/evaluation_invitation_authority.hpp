#pragma once
// OT206 evaluation authority. The trusted composition owns three isolated
// PersistentStorage instances: one boot ledger and one invitation ledger per
// local role. A single sequential owner serializes all access, including other
// reconstructed authorities; this interface provides no cross-thread/process CAS.
// There is no reset/erase permission or caller-supplied freshness/boot token.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sodium.h>
#include "opentrail/evaluation_invitation.hpp"
#include "opentrail/outbound_counter_lease_store.hpp"

namespace opentrail::security_evaluation {
enum class InvitationRole : std::uint8_t { initiator = 1, responder = 2 };

namespace authority_detail {
using Bytes = std::array<std::uint8_t, persistence::kPersistentSlotBytes>;
using Snapshot = std::array<Bytes, persistence::kPersistentSlotCount>;
inline constexpr auto domain = persistence::StorageDomain::outbound_counter_state;
inline constexpr persistence::CounterDomainId boot_domain{
    'O','T','2','0','6','-','B','O','O','T','-','V','1',0,0,1};
inline constexpr std::uint32_t claim_marker = 0x2061CA17U;
inline void put(std::uint8_t* p, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) p[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
inline std::uint64_t get(const std::uint8_t* p, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value |= std::uint64_t(p[i]) << (8 * i);
    return value;
}
inline std::uint32_t checksum(const Bytes& bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < 56; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}
inline bool snapshot(persistence::PersistentStorage& storage, Snapshot& output) {
    for (std::size_t slot = 0; slot < output.size(); ++slot) {
        const auto read = storage.read_slot(domain, slot, {output[slot].data(), output[slot].size()});
        if (!read.read() || read.bytes_read != output[slot].size()) return false;
    }
    return true;
}
inline bool role_valid(InvitationRole role) {
    return role == InvitationRole::initiator || role == InvitationRole::responder;
}
// Exact encoding of the frozen counter contract for this lease-size-one boot
// owner. The allocator itself is unchanged; its counter equals its generation.
inline Bytes boot_record(std::uint32_t generation) {
    Bytes bytes{};
    std::memcpy(bytes.data(), "OTCN", 4);
    bytes[4] = persistence::kOutboundCounterEnvelopeVersion;
    bytes[5] = persistence::kOutboundCounterSchemaVersion;
    bytes[7] = 16;
    put(bytes.data() + 8, generation, 4);
    put(bytes.data() + 12, 28, 2);
    std::memcpy(bytes.data() + 16, boot_domain.data(), boot_domain.size());
    put(bytes.data() + 32, 1, 4);
    put(bytes.data() + 36, generation, 8);
    put(bytes.data() + 56, checksum(bytes), 4);
    put(bytes.data() + 60, persistence::kOutboundCounterCommitMarker, 4);
    return bytes;
}
inline bool prior_boot_record(const Bytes& bytes) {
    bool blank = true;
    for (auto value : bytes) blank = blank && value == 0xFF;
    if (blank) return true;
    const auto generation = static_cast<std::uint32_t>(get(bytes.data() + 8, 4));
    return generation != 0 && bytes == boot_record(generation);
}
} // namespace authority_detail

// The trusted startup owner calls start exactly once on each object. It always
// reserves a new durable generation, including after reconstruction. The token
// is deterministic and public: uniqueness is scoped to this retained boot ledger,
// not global uniqueness or an entropy claim. Whole-flash attacker rollback and
// authorized destructive factory reset remain outside this evaluation contract.
class InvitationBootAuthority final {
public:
    explicit InvitationBootAuthority(persistence::PersistentStorage& storage) : storage_(storage) {}
    InvitationBootAuthority(const InvitationBootAuthority&) = delete;
    InvitationBootAuthority& operator=(const InvitationBootAuthority&) = delete;
    bool start() {
        if (started_) return refuse();
        started_ = true;
        authority_detail::Snapshot prior{};
        if (!authority_detail::snapshot(storage_, prior)) return refuse();
        // Validate even the slot about to be overwritten. A corrupt snapshot of
        // that slot must not disappear merely because allocation replaces it.
        for (const auto& bytes : prior)
            if (!authority_detail::prior_boot_record(bytes)) return refuse();
        persistence::OutboundCounterLeaseStore ledger(storage_);
        persistence::OutboundCounterLeaseRequest request{};
        request.domain_id = authority_detail::boot_domain;
        request.group_epoch = 1;
        request.lease_size = 1;
        const auto allocation = ledger.reserve(request);
        if (!allocation.reserved() || allocation.first_counter != allocation.last_counter ||
            allocation.last_counter != allocation.generation ||
            allocation.written_slot >= persistence::kPersistentSlotCount ||
            !authority_detail::snapshot(storage_, snapshot_)) return refuse();
        // Bind our retained snapshot to its verified result, including all header,
        // domain, reserved, CRC and marker bytes, and preserve the untouched slot.
        const auto expected = authority_detail::boot_record(allocation.generation);
        if (snapshot_[allocation.written_slot] != expected ||
            snapshot_[1 - allocation.written_slot] != prior[1 - allocation.written_slot]) return refuse();
        generation_ = allocation.last_counter;
        constexpr std::uint8_t label[] = "OpenTrail OT206 durable boot context v1";
        std::array<std::uint8_t, sizeof(label) - 1 + 8> input{};
        std::memcpy(input.data(), label, sizeof(label) - 1);
        authority_detail::put(input.data() + sizeof(label) - 1, generation_, 8);
        InvitationKey hash{};
        if (crypto_hash_sha256(hash.data(), input.data(), input.size()) != 0) return refuse();
        std::memcpy(context_.data(), hash.data(), context_.size());
        ready_ = true;
        return current();
    }
    bool current() const {
        if (!ready_ || failed_) return false;
        authority_detail::Snapshot observed{};
        if (!authority_detail::snapshot(storage_, observed) || observed != snapshot_) return refuse();
        return true;
    }
    bool ready() const { return current(); }
    bool failed() const { return failed_; }
    std::uint64_t generation() const { return generation_; }
    const InvitationToken& context() const { return context_; }
private:
    bool refuse() const { failed_ = true; ready_ = false; return false; }
    persistence::PersistentStorage& storage_;
    authority_detail::Snapshot snapshot_{};
    InvitationToken context_{};
    std::uint64_t generation_{0};
    bool started_{false};
    mutable bool ready_{false}, failed_{false};
};

// Trusted pins and local role come from the composition, never from the received
// invitation. The now value is the composition's trusted monotonic clock sample.
// Invalid/malformed/expired attempts and explicit cancellation consume the role
// ledger before refusal. This deliberately mutates authorization storage; session
// TX/RX must remain untouched until consume succeeds. A failed operation which
// leaves no durable bytes cannot promise consumption after a power loss, but it
// never authorizes a session. Partial/ambiguous retained records refuse on reopen.
class RoleInvitationAuthority final {
public:
    RoleInvitationAuthority(persistence::PersistentStorage& storage, const InvitationBootAuthority& boot,
                            InvitationRole role, const InvitationKey& signer,
                            const InvitationKey& peer_a, const InvitationKey& peer_b)
        : storage_(storage), boot_(boot), role_(role), signer_(signer), a_(peer_a), b_(peer_b) {
        if (!authority_detail::role_valid(role_) || !invitation_detail::nonzero(signer_) ||
            !invitation_detail::nonzero(a_) || !invitation_detail::nonzero(b_) || a_ == b_)
            phase_ = Phase::failed;
    }
    RoleInvitationAuthority(const RoleInvitationAuthority&) = delete;
    RoleInvitationAuthority& operator=(const RoleInvitationAuthority&) = delete;
    bool consume(const Invitation& invitation, std::uint64_t now) {
        if (!take(&invitation, 1)) return false;
        InvitationGate validated;
        if (!validated.open(invitation, signer_, a_, b_, boot_.context(), now)) return refuse();
        phase_ = Phase::authorized;
        return current();
    }
    bool cancel() {
        // Successful consumption already burned this boot/role durably. Revoke
        // the live admission without trying to spend the same authorization twice.
        if (durable_) {
            if (!retained()) return false;
            if (phase_ != Phase::failed) phase_ = Phase::cancelled;
            return true;
        }
        if (!take(nullptr, 2)) return false;
        phase_ = Phase::cancelled;
        return true;
    }
    bool current() const {
        if (phase_ != Phase::authorized) return false;
        return retained();
    }
    bool failed() const { return phase_ == Phase::failed; }
    bool consumed() const { return attempted_; }
    InvitationRole role() const { return role_; }
    const InvitationKey& trusted_signer() const { return signer_; }
    const InvitationKey& peer_a() const { return a_; }
    const InvitationKey& peer_b() const { return b_; }
    const InvitationToken& boot_context() const { return boot_.context(); }
private:
    enum class Phase { unused, spent, authorized, cancelled, failed };
    bool retained() const {
        authority_detail::Snapshot observed{};
        if (!boot_.current() || !authority_detail::snapshot(storage_, observed) || observed != snapshot_)
            return refuse();
        return true;
    }
    struct Record {
        authority_detail::Bytes bytes{};
        bool blank{true};
        std::uint64_t boot_generation{0};
        std::uint32_t sequence{0};
    };
    bool inspect(const authority_detail::Bytes& bytes, Record& result) const {
        result.bytes = bytes;
        for (auto value : bytes) result.blank = result.blank && value == 0xFF;
        if (result.blank) return true;
        using authority_detail::get;
        if (std::memcmp(bytes.data(), "OTIA", 4) != 0 || bytes[4] != 1 ||
            (bytes[5] != 1 && bytes[5] != 2) || bytes[6] != static_cast<std::uint8_t>(role_) ||
            bytes[7] != 0 || get(bytes.data() + 52, 4) != 0 ||
            get(bytes.data() + 56, 4) != authority_detail::checksum(bytes) ||
            get(bytes.data() + 60, 4) != authority_detail::claim_marker) return false;
        result.boot_generation = get(bytes.data() + 8, 8);
        result.sequence = static_cast<std::uint32_t>(get(bytes.data() + 48, 4));
        return result.boot_generation != 0 && result.sequence != 0;
    }
    bool binding(const Invitation* invitation, std::uint8_t operation, InvitationKey& output) const {
        constexpr std::uint8_t label[] = "OpenTrail OT206 role invitation consumption v1";
        crypto_hash_sha256_state state{};
        const auto role = static_cast<std::uint8_t>(role_);
        bool ok = crypto_hash_sha256_init(&state) == 0 &&
            crypto_hash_sha256_update(&state, label, sizeof(label) - 1) == 0 &&
            crypto_hash_sha256_update(&state, &operation, 1) == 0 &&
            crypto_hash_sha256_update(&state, &role, 1) == 0 &&
            crypto_hash_sha256_update(&state, boot_.context().data(), boot_.context().size()) == 0 &&
            crypto_hash_sha256_update(&state, signer_.data(), signer_.size()) == 0 &&
            crypto_hash_sha256_update(&state, a_.data(), a_.size()) == 0 &&
            crypto_hash_sha256_update(&state, b_.data(), b_.size()) == 0;
        if (ok && invitation != nullptr)
            ok = crypto_hash_sha256_update(&state, invitation->payload.data(), invitation->payload.size()) == 0 &&
                crypto_hash_sha256_update(&state, invitation->signature.data(), invitation->signature.size()) == 0;
        ok = ok && crypto_hash_sha256_final(&state, output.data()) == 0;
        sodium_memzero(&state, sizeof state);
        return ok;
    }
    bool take(const Invitation* invitation, std::uint8_t operation) {
        if (phase_ != Phase::unused) return refuse();
        attempted_ = true;
        phase_ = Phase::spent;
        authority_detail::Snapshot prior{};
        if (!boot_.current() || !authority_detail::snapshot(storage_, prior)) return refuse();
        std::array<Record, 2> records{};
        for (std::size_t i = 0; i < records.size(); ++i)
            if (!inspect(prior[i], records[i])) return refuse();
        std::size_t latest = records[0].blank ? 1 : 0;
        if (!records[1].blank && records[1].sequence > records[latest].sequence) latest = 1;
        const auto& previous = records[latest];
        const auto& older = records[1 - latest];
        if (!older.blank && (previous.sequence <= older.sequence ||
            previous.sequence - older.sequence != 1 || previous.boot_generation <= older.boot_generation))
            return refuse();
        if (!previous.blank && (previous.boot_generation >= boot_.generation() ||
            previous.sequence == std::numeric_limits<std::uint32_t>::max())) return refuse();
        InvitationKey digest{};
        if (!binding(invitation, operation, digest)) return refuse();
        authority_detail::Bytes bytes{};
        std::memcpy(bytes.data(), "OTIA", 4);
        bytes[4] = 1; bytes[5] = operation; bytes[6] = static_cast<std::uint8_t>(role_);
        authority_detail::put(bytes.data() + 8, boot_.generation(), 8);
        std::memcpy(bytes.data() + 16, digest.data(), digest.size());
        authority_detail::put(bytes.data() + 48, previous.blank ? 1 : previous.sequence + 1, 4);
        authority_detail::put(bytes.data() + 56, authority_detail::checksum(bytes), 4);
        authority_detail::put(bytes.data() + 60, authority_detail::claim_marker, 4);
        const std::size_t target = previous.blank ? 0 : 1 - latest;
        using persistence::StorageError;
        if (!boot_.current() || storage_.erase_slot(authority_detail::domain, target) != StorageError::none ||
            storage_.write_slot(authority_detail::domain, target, 0, {bytes.data(), 60}) != StorageError::none ||
            storage_.sync_slot(authority_detail::domain, target) != StorageError::none ||
            storage_.write_slot(authority_detail::domain, target, 60, {bytes.data() + 60, 4}) != StorageError::none ||
            storage_.sync_slot(authority_detail::domain, target) != StorageError::none ||
            !authority_detail::snapshot(storage_, snapshot_) || snapshot_[target] != bytes ||
            snapshot_[1 - target] != prior[1 - target] || !boot_.current()) return refuse();
        durable_ = true;
        return true;
    }
    bool refuse() const { phase_ = Phase::failed; return false; }
    persistence::PersistentStorage& storage_;
    const InvitationBootAuthority& boot_;
    const InvitationRole role_;
    const InvitationKey signer_, a_, b_;
    authority_detail::Snapshot snapshot_{};
    mutable Phase phase_{Phase::unused};
    bool attempted_{false}, durable_{false};
};
} // namespace opentrail::security_evaluation
