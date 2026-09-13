#pragma once
// Actual OT215 same-chip evaluation, shared by the target and SDK-seam host tests.
// The caller owns NVS initialization, guarded entropy/sodium startup, one consumed
// control request, and entropy shutdown. No radio, product trust enrollment, reset,
// receipt output or erase/recovery permission is provided by this function.
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <sodium.h>
#include "nvs_confirmation_backend.hpp"
#include "opentrail/evaluation_confirmation_owner.hpp"

namespace opentrail::target::heltec_v4_confirmation_eval {
using Clock = std::uint64_t (*)();
// Explicit same-chip evaluation decisions. These are not a human, phone, BLE
// authorization, or a remote peer. Production has no adapter to this target.
enum class SyntheticDecision { confirm, cancel, withhold };
struct SyntheticConfirmationInput {
    SyntheticDecision a{SyntheticDecision::confirm};
    SyntheticDecision b{SyntheticDecision::confirm};
};

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
class LocalConfirmationAuthority final : public security_evaluation::ConfirmationAuthority {
public:
    LocalConfirmationAuthority(security_evaluation::ConfirmationContext context, Clock clock)
        : context_(context), clock_(clock) {}
    security_evaluation::ConfirmationSample sample() override { return {context_, clock_()}; }
private:
    const security_evaluation::ConfirmationContext context_;
    Clock clock_;
};
inline bool matches(const security_evaluation::ConfirmationOffer* offer,
                    security_evaluation::ConfirmationContext context,
                    security_evaluation::InvitationRole role,
                    const security_evaluation::InvitationFields& fields,
                    const security_evaluation::InvitationKey& transcript) {
    return offer != nullptr && offer->context() == context && offer->role() == role &&
           offer->group() == fields.group && offer->epoch() == fields.epoch &&
           offer->nonce() == fields.nonce && offer->deadline_ms() == fields.deadline_ms &&
           offer->peer() == (role == security_evaluation::InvitationRole::initiator ? fields.peer_b : fields.peer_a) &&
           offer->transcript() == transcript;
}
inline bool decide(security_evaluation::EvaluationConfirmationOwner& owner,
                   const security_evaluation::ConfirmationOffer& offer, SyntheticDecision decision) {
    if (decision == SyntheticDecision::confirm) return owner.confirm(offer);
    if (decision == SyntheticDecision::cancel) (void)owner.cancel(offer);
    return false;
}
} // namespace evaluation_detail

inline bool evaluate(security::SecureRandomSource& random, Clock now_ms,
                     SyntheticConfirmationInput input = {}) {
    using namespace security_evaluation;
    if (now_ms == nullptr || random.state() != security::EntropyState::ready) return false;
    const auto entered_at = now_ms();
    if (entered_at > std::numeric_limits<std::uint64_t>::max() - kInvitationMaximumWindowMs) return false;

    NvsConfirmationBackend boot_backend(NvsConfirmationBackend::Store::boot);
    NvsConfirmationBackend invitation_a(NvsConfirmationBackend::Store::invite_a);
    NvsConfirmationBackend invitation_b(NvsConfirmationBackend::Store::invite_b);
    using PolicyBackend = NvsConfirmationBackend;
    PolicyBackend tx_a(PolicyBackend::Store::tx_a), tx_b(PolicyBackend::Store::tx_b);
    PolicyBackend rx_a(PolicyBackend::Store::rx_a), rx_b(PolicyBackend::Store::rx_b);
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
    const ConfirmationContext context_a{boot.generation(), 1}, context_b{boot.generation(), 2};
    evaluation_detail::LocalConfirmationAuthority local_a(context_a, now_ms), local_b(context_b, now_ms);
    EvaluationConfirmationOwner owner_a(a, *authority_a, local_a), owner_b(b, *authority_b, local_b);
    const bool evaluated = [&]() {
        if (!owner_a.begin(invitation) || !owner_b.begin(invitation)) return false;
        {
            // Reconstructed objects inspect retained ledgers; no board reset is simulated.
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
        if (!owner_a.write(message.data(), message.size(), length) || !owner_b.read(message.data(), length) ||
            !owner_b.write(message.data(), message.size(), length) || !owner_a.read(message.data(), length) ||
            !owner_a.write(message.data(), message.size(), length) || !owner_b.read(message.data(), length) ||
            !owner_a.finish() || !owner_b.finish() || a.transcript() != b.transcript()) return false;
        const auto* offer_a = owner_a.offer();
        const auto* offer_b = owner_b.offer();
        if (!evaluation_detail::matches(offer_a, context_a, InvitationRole::initiator, fields, a.transcript()) ||
            !evaluation_detail::matches(offer_b, context_b, InvitationRole::responder, fields, b.transcript()) ||
            owner_a.outcome() != ConfirmationOutcome::review || owner_b.outcome() != ConfirmationOutcome::review) return false;
        // This explicit synthetic decision consumes each exact device-owned offer.
        // The first local confirmation already initializes its TX/RX journals;
        // traffic remains unavailable here until the second owner also confirms.
        if (!evaluation_detail::decide(owner_a, *offer_a, input.a) ||
            !evaluation_detail::decide(owner_b, *offer_b, input.b) ||
            owner_a.outcome() != ConfirmationOutcome::local_confirmed ||
            owner_b.outcome() != ConfirmationOutcome::local_confirmed) return false;
        constexpr std::array<unsigned char, 8> plain{0, 1, 2, 3, 4, 5, 6, 7};
        std::array<unsigned char, 8> opened{};
        EvaluationRecord record{};
        if (!a.seal(plain, record, now_ms()) || !b.open(record, opened, now_ms()) || opened != plain) return false;
        opened.fill(0xA5);
        const auto unchanged = opened;
        if (b.open(record, opened, now_ms()) || opened != unchanged || b.failed()) return false;
        if (!b.seal(plain, record, now_ms()) || !a.open(record, opened, now_ms()) || opened != plain) return false;
        return true;
    }();
    // Independent explicit cleanup also runs after a failed/cancelled second
    // decision. Active local roles retire RX; any uncertainty remains a refusal
    // with retained journals, never permission to reset or erase them.
    const bool closed_a = owner_a.close();
    const bool closed_b = owner_b.close();
    return evaluated && closed_a && closed_b && a.retired() && b.retired() &&
           a.secrets_cleared() && b.secrets_cleared();
}
} // namespace opentrail::target::heltec_v4_confirmation_eval
