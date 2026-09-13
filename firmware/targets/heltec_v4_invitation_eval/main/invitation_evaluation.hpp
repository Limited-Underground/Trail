#pragma once
// Actual OT208 same-chip evaluation, shared by the target and SDK-seam host tests.
// The caller owns NVS initialization, guarded entropy/sodium startup, one consumed
// control request, and entropy shutdown. No radio, product trust enrollment, reset,
// receipt output or erase/recovery permission is provided by this function.
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <sodium.h>
#include "nvs_invitation_backend.hpp"
#include "nvs_policy_backend.hpp"
#include "opentrail/authorized_policy_session.hpp"

namespace opentrail::target::heltec_v4_invitation_eval {
using Clock = std::uint64_t (*)();

namespace evaluation_detail {
inline bool fill(security::SecureRandomSource& random, unsigned char* output, std::size_t size) {
    if (random.state() != security::EntropyState::ready) return false;
    const auto result = random.fill(output, size);
    return result.ok() && result.bytes_written == size && random.state() == security::EntropyState::ready;
}
struct SigningMaterial {
    std::array<unsigned char, 32> seed{};
    std::array<unsigned char, 64> secret{};
    ~SigningMaterial() {
        sodium_memzero(seed.data(), seed.size());
        sodium_memzero(secret.data(), secret.size());
    }
};
struct InvitationContext {
    security_evaluation::InvitationFields fields{};
    security_evaluation::Invitation invitation{};
    ~InvitationContext() {
        // The nonce, boot context, identities and signed invitation are public.
        // Clear these local copies too; secret keys have their own explicit owners.
        sodium_memzero(&fields, sizeof fields);
        sodium_memzero(&invitation, sizeof invitation);
    }
};
} // namespace evaluation_detail

inline bool evaluate(security::SecureRandomSource& random, Clock now_ms) {
    using namespace security_evaluation;
    if (now_ms == nullptr || random.state() != security::EntropyState::ready) return false;
    const auto entered_at = now_ms();
    if (entered_at > std::numeric_limits<std::uint64_t>::max() - kInvitationMaximumWindowMs) return false;

    NvsInvitationBackend boot_backend(NvsInvitationBackend::Store::boot);
    NvsInvitationBackend invitation_a(NvsInvitationBackend::Store::invite_a);
    NvsInvitationBackend invitation_b(NvsInvitationBackend::Store::invite_b);
    using PolicyBackend = security_eval::NvsPolicyBackend;
    PolicyBackend tx_a(PolicyBackend::Role::tx_a), tx_b(PolicyBackend::Role::tx_b);
    PolicyBackend rx_a(PolicyBackend::Role::rx_a), rx_b(PolicyBackend::Role::rx_b);
    if (!boot_backend.ready() || !invitation_a.ready() || !invitation_b.ready() ||
        !tx_a.ready() || !tx_b.ready() || !rx_a.ready() || !rx_b.ready()) return false;

    persistence::PersistentStorageKv boot_storage(boot_backend), ia_storage(invitation_a), ib_storage(invitation_b);
    persistence::PersistentStorageKv ta_storage(tx_a), tb_storage(tx_b), ra_storage(rx_a), rb_storage(rx_b);
    InvitationBootAuthority boot(boot_storage);
    // SDK opens happened since the entry sample. Revalidate admission immediately
    // before the first ledger mutation; this does not bound/cancel SDK calls.
    if (random.state() != security::EntropyState::ready) return false;
    const auto boot_at = now_ms();
    if (boot_at < entered_at ||
        boot_at > std::numeric_limits<std::uint64_t>::max() - kInvitationMaximumWindowMs) return false;
    if (!boot.start()) return false;

    evaluation_detail::SigningMaterial signing;
    evaluation_detail::InvitationContext context;
    auto& fields = context.fields;
    auto& invitation = context.invitation;
    // Authorities need the actual generated public identities. Declare their
    // fixed-storage owners first, construct them later, and destroy sessions first.
    std::optional<RoleInvitationAuthority> authority_a, authority_b;
    AuthorizedPolicySession a(random, ta_storage, ra_storage), b(random, tb_storage, rb_storage);
    if (!a.generate_identity() || !b.generate_identity()) return false;
    if (!evaluation_detail::fill(random, signing.seed.data(), signing.seed.size()) ||
        crypto_sign_seed_keypair(fields.signer.data(), signing.secret.data(), signing.seed.data()) != 0 ||
        !evaluation_detail::fill(random, fields.nonce.data(), fields.nonce.size())) return false;
    fields.group = 1;
    fields.epoch = 1;
    fields.peer_a = a.public_identity();
    fields.peer_b = b.public_identity();
    fields.boot_context = boot.context();
    fields.issued_ms = now_ms();
    if (fields.issued_ms < boot_at ||
        fields.issued_ms > std::numeric_limits<std::uint64_t>::max() - kInvitationMaximumWindowMs) return false;
    fields.deadline_ms = fields.issued_ms + kInvitationMaximumWindowMs;
    if (!encode_invitation(fields, invitation)) return false;
    unsigned long long signature_size = 0;
    if (crypto_sign_detached(invitation.signature.data(), &signature_size,
                            invitation.payload.data(), invitation.payload.size(), signing.secret.data()) != 0 ||
        signature_size != invitation.signature.size()) return false;

    // Same-chip evaluation authority pins actual local identities and the locally
    // generated signer. This supplies no production trust-root or human UI claim.
    authority_a.emplace(ia_storage, boot, InvitationRole::initiator, fields.signer, fields.peer_a, fields.peer_b);
    authority_b.emplace(ib_storage, boot, InvitationRole::responder, fields.signer, fields.peer_a, fields.peer_b);
    if (!a.bind_authority(*authority_a) || !b.bind_authority(*authority_b) ||
        !a.begin(invitation, now_ms()) || !b.begin(invitation, now_ms())) return false;
    {
        // Reconstruct authority objects over the retained ledgers, not a simulated
        // board reset. Reuse must refuse while the original live owners stay valid.
        RoleInvitationAuthority repeated_a(ia_storage, boot, InvitationRole::initiator,
                                            fields.signer, fields.peer_a, fields.peer_b);
        RoleInvitationAuthority repeated_b(ib_storage, boot, InvitationRole::responder,
                                            fields.signer, fields.peer_a, fields.peer_b);
        if (repeated_a.consume(invitation, now_ms()) || repeated_b.consume(invitation, now_ms()) ||
            !repeated_a.failed() || !repeated_b.failed() ||
            !authority_a->current() || !authority_b->current()) return false;
    }

    std::array<unsigned char, 128> message{};
    std::size_t length = 0;
    if (!a.write(message.data(), message.size(), length, now_ms()) ||
        !b.read(message.data(), length, now_ms()) ||
        !b.write(message.data(), message.size(), length, now_ms()) ||
        !a.read(message.data(), length, now_ms()) ||
        !a.write(message.data(), message.size(), length, now_ms()) ||
        !b.read(message.data(), length, now_ms()) ||
        !a.finish(now_ms()) || !b.finish(now_ms()) || a.transcript() != b.transcript()) return false;
    // Synthetic comparison exercises exact transcript binding, not human approval.
    if (!a.confirm(b.transcript(), now_ms()) || !b.confirm(a.transcript(), now_ms())) return false;
    constexpr std::array<unsigned char, 8> plain{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<unsigned char, 8> opened{};
    EvaluationRecord record{};
    if (!a.seal(plain, record, now_ms()) || !b.open(record, opened, now_ms()) || opened != plain) return false;
    opened.fill(0xA5);
    const auto unchanged = opened;
    if (b.open(record, opened, now_ms()) || opened != unchanged || b.failed()) return false;
    if (!b.seal(plain, record, now_ms()) || !a.open(record, opened, now_ms()) || opened != plain) return false;

    // Exercise active cancellation on one owner and ordinary retirement on the
    // other. Each independently gates its persistence and clears volatile secrets.
    const bool cancelled_a = a.cancel();
    const bool retired_b = b.retire();
    return cancelled_a && retired_b && a.retired() && b.retired() &&
           a.secrets_cleared() && b.secrets_cleared();
}
} // namespace opentrail::target::heltec_v4_invitation_eval
