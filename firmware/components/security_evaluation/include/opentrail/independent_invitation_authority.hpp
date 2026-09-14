#pragma once
// OT228 durable admission for the independent-clock invitation contract.
// One serialized owner supplies distinct retained boot/role storage. The reentry
// latch protects callback ordering; it is not a cross-thread/process CAS.
// CRC/readback detects storage uncertainty, not malicious whole-flash rollback.
// No erase/reset/retry authority is granted by constructing a new object.
#include "opentrail/independent_invitation.hpp"

namespace opentrail::security_evaluation {
namespace independent_authority_detail {
inline constexpr std::array<std::uint8_t, 4> magic{'O','T','I','2'};
inline constexpr std::uint8_t version = 2;
inline constexpr std::uint32_t claim_marker = 0x2281CA17U;
inline constexpr std::uint8_t binding_label[] =
    "OpenTrail OT228 independent role invitation consumption v2";
} // namespace independent_authority_detail

// Invalid/expired invitations and explicit cancellation consume this role's
// authorization ledger before refusal; TX/RX are outside this component. A
// failure before any durable bytes cannot promise retained consumption across
// power loss. Complete, partial and ambiguous retained writes never authorize a
// reconstructed same-boot role. Old OTIA records are not v2 admission records.
class IndependentRoleInvitationAuthority final {
public:
    IndependentRoleInvitationAuthority(persistence::PersistentStorage& storage,
                                       const InvitationBootAuthority& boot, InvitationRole role,
                                       const InvitationKey& signer, const InvitationKey& peer_a,
                                       const InvitationKey& peer_b)
        : storage_(storage), boot_(boot), role_(role), signer_(signer), a_(peer_a), b_(peer_b) {
        if (!authority_detail::role_valid(role_) || !invitation_detail::nonzero(signer_) ||
            !invitation_detail::nonzero(a_) || !invitation_detail::nonzero(b_) || a_ == b_)
            phase_ = Phase::failed;
    }
    IndependentRoleInvitationAuthority(const IndependentRoleInvitationAuthority&) = delete;
    IndependentRoleInvitationAuthority& operator=(const IndependentRoleInvitationAuthority&) = delete;

    bool consume(const IndependentInvitation& invitation, std::uint64_t now) {
        const IndependentInvitation candidate = invitation;
        return operation([&] {
            if (!take(&candidate, 1)) return false;
            IndependentInvitationGate validated;
            if (!pending() || !validated.open(candidate, role_, signer_, a_, b_, boot_.context(), now) ||
                !pending()) return refuse();
            phase_ = Phase::authorized;
            return retained() && phase_ == Phase::authorized;
        });
    }
    bool cancel() {
        return operation([&] {
            if (durable_) {
                // The retained claim already burned this role. Cancellation of
                // an admitted role revokes it without another durable mutation.
                if (!retained()) return false;
                if (phase_ != Phase::failed) phase_ = Phase::cancelled;
                return true;
            }
            if (!take(nullptr, 2) || !pending()) return false;
            phase_ = Phase::cancelled;
            return true;
        });
    }
    bool current() const {
        if (!enter()) return false;
        const bool accepted = phase_ == Phase::authorized && retained() && phase_ == Phase::authorized;
        busy_ = false;
        return accepted && !reentered_;
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
    bool enter() const {
        if (busy_) { reentered_ = true; return refuse(); }
        if (reentered_) return refuse();
        busy_ = true;
        return true;
    }
    template<class Action> bool operation(Action action) {
        if (!enter()) return false;
        const bool accepted = action();
        if (reentered_) (void)refuse();
        busy_ = false;
        return accepted && !reentered_;
    }
    bool pending() const { return phase_ == Phase::spent && !reentered_; }
    bool retained() const {
        authority_detail::Snapshot observed{};
        if (reentered_ || !boot_.current() || !authority_detail::snapshot(storage_, observed) ||
            observed != snapshot_ || reentered_ || !boot_.current() || reentered_) return refuse();
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
        if (std::memcmp(bytes.data(), independent_authority_detail::magic.data(), 4) != 0 ||
            bytes[4] != independent_authority_detail::version || (bytes[5] != 1 && bytes[5] != 2) ||
            bytes[6] != static_cast<std::uint8_t>(role_) || bytes[7] != 0 || get(bytes.data() + 52, 4) != 0 ||
            get(bytes.data() + 56, 4) != authority_detail::checksum(bytes) ||
            get(bytes.data() + 60, 4) != independent_authority_detail::claim_marker) return false;
        result.boot_generation = get(bytes.data() + 8, 8);
        result.sequence = static_cast<std::uint32_t>(get(bytes.data() + 48, 4));
        return result.boot_generation != 0 && result.sequence != 0;
    }
    bool binding(const IndependentInvitation* invitation, std::uint8_t operation, InvitationKey& output) const {
        crypto_hash_sha256_state state{};
        const auto role = static_cast<std::uint8_t>(role_);
        bool ok = crypto_hash_sha256_init(&state) == 0 &&
            crypto_hash_sha256_update(&state, independent_authority_detail::binding_label,
                sizeof(independent_authority_detail::binding_label) - 1) == 0 &&
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
    bool take(const IndependentInvitation* invitation, std::uint8_t operation) {
        if (phase_ != Phase::unused) return refuse();
        attempted_ = true;
        phase_ = Phase::spent;
        authority_detail::Snapshot prior{};
        if (!boot_.current() || !pending() || !authority_detail::snapshot(storage_, prior) || !pending())
            return refuse();
        std::array<Record, 2> records{};
        for (std::size_t i = 0; i < records.size(); ++i)
            if (!inspect(prior[i], records[i])) return refuse();
        std::size_t latest = records[0].blank ? 1 : 0;
        if (!records[1].blank && records[1].sequence > records[latest].sequence) latest = 1;
        const auto& previous = records[latest];
        const auto& older = records[1 - latest];
        if (!older.blank && (previous.sequence <= older.sequence || previous.sequence - older.sequence != 1 ||
            previous.boot_generation <= older.boot_generation)) return refuse();
        if (!previous.blank && (previous.boot_generation >= boot_.generation() ||
            previous.sequence == std::numeric_limits<std::uint32_t>::max())) return refuse();
        InvitationKey digest{};
        if (!binding(invitation, operation, digest) || !pending()) return refuse();
        authority_detail::Bytes bytes{};
        std::memcpy(bytes.data(), independent_authority_detail::magic.data(), 4);
        bytes[4] = independent_authority_detail::version;
        bytes[5] = operation;
        bytes[6] = static_cast<std::uint8_t>(role_);
        authority_detail::put(bytes.data() + 8, boot_.generation(), 8);
        std::memcpy(bytes.data() + 16, digest.data(), digest.size());
        authority_detail::put(bytes.data() + 48, previous.blank ? 1 : previous.sequence + 1, 4);
        authority_detail::put(bytes.data() + 56, authority_detail::checksum(bytes), 4);
        authority_detail::put(bytes.data() + 60, independent_authority_detail::claim_marker, 4);
        const std::size_t target = previous.blank ? 0 : 1 - latest;
        using persistence::StorageError;
        if (!boot_.current() || !pending() ||
            storage_.erase_slot(authority_detail::domain, target) != StorageError::none || !pending() ||
            storage_.write_slot(authority_detail::domain, target, 0, {bytes.data(), 60}) != StorageError::none || !pending() ||
            storage_.sync_slot(authority_detail::domain, target) != StorageError::none || !pending() ||
            storage_.write_slot(authority_detail::domain, target, 60, {bytes.data() + 60, 4}) != StorageError::none || !pending() ||
            storage_.sync_slot(authority_detail::domain, target) != StorageError::none || !pending() ||
            !authority_detail::snapshot(storage_, snapshot_) || !pending() || snapshot_[target] != bytes ||
            snapshot_[1 - target] != prior[1 - target] || !boot_.current() || !pending()) return refuse();
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
    mutable bool busy_{false}, reentered_{false};
    bool attempted_{false}, durable_{false};
};
} // namespace opentrail::security_evaluation
