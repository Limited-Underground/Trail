#pragma once

#include <cstdint>

namespace opentrail::companion {

// Opaque reference minted and resolved by the trusted BLE bond store. It must
// be stable across reboot, nonzero, unique per bond, and unavailable to the
// peer. It must never contain a raw BLE address, public device identifier,
// client-supplied bytes, or cryptographic key material. This component only
// compares and persists the opaque value; it does not derive or log it.
struct CompanionBondIdentityToken {
    std::uint64_t high{0};
    std::uint64_t low{0};
};

[[nodiscard]] constexpr bool operator==(
    const CompanionBondIdentityToken& left,
    const CompanionBondIdentityToken& right) {
    return left.high == right.high && left.low == right.low;
}

[[nodiscard]] constexpr bool operator!=(
    const CompanionBondIdentityToken& left,
    const CompanionBondIdentityToken& right) {
    return !(left == right);
}

[[nodiscard]] constexpr bool valid_bond_identity(
    const CompanionBondIdentityToken& identity) {
    return identity.high != 0 || identity.low != 0;
}

enum class CompanionAuthorizationRecordState : std::uint8_t {
    unowned = 0,
    owned = 1,
};

// Fixed persistence payload. The injected backend owns encoding, atomicity,
// rollback-resistant trusted-generation storage, and privacy at rest.
struct CompanionAuthorizationRecord {
    std::uint8_t schema_version{0};
    CompanionAuthorizationRecordState state{
        CompanionAuthorizationRecordState::unowned};
    std::uint16_t reserved{0};
    std::uint32_t generation{0};
    CompanionBondIdentityToken owner{};
};

enum class CompanionAuthorizationPersistenceError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    uncertain,
    conflict,
};

struct CompanionAuthorizationLoadResult {
    CompanionAuthorizationPersistenceError error{
        CompanionAuthorizationPersistenceError::not_ready};
    bool record_present{false};
    std::uint32_t trusted_generation{0};
    CompanionAuthorizationRecord record{};
};

struct CompanionAuthorizationCommitResult {
    CompanionAuthorizationPersistenceError error{
        CompanionAuthorizationPersistenceError::not_ready};
    CompanionAuthorizationRecord verified_record{};

    [[nodiscard]] constexpr bool committed_and_verified() const {
        return error == CompanionAuthorizationPersistenceError::none;
    }
};

class CompanionAuthorizationPersistence {
public:
    virtual ~CompanionAuthorizationPersistence() = default;

    // A present record is accepted only when its generation equals the trusted
    // rollback floor. Empty is valid only with a zero trusted generation.
    [[nodiscard]] virtual CompanionAuthorizationLoadResult load() = 0;

    // Atomically compare against expected_generation, store candidate, advance
    // the trusted floor, read back, and return the exact verified record.
    // failed guarantees no durable change; uncertain means commit state cannot
    // be proven and causes this boot policy to latch closed.
    [[nodiscard]] virtual CompanionAuthorizationCommitResult
    commit_and_verify(
        std::uint32_t expected_generation,
        const CompanionAuthorizationRecord& candidate) = 0;
};

enum class CompanionPhysicalAuthorizationAction : std::uint8_t {
    claim_owner = 1,
    revoke_owner = 2,
    replace_owner = 3,
    reset_authorization = 4,
};

// Supplied only by the local physical-input owner. Candidate is required for
// claim/replace and must be empty for revoke/reset. boot_challenge binds a
// delayed callback to this exact boot; event_token is strictly increasing for
// the boot and cannot be replayed.
struct CompanionPhysicalPresenceInput {
    CompanionPhysicalAuthorizationAction action{
        CompanionPhysicalAuthorizationAction::claim_owner};
    CompanionBondIdentityToken candidate{};
    std::uint64_t boot_challenge{0};
    std::uint64_t event_token{0};
    std::uint64_t observed_at_ms{0};
};

// All fields are trusted adapter evidence, never decoded from the phone. The
// bond authority must resolve identity from an encrypted, authenticated bond.
// The boot/session authority must mint a boot-unique challenge, a strictly
// increasing nonzero session challenge, and a private nonzero controller
// binding that is not an address, public ID, or client value.
struct CompanionControllerClaim {
    CompanionBondIdentityToken bond_identity{};
    std::uint64_t boot_challenge{0};
    std::uint64_t session_challenge{0};
    std::uint64_t controller_binding{0};
    bool link_encrypted{false};
    bool authenticated_bond{false};
};

struct CompanionAuthorizationPolicy {
    std::uint64_t physical_window_ms{30000};
};

inline constexpr std::uint64_t kCompanionMaximumPhysicalWindowMs = 30000;

enum class CompanionAuthorizationError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_policy,
    not_restored,
    already_restored,
    persistence_not_ready,
    persistence_failed,
    persistence_uncertain,
    persistence_conflict,
    persistence_record_invalid,
    persistence_rollback,
    physical_presence_required,
    physical_presence_expired,
    physical_presence_mismatch,
    physical_event_replayed,
    clock_rollback,
    link_not_encrypted,
    bond_not_authenticated,
    owner_not_claimed,
    owner_already_claimed,
    owner_mismatch,
    same_owner_replacement,
    controller_in_use,
    no_active_controller,
    wrong_controller,
    boot_challenge_mismatch,
    session_challenge_replayed,
    session_challenge_exhausted,
    generation_exhausted,
    reentrant_call,
    faulted,
};

enum class CompanionAuthorizationDisposition : std::uint8_t {
    rejected = 0,
    restored,
    claimed,
    authorized,
    duplicate_authorization,
    released,
    revoked,
    replaced,
    reset,
    physical_window_opened,
};

struct CompanionAuthorizationResult {
    CompanionAuthorizationDisposition disposition{
        CompanionAuthorizationDisposition::rejected};
    CompanionAuthorizationError error{
        CompanionAuthorizationError::invalid_argument};
    std::uint64_t controller_binding{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == CompanionAuthorizationError::none;
    }

    [[nodiscard]] constexpr bool connection_authorized() const {
        return accepted() &&
               (disposition == CompanionAuthorizationDisposition::claimed ||
                disposition ==
                    CompanionAuthorizationDisposition::authorized ||
                disposition == CompanionAuthorizationDisposition::
                    duplicate_authorization ||
                disposition == CompanionAuthorizationDisposition::replaced);
    }
};

struct CompanionAuthorizationStatus {
    bool restored{false};
    bool owner_present{false};
    bool physical_window_open{false};
    bool controller_active{false};
    bool persistence_uncertain{false};
    bool faulted{false};
    std::uint32_t owner_generation{0};
};

// Fixed-memory one-phone owner. No status/result exposes the bond token,
// physical event token, boot/session challenge, or inactive binding. The
// authority and its persistence callbacks are single-owner and must be
// externally serialized by a future target adapter. operation_active_ blocks
// synchronous callback reentry; it is deliberately not a concurrent-task
// synchronization primitive and provides no thread-safety claim. A target must
// retain exactly one authority instance for the lifetime of its boot challenge;
// recreating it under the same challenge would discard boot-local replay state.
class CompanionAuthorizationAuthority {
public:
    CompanionAuthorizationAuthority(
        CompanionAuthorizationPersistence& persistence,
        std::uint64_t boot_challenge,
        CompanionAuthorizationPolicy policy = {});

    [[nodiscard]] CompanionAuthorizationResult restore();
    [[nodiscard]] CompanionAuthorizationResult open_physical_window(
        const CompanionPhysicalPresenceInput& input);
    [[nodiscard]] CompanionAuthorizationResult claim_owner(
        const CompanionControllerClaim& claim,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionAuthorizationResult authorize_connection(
        const CompanionControllerClaim& claim,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionAuthorizationResult release_connection(
        std::uint64_t controller_binding);
    [[nodiscard]] CompanionAuthorizationResult revoke_owner(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionAuthorizationResult replace_owner(
        const CompanionControllerClaim& replacement,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionAuthorizationResult reset_authorization(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionAuthorizationStatus status(
        std::uint64_t now_ms) const;

private:
    [[nodiscard]] CompanionAuthorizationResult reject(
        CompanionAuthorizationError error) const;
    [[nodiscard]] CompanionAuthorizationError ready_error() const;
    [[nodiscard]] CompanionAuthorizationError observe_time(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionAuthorizationError validate_claim(
        const CompanionControllerClaim& claim) const;
    [[nodiscard]] CompanionAuthorizationError physical_window_error(
        CompanionPhysicalAuthorizationAction action,
        const CompanionBondIdentityToken& candidate,
        std::uint64_t now_ms) const;
    [[nodiscard]] CompanionAuthorizationResult publish_owner_change(
        CompanionAuthorizationRecord candidate,
        CompanionAuthorizationDisposition disposition,
        const CompanionControllerClaim* active_claim);
    void consume_physical_window();
    void latch_persistence_failure(
        CompanionAuthorizationPersistenceError error);

    CompanionAuthorizationPersistence& persistence_;
    CompanionAuthorizationPolicy policy_{};
    CompanionAuthorizationError construction_error_{
        CompanionAuthorizationError::none};
    std::uint64_t boot_challenge_{0};
    bool restored_{false};
    bool owner_present_{false};
    bool physical_window_open_{false};
    bool controller_active_{false};
    bool persistence_uncertain_{false};
    bool faulted_{false};
    bool operation_active_{false};
    bool time_observed_{false};
    std::uint32_t generation_{0};
    CompanionBondIdentityToken owner_{};
    CompanionPhysicalAuthorizationAction physical_action_{
        CompanionPhysicalAuthorizationAction::claim_owner};
    CompanionBondIdentityToken physical_candidate_{};
    std::uint64_t physical_event_token_{0};
    std::uint64_t last_physical_event_token_{0};
    std::uint64_t physical_opened_at_ms_{0};
    std::uint64_t last_now_ms_{0};
    std::uint64_t last_session_challenge_{0};
    std::uint64_t active_session_challenge_{0};
    std::uint64_t active_controller_binding_{0};
};

}  // namespace opentrail::companion
