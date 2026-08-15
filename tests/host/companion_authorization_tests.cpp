#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "opentrail/companion_authorization.hpp"

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr std::uint64_t kBoot = 0xB007;
constexpr CompanionBondIdentityToken kOwner{0x11, 0x22};
constexpr CompanionBondIdentityToken kOther{0x33, 0x44};

CompanionControllerClaim claim(CompanionBondIdentityToken identity,
                               std::uint64_t session,
                               std::uint64_t binding) {
    return {identity, kBoot, session, binding, true, true};
}

CompanionPhysicalPresenceInput physical(
    CompanionPhysicalAuthorizationAction action,
    CompanionBondIdentityToken candidate,
    std::uint64_t event,
    std::uint64_t now) {
    return {action, candidate, kBoot, event, now};
}

class FakePersistence final : public CompanionAuthorizationPersistence {
public:
    bool present{false};
    std::uint32_t trusted_generation{0};
    CompanionAuthorizationRecord record{};
    CompanionAuthorizationPersistenceError load_error{
        CompanionAuthorizationPersistenceError::none};
    CompanionAuthorizationPersistenceError commit_error{
        CompanionAuthorizationPersistenceError::none};
    bool corrupt_readback{false};
    std::uint32_t load_calls{0};
    std::uint32_t commit_calls{0};
    CompanionAuthorizationAuthority* callback_authority{nullptr};
    bool reenter_load{false};
    bool reenter_commit{false};
    CompanionAuthorizationError callback_error{
        CompanionAuthorizationError::none};

    CompanionAuthorizationLoadResult load() override {
        ++load_calls;
        if (reenter_load && callback_authority != nullptr) {
            reenter_load = false;
            callback_error = callback_authority->restore().error;
        }
        return {load_error, present, trusted_generation, record};
    }

    CompanionAuthorizationCommitResult commit_and_verify(
        std::uint32_t expected_generation,
        const CompanionAuthorizationRecord& candidate) override {
        ++commit_calls;
        if (reenter_commit && callback_authority != nullptr) {
            reenter_commit = false;
            callback_error =
                callback_authority->release_connection(0xA1).error;
        }
        if (commit_error != CompanionAuthorizationPersistenceError::none) {
            return {commit_error, {}};
        }
        if (expected_generation != trusted_generation) {
            return {CompanionAuthorizationPersistenceError::conflict, {}};
        }
        present = true;
        trusted_generation = candidate.generation;
        record = candidate;
        CompanionAuthorizationRecord verified = record;
        if (corrupt_readback) {
            ++verified.generation;
        }
        return {CompanionAuthorizationPersistenceError::none, verified};
    }
};

void restore_empty(CompanionAuthorizationAuthority& authority) {
    const auto result = authority.restore();
    EXPECT(result.accepted());
    EXPECT(result.disposition == CompanionAuthorizationDisposition::restored);
}

void open_claim(CompanionAuthorizationAuthority& authority,
                CompanionBondIdentityToken owner = kOwner,
                std::uint64_t event = 1,
                std::uint64_t now = 100) {
    const auto result = authority.open_physical_window(physical(
        CompanionPhysicalAuthorizationAction::claim_owner, owner, event, now));
    EXPECT(result.accepted());
}

void establish_owner(FakePersistence& persistence,
                     CompanionAuthorizationAuthority& authority,
                     std::uint64_t session = 1,
                     std::uint64_t binding = 0xA1) {
    restore_empty(authority);
    open_claim(authority);
    const auto result = authority.claim_owner(
        claim(kOwner, session, binding), 101);
    EXPECT(result.accepted());
    EXPECT(result.connection_authorized());
    EXPECT(persistence.commit_calls == 1);
}

void test_claim_requires_physical_secure_authenticated_bond() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    restore_empty(authority);

    EXPECT(authority.claim_owner(claim(kOwner, 1, 1), 10).error ==
           CompanionAuthorizationError::physical_presence_required);
    open_claim(authority, kOwner, 1, 10);
    auto insecure = claim(kOwner, 1, 1);
    insecure.link_encrypted = false;
    EXPECT(authority.claim_owner(insecure, 11).error ==
           CompanionAuthorizationError::link_not_encrypted);
    auto unauthenticated = claim(kOwner, 1, 1);
    unauthenticated.authenticated_bond = false;
    EXPECT(authority.claim_owner(unauthenticated, 11).error ==
           CompanionAuthorizationError::bond_not_authenticated);
    EXPECT(persistence.commit_calls == 0);
    EXPECT(authority.claim_owner(claim(kOwner, 1, 1), 11).accepted());
}

void test_duplicate_release_and_session_replay() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority, 7, 0xA1);

    const auto duplicate =
        authority.authorize_connection(claim(kOwner, 7, 0xA1), 102);
    EXPECT(duplicate.accepted());
    EXPECT(duplicate.disposition ==
           CompanionAuthorizationDisposition::duplicate_authorization);
    EXPECT(authority.release_connection(0xA2).error ==
           CompanionAuthorizationError::wrong_controller);
    EXPECT(authority.release_connection(0xA1).accepted());
    EXPECT(authority.authorize_connection(claim(kOwner, 7, 0xA1), 103).error ==
           CompanionAuthorizationError::session_challenge_replayed);
    EXPECT(authority.authorize_connection(claim(kOwner, 8, 0xA2), 103)
               .accepted());
}

void test_one_controller_and_one_owner_only() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority);

    EXPECT(authority.authorize_connection(claim(kOwner, 2, 0xA2), 102).error ==
           CompanionAuthorizationError::controller_in_use);
    EXPECT(authority.authorize_connection(claim(kOther, 2, 0xB1), 102).error ==
           CompanionAuthorizationError::owner_mismatch);
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOther, 2,
               102)).error ==
           CompanionAuthorizationError::owner_already_claimed);
}

void test_physical_window_expiry_and_exact_candidate() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(
        persistence, kBoot, CompanionAuthorizationPolicy{20});
    restore_empty(authority);
    open_claim(authority, kOwner, 1, 100);
    EXPECT(authority.claim_owner(claim(kOther, 1, 1), 119).error ==
           CompanionAuthorizationError::physical_presence_mismatch);
    EXPECT(authority.claim_owner(claim(kOwner, 1, 1), 120).error ==
           CompanionAuthorizationError::physical_presence_expired);
    EXPECT(!authority.status(120).physical_window_open);
    EXPECT(persistence.commit_calls == 0);
}

void test_boot_and_physical_event_replay_are_rejected() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    restore_empty(authority);
    auto wrong_boot = physical(
        CompanionPhysicalAuthorizationAction::claim_owner, kOwner, 1, 10);
    wrong_boot.boot_challenge = kBoot + 1;
    EXPECT(authority.open_physical_window(wrong_boot).error ==
           CompanionAuthorizationError::boot_challenge_mismatch);
    open_claim(authority, kOwner, 2, 10);
    auto invalid_action = physical(
        CompanionPhysicalAuthorizationAction::claim_owner, kOwner, 3, 11);
    invalid_action.action =
        static_cast<CompanionPhysicalAuthorizationAction>(0xFF);
    invalid_action.candidate = {};
    EXPECT(authority.open_physical_window(invalid_action).error ==
           CompanionAuthorizationError::invalid_argument);
    EXPECT(authority.status(11).physical_window_open);
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOwner, 3,
               11)).accepted());
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOwner, 2,
               11)).error ==
           CompanionAuthorizationError::physical_event_replayed);
    auto stale_claim = claim(kOwner, 1, 1);
    stale_claim.boot_challenge = kBoot + 1;
    EXPECT(authority.claim_owner(stale_claim, 11).error ==
           CompanionAuthorizationError::boot_challenge_mismatch);
    EXPECT(authority.claim_owner(claim(kOwner, 1, 1), 11).accepted());
}

void test_revoke_persists_tombstone_across_reboot() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority);
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::revoke_owner, {}, 2,
               110)).accepted());
    EXPECT(authority.revoke_owner(111).accepted());
    EXPECT(!authority.status(111).owner_present);
    EXPECT(persistence.record.state ==
           CompanionAuthorizationRecordState::unowned);
    EXPECT(persistence.record.generation == 2);

    CompanionAuthorizationAuthority rebooted(persistence, kBoot + 1);
    EXPECT(rebooted.restore().accepted());
    EXPECT(!rebooted.status(0).owner_present);
    auto current_boot_claim = claim(kOwner, 1, 1);
    current_boot_claim.boot_challenge = kBoot + 1;
    EXPECT(rebooted.authorize_connection(current_boot_claim, 0).error ==
           CompanionAuthorizationError::owner_not_claimed);
}

void test_explicit_replace_atomically_displaces_old_owner() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority);
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::replace_owner, kOther, 2,
               110)).accepted());
    const auto replacement =
        authority.replace_owner(claim(kOther, 2, 0xB1), 111);
    EXPECT(replacement.accepted());
    EXPECT(replacement.disposition ==
           CompanionAuthorizationDisposition::replaced);
    EXPECT(replacement.controller_binding == 0xB1);
    EXPECT(persistence.record.owner == kOther);
    EXPECT(authority.authorize_connection(claim(kOwner, 2, 0xA2), 112).error ==
           CompanionAuthorizationError::owner_mismatch);
}

void test_session_challenge_never_rolls_back_on_owner_change() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority, 8, 0xA1);
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::replace_owner, kOther, 2,
               110)).accepted());
    EXPECT(authority.replace_owner(claim(kOther, 8, 0xB1), 111).error ==
           CompanionAuthorizationError::session_challenge_replayed);
    EXPECT(authority.replace_owner(claim(kOther, 9, 0xB1), 111).accepted());
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::revoke_owner, {}, 3,
               112)).accepted());
    EXPECT(authority.revoke_owner(113).accepted());
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOwner, 4,
               114)).accepted());
    EXPECT(authority.claim_owner(claim(kOwner, 9, 0xA2), 115).error ==
           CompanionAuthorizationError::session_challenge_replayed);
    EXPECT(authority.claim_owner(claim(kOwner, 10, 0xA2), 115).accepted());
}

void test_persistence_failure_and_uncertainty_fail_closed() {
    for (const auto error : {
             CompanionAuthorizationPersistenceError::not_ready,
             CompanionAuthorizationPersistenceError::failed,
             CompanionAuthorizationPersistenceError::uncertain,
             CompanionAuthorizationPersistenceError::conflict}) {
        FakePersistence persistence;
        CompanionAuthorizationAuthority authority(persistence, kBoot);
        restore_empty(authority);
        open_claim(authority);
        persistence.commit_error = error;
        EXPECT(!authority.claim_owner(claim(kOwner, 1, 1), 101).accepted());
        const auto status = authority.status(101);
        EXPECT(status.faulted);
        EXPECT(!status.owner_present);
        EXPECT(!status.controller_active);
        EXPECT(!status.physical_window_open);
        EXPECT(status.persistence_uncertain ==
               (error == CompanionAuthorizationPersistenceError::uncertain ||
                error == CompanionAuthorizationPersistenceError::conflict));
        EXPECT(authority.authorize_connection(claim(kOwner, 2, 2), 102).error ==
               CompanionAuthorizationError::faulted);
    }
}

void test_load_retry_uncertainty_and_rollback() {
    FakePersistence transient;
    transient.load_error = CompanionAuthorizationPersistenceError::not_ready;
    CompanionAuthorizationAuthority retryable(transient, kBoot);
    EXPECT(retryable.restore().error ==
           CompanionAuthorizationError::persistence_not_ready);
    transient.load_error = CompanionAuthorizationPersistenceError::none;
    EXPECT(retryable.restore().accepted());

    FakePersistence uncertain;
    uncertain.load_error = CompanionAuthorizationPersistenceError::uncertain;
    CompanionAuthorizationAuthority closed(uncertain, kBoot);
    EXPECT(closed.restore().error ==
           CompanionAuthorizationError::persistence_uncertain);
    uncertain.load_error = CompanionAuthorizationPersistenceError::none;
    EXPECT(closed.restore().error == CompanionAuthorizationError::faulted);

    FakePersistence rollback;
    rollback.present = true;
    rollback.trusted_generation = 4;
    rollback.record = {0, CompanionAuthorizationRecordState::owned, 0, 3,
                       kOwner};
    CompanionAuthorizationAuthority rejected(rollback, kBoot);
    EXPECT(rejected.restore().error ==
           CompanionAuthorizationError::persistence_rollback);
    EXPECT(rejected.status(0).persistence_uncertain);
}

void test_verified_readback_is_required_before_publication() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    restore_empty(authority);
    open_claim(authority);
    persistence.corrupt_readback = true;
    EXPECT(authority.claim_owner(claim(kOwner, 1, 1), 101).error ==
           CompanionAuthorizationError::persistence_record_invalid);
    const auto status = authority.status(101);
    EXPECT(!status.owner_present);
    EXPECT(status.faulted);
    EXPECT(status.persistence_uncertain);
}

void test_reset_is_physical_and_persisted_even_when_unowned() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    restore_empty(authority);
    EXPECT(authority.reset_authorization(10).error ==
           CompanionAuthorizationError::physical_presence_required);
    EXPECT(authority.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::reset_authorization, {},
               1, 10)).accepted());
    EXPECT(authority.reset_authorization(11).accepted());
    EXPECT(persistence.record.generation == 1);
    EXPECT(persistence.record.state ==
           CompanionAuthorizationRecordState::unowned);
}

void test_clock_rollback_latches_closed() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority);
    EXPECT(authority.authorize_connection(claim(kOwner, 1, 0xA1), 90).error ==
           CompanionAuthorizationError::clock_rollback);
    const auto status = authority.status(90);
    EXPECT(status.faulted);
    EXPECT(!status.controller_active);
}

void test_persistence_callbacks_cannot_reenter() {
    FakePersistence persistence;
    CompanionAuthorizationAuthority authority(persistence, kBoot);
    persistence.callback_authority = &authority;
    persistence.reenter_load = true;
    EXPECT(authority.restore().accepted());
    EXPECT(persistence.callback_error ==
           CompanionAuthorizationError::reentrant_call);
    open_claim(authority);
    persistence.reenter_commit = true;
    EXPECT(authority.claim_owner(claim(kOwner, 1, 0xA1), 101).accepted());
    EXPECT(persistence.callback_error ==
           CompanionAuthorizationError::reentrant_call);
    EXPECT(authority.status(101).controller_active);
}

void test_generation_and_session_exhaustion() {
    FakePersistence generation;
    generation.present = true;
    generation.trusted_generation =
        std::numeric_limits<std::uint32_t>::max();
    generation.record = {0, CompanionAuthorizationRecordState::unowned, 0,
                         std::numeric_limits<std::uint32_t>::max(), {}};
    CompanionAuthorizationAuthority authority(generation, kBoot);
    EXPECT(authority.restore().accepted());
    open_claim(authority);
    EXPECT(authority.claim_owner(claim(kOwner, 1, 1), 101).error ==
           CompanionAuthorizationError::generation_exhausted);

    FakePersistence session;
    CompanionAuthorizationAuthority session_authority(session, kBoot);
    establish_owner(session, session_authority,
                    std::numeric_limits<std::uint64_t>::max(), 1);
    EXPECT(session_authority.release_connection(1).accepted());
    EXPECT(session_authority.authorize_connection(claim(kOwner, 1, 2), 102)
               .error ==
           CompanionAuthorizationError::session_challenge_exhausted);
}

void test_fixed_redacted_status_and_invalid_construction() {
    static_assert(std::is_trivially_copyable_v<CompanionBondIdentityToken>);
    static_assert(std::is_trivially_copyable_v<CompanionAuthorizationRecord>);
    static_assert(std::is_trivially_copyable_v<CompanionAuthorizationResult>);
    static_assert(std::is_trivially_copyable_v<CompanionAuthorizationStatus>);
    static_assert(sizeof(CompanionAuthorizationStatus) <= 16);
    static_assert(sizeof(CompanionAuthorizationAuthority) <= 192);

    FakePersistence persistence;
    CompanionAuthorizationAuthority no_boot(persistence, 0);
    EXPECT(no_boot.restore().error ==
           CompanionAuthorizationError::invalid_argument);
    CompanionAuthorizationAuthority no_window(
        persistence, kBoot, CompanionAuthorizationPolicy{0});
    EXPECT(no_window.restore().error ==
           CompanionAuthorizationError::invalid_policy);
    CompanionAuthorizationAuthority excessive_window(
        persistence, kBoot,
        CompanionAuthorizationPolicy{kCompanionMaximumPhysicalWindowMs + 1});
    EXPECT(excessive_window.restore().error ==
           CompanionAuthorizationError::invalid_policy);
    CompanionAuthorizationAuthority maximum_window(
        persistence, kBoot,
        CompanionAuthorizationPolicy{kCompanionMaximumPhysicalWindowMs});
    EXPECT(maximum_window.restore().accepted());
    FakePersistence maximum_value_persistence;
    CompanionAuthorizationAuthority maximum_value_window(
        maximum_value_persistence, kBoot,
        CompanionAuthorizationPolicy{
            std::numeric_limits<std::uint64_t>::max()});
    EXPECT(maximum_value_window.restore().error ==
           CompanionAuthorizationError::invalid_policy);

    CompanionAuthorizationAuthority authority(persistence, kBoot);
    establish_owner(persistence, authority);
    const auto status = authority.status(101);
    EXPECT(status.restored);
    EXPECT(status.owner_present);
    EXPECT(status.owner_generation == 1);
    EXPECT(status.controller_active);
}

}  // namespace

int main() {
    test_claim_requires_physical_secure_authenticated_bond();
    test_duplicate_release_and_session_replay();
    test_one_controller_and_one_owner_only();
    test_physical_window_expiry_and_exact_candidate();
    test_boot_and_physical_event_replay_are_rejected();
    test_revoke_persists_tombstone_across_reboot();
    test_explicit_replace_atomically_displaces_old_owner();
    test_session_challenge_never_rolls_back_on_owner_change();
    test_persistence_failure_and_uncertainty_fail_closed();
    test_load_retry_uncertainty_and_rollback();
    test_verified_readback_is_required_before_publication();
    test_reset_is_physical_and_persisted_even_when_unowned();
    test_clock_rollback_latches_closed();
    test_persistence_callbacks_cannot_reenter();
    test_generation_and_session_exhaustion();
    test_fixed_redacted_status_and_invalid_construction();

    if (failures != 0) {
        std::cerr << failures
                  << " companion authorization assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 16 companion authorization scenario groups\n";
    return EXIT_SUCCESS;
}
