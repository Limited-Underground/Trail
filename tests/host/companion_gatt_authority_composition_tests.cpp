#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/companion_gatt_authority_composition.hpp"

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

constexpr CompanionBondIdentityToken kOwnerA{0x1011121314151617ULL,
                                              0x2021222324252627ULL};
constexpr CompanionBondIdentityToken kOwnerB{0x3031323334353637ULL,
                                              0x4041424344454647ULL};

CompanionPrivateBondReference private_reference(std::uint8_t seed = 1,
                                                std::uint32_t generation = 1) {
    CompanionPrivateBondReference result{};
    for (std::size_t index = 0; index < result.value.size(); ++index) {
        result.value[index] =
            static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index));
    }
    result.bond_generation = generation;
    return result;
}

class FakeReferenceSource final : public CompanionGattPrivateBondReferenceSource {
public:
    CompanionGattPrivateBondReferenceResult result{
        CompanionGattPrivateBondReferenceError::none, private_reference()};
    PersistentCompanionGattTrustedBindingAuthority* callback{nullptr};
    bool reenter{false};
    CompanionGattTrustedBindingError callback_error{
        CompanionGattTrustedBindingError::none};
    std::uint32_t calls{0};

    CompanionGattPrivateBondReferenceResult resolve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) override {
        ++calls;
        observed_handle = connection_handle;
        observed_generation = transport_generation;
        if (reenter && callback != nullptr) {
            reenter = false;
            callback_error =
                callback->resolve(connection_handle, transport_generation).error;
        }
        return result;
    }

    std::uint16_t observed_handle{0};
    std::uint64_t observed_generation{0};
};

class FakePrf final : public CompanionBondBindingPrf {
public:
    CompanionBondBindingPrfError error{CompanionBondBindingPrfError::none};
    std::uint32_t calls{0};
    CompanionBondBindingPrfError calculate(
        const std::array<std::uint8_t,
                         kCompanionBondBindingPrfMessageBytes>& message,
        std::array<std::uint8_t, kCompanionBondBindingPrfBytes>& output)
        override {
        ++calls;
        output.fill(0);
        if (error != CompanionBondBindingPrfError::none) return error;
        for (std::size_t index = 0; index < output.size(); ++index) {
            output[index] = static_cast<std::uint8_t>(
                message[index % message.size()] ^
                message[(index + 16) % message.size()] ^
                message[(index + 32) % message.size()] ^ 0x5AU);
        }
        return CompanionBondBindingPrfError::none;
    }
};

class FakeSessionIssuer final : public CompanionGattPrivateSessionIssuer {
public:
    CompanionGattPrivateSessionResult result{
        CompanionGattPrivateSessionError::none, 101, 201, 301, 401};
    std::uint32_t calls{0};
    CompanionGattPrivateSessionContext observed{};
    CompanionGattPrivateSessionResult issue(
        const CompanionGattPrivateSessionContext& context) override {
        ++calls;
        observed = context;
        return result;
    }
};

class FakeProtectedStore final : public CompanionAuthorizationProtectedStore {
public:
    bool present{false};
    std::uint32_t floor{0};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
    CompanionAuthorizationProtectedStoreError commit_error{
        CompanionAuthorizationProtectedStoreError::none};
    bool apply_before_error{false};
    std::uint32_t commit_calls{0};
    PersistentCompanionGattAuthorizationAuthority* callback{nullptr};
    CompanionControllerClaim callback_claim{};
    bool reenter_commit{false};
    CompanionGattAuthorizationAuthorityError callback_error{
        CompanionGattAuthorizationAuthorityError::none};

    CompanionAuthorizationProtectedSnapshot load_verified() override {
        return {CompanionAuthorizationProtectedStoreError::none,
                present, floor, record};
    }
    CompanionAuthorizationProtectedSnapshot compare_commit_and_verify(
        std::uint32_t expected_generation,
        std::uint32_t new_generation,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& candidate)
        override {
        ++commit_calls;
        if (reenter_commit && callback != nullptr) {
            reenter_commit = false;
            callback_error = callback->apply_claim(
                CompanionAuthorizationPurpose::authorize_controller,
                callback_claim, 12).error;
        }
        if (expected_generation != floor) {
            return {CompanionAuthorizationProtectedStoreError::conflict,
                    present, floor, record};
        }
        if (commit_error != CompanionAuthorizationProtectedStoreError::none) {
            if (apply_before_error) {
                present = true;
                floor = new_generation;
                record = candidate;
            }
            return {commit_error, present, floor, record};
        }
        present = true;
        floor = new_generation;
        record = candidate;
        return {CompanionAuthorizationProtectedStoreError::none,
                true, floor, record};
    }
};

CompanionControllerClaim claim(CompanionBondIdentityToken owner,
                               std::uint64_t boot,
                               std::uint64_t session,
                               std::uint64_t binding) {
    return {owner, boot, session, binding, true, true};
}
CompanionPhysicalPresenceInput physical(
    CompanionPhysicalAuthorizationAction action,
    CompanionBondIdentityToken candidate,
    std::uint64_t boot,
    std::uint64_t event,
    std::uint64_t now) {
    return {action, candidate, boot, event, now};
}
void put_owner(FakeProtectedStore& store, CompanionBondIdentityToken owner) {
    const CompanionAuthorizationRecord record{
        0, CompanionAuthorizationRecordState::owned, 0, 1, owner};
    EXPECT(encode_companion_authorization_durable_record(record, store.record) ==
           CompanionAuthorizationDurableCodecError::none);
    store.present = true;
    store.floor = 1;
}
void expect_empty_binding(const CompanionGattTrustedBindingResult& result) {
    EXPECT(!valid_bond_identity(result.claim.bond_identity));
    EXPECT(result.claim.boot_challenge == 0);
    EXPECT(result.claim.session_challenge == 0);
    EXPECT(result.claim.controller_binding == 0);
    EXPECT(result.provisional_session_nonce == 0);
}

void test_binding_failures_are_redacted_and_not_cached() {
    for (std::uint8_t mode = 0; mode < 6; ++mode) {
        FakeReferenceSource source{};
        FakePrf prf{};
        CompanionBondBindingResolver resolver{prf};
        FakeSessionIssuer issuer{};
        if (mode == 0) source.result.error = CompanionGattPrivateBondReferenceError::not_ready;
        if (mode == 1) source.result.error = CompanionGattPrivateBondReferenceError::failed;
        if (mode == 2) prf.error = CompanionBondBindingPrfError::not_ready;
        if (mode == 3) prf.error = CompanionBondBindingPrfError::failed;
        if (mode == 4) issuer.result.error = CompanionGattPrivateSessionError::not_ready;
        if (mode == 5) issuer.result.error = CompanionGattPrivateSessionError::failed;
        PersistentCompanionGattTrustedBindingAuthority authority{
            source, resolver, issuer};
        const auto first = authority.resolve(7, 1);
        EXPECT(first.error ==
               ((mode == 0 || mode == 2 || mode == 4)
                    ? CompanionGattTrustedBindingError::not_ready
                    : CompanionGattTrustedBindingError::failed));
        expect_empty_binding(first);
        EXPECT(authority.resolve(7, 1).error == first.error);
        EXPECT(source.calls == 2);
    }
}

void test_binding_cache_generation_and_reentry() {
    FakeReferenceSource source{};
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    FakeSessionIssuer issuer{};
    PersistentCompanionGattTrustedBindingAuthority authority{
        source, resolver, issuer};
    const auto first = authority.resolve(7, 1);
    EXPECT(first.error == CompanionGattTrustedBindingError::none);
    EXPECT(valid_bond_identity(first.claim.bond_identity));
    EXPECT(first.claim.boot_challenge == 101);
    EXPECT(first.claim.session_challenge == 201);
    EXPECT(first.claim.controller_binding == 301);
    EXPECT(first.provisional_session_nonce == 401);
    EXPECT(!first.claim.link_encrypted && !first.claim.authenticated_bond);
    EXPECT(issuer.observed.bond_identity == first.claim.bond_identity);
    const auto cached = authority.resolve(7, 1);
    EXPECT(cached.claim.bond_identity == first.claim.bond_identity);
    EXPECT(cached.claim.controller_binding == first.claim.controller_binding);
    EXPECT(source.calls == 1 && prf.calls == 1 && issuer.calls == 1);
    EXPECT(authority.resolve(8, 1).error == CompanionGattTrustedBindingError::failed);
    issuer.result = {CompanionGattPrivateSessionError::none, 101, 202, 302, 402};
    EXPECT(authority.resolve(8, 2).claim.session_challenge == 202);
    EXPECT(authority.resolve(7, 1).error == CompanionGattTrustedBindingError::failed);

    FakeReferenceSource reentrant_source{};
    FakePrf reentrant_prf{};
    CompanionBondBindingResolver reentrant_resolver{reentrant_prf};
    FakeSessionIssuer reentrant_issuer{};
    PersistentCompanionGattTrustedBindingAuthority reentrant{
        reentrant_source, reentrant_resolver, reentrant_issuer};
    reentrant_source.callback = &reentrant;
    reentrant_source.reenter = true;
    const auto result = reentrant.resolve(9, 1);
    EXPECT(reentrant_source.callback_error == CompanionGattTrustedBindingError::failed);
    EXPECT(result.error == CompanionGattTrustedBindingError::failed);
    expect_empty_binding(result);
    EXPECT(reentrant_issuer.calls == 0);
}

void test_private_reference_is_pinned_across_downstream_retry() {
    for (std::uint8_t downstream = 0; downstream < 2; ++downstream) {
        FakeReferenceSource source{};
        FakePrf prf{};
        CompanionBondBindingResolver resolver{prf};
        FakeSessionIssuer issuer{};
        if (downstream == 0) {
            prf.error = CompanionBondBindingPrfError::not_ready;
        } else {
            issuer.result.error = CompanionGattPrivateSessionError::not_ready;
        }
        PersistentCompanionGattTrustedBindingAuthority authority{
            source, resolver, issuer};
        const auto first = authority.resolve(11, 1);
        EXPECT(first.error == CompanionGattTrustedBindingError::not_ready);
        expect_empty_binding(first);
        EXPECT(source.calls == 1);
        source.result.reference = private_reference(9, 2);
        prf.error = CompanionBondBindingPrfError::none;
        issuer.result = {CompanionGattPrivateSessionError::none,
                         101, 202, 302, 402};
        const auto changed = authority.resolve(11, 1);
        EXPECT(changed.error == CompanionGattTrustedBindingError::failed);
        expect_empty_binding(changed);
        EXPECT(source.calls == 2);
        EXPECT(issuer.calls == (downstream == 0 ? 0U : 1U));
    }
}

void test_new_generation_repair_mints_distinct_owner_token() {
    FakeReferenceSource source{};
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    FakeSessionIssuer issuer{};
    PersistentCompanionGattTrustedBindingAuthority authority{
        source, resolver, issuer};
    const auto original = authority.resolve(12, 1);
    EXPECT(original.error == CompanionGattTrustedBindingError::none);
    EXPECT(valid_bond_identity(original.claim.bond_identity));
    source.result.reference = private_reference(1, 2);
    issuer.result = {CompanionGattPrivateSessionError::none,
                     101, 202, 302, 402};
    const auto repaired = authority.resolve(12, 2);
    EXPECT(repaired.error == CompanionGattTrustedBindingError::none);
    EXPECT(valid_bond_identity(repaired.claim.bond_identity));
    EXPECT(repaired.claim.bond_identity != original.claim.bond_identity);
    EXPECT(repaired.claim.session_challenge == 202);
    EXPECT(repaired.claim.controller_binding == 302);
    EXPECT(repaired.provisional_session_nonce == 402);
    EXPECT(source.calls == 2 && prf.calls == 2 && issuer.calls == 2);
}

void test_invalid_private_session_is_never_published() {
    for (std::uint8_t field = 0; field < 4; ++field) {
        FakeReferenceSource source{};
        FakePrf prf{};
        CompanionBondBindingResolver resolver{prf};
        FakeSessionIssuer issuer{};
        if (field == 0) issuer.result.boot_challenge = 0;
        if (field == 1) issuer.result.session_challenge = 0;
        if (field == 2) issuer.result.controller_binding = 0;
        if (field == 3) issuer.result.provisional_session_nonce = 0;
        PersistentCompanionGattTrustedBindingAuthority authority{
            source, resolver, issuer};
        const auto result = authority.resolve(7, 1);
        EXPECT(result.error == CompanionGattTrustedBindingError::failed);
        expect_empty_binding(result);
    }
}

void test_first_claim_gate_reconnect_and_release() {
    FakeProtectedStore store{};
    DurableCompanionAuthorizationPersistence persistence{store};
    CompanionAuthorizationAuthority owner{persistence, 100};
    EXPECT(owner.restore().accepted());
    PersistentCompanionGattAuthorizationAuthority authority{owner};
    const auto owner_a = claim(kOwnerA, 100, 1, 501);
    const auto denied = authority.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, owner_a, 10);
    EXPECT(denied.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(denied.reason == CompanionAuthorizationDenyReason::physical_presence_required);
    EXPECT(denied.controller_binding == 0 && store.commit_calls == 0);
    EXPECT(owner.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOwnerA,
               100, 1, 11)).accepted());
    const auto accepted = authority.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, owner_a, 12);
    EXPECT(accepted.outcome == CompanionAuthorizationClaimOutcome::accepted);
    EXPECT(accepted.reason == CompanionAuthorizationDenyReason::none);
    EXPECT(accepted.controller_binding == 501 && store.commit_calls == 1);
    EXPECT(authority.release_connection(501) == CompanionGattAuthorizationAuthorityError::none);
    const auto reconnect = authority.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller,
        claim(kOwnerA, 100, 2, 502), 13);
    EXPECT(reconnect.outcome == CompanionAuthorizationClaimOutcome::accepted);
    EXPECT(reconnect.controller_binding == 502 && store.commit_calls == 1);
}

void test_wrong_phone_and_explicit_replacement() {
    FakeProtectedStore store{};
    put_owner(store, kOwnerA);
    DurableCompanionAuthorizationPersistence persistence{store};
    CompanionAuthorizationAuthority owner{persistence, 200};
    EXPECT(owner.restore().accepted());
    PersistentCompanionGattAuthorizationAuthority authority{owner};
    const auto wrong = authority.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller,
        claim(kOwnerB, 200, 1, 601), 20);
    EXPECT(wrong.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(wrong.reason == CompanionAuthorizationDenyReason::owner_state_conflict);
    EXPECT(wrong.controller_binding == 0 && store.commit_calls == 0);
    EXPECT(authority.release_connection(601) == CompanionGattAuthorizationAuthorityError::failed);
    const auto premature = authority.apply_claim(
        CompanionAuthorizationPurpose::replace_controller,
        claim(kOwnerB, 200, 2, 602), 21);
    EXPECT(premature.reason == CompanionAuthorizationDenyReason::physical_presence_required);
    EXPECT(owner.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::replace_owner, kOwnerB,
               200, 1, 22)).accepted());
    const auto replaced = authority.apply_claim(
        CompanionAuthorizationPurpose::replace_controller,
        claim(kOwnerB, 200, 3, 603), 23);
    EXPECT(replaced.outcome == CompanionAuthorizationClaimOutcome::replaced);
    EXPECT(replaced.controller_binding == 603 && store.commit_calls == 1);
    EXPECT(authority.release_connection(603) == CompanionGattAuthorizationAuthorityError::none);
    const auto stale = authority.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller,
        claim(kOwnerA, 200, 4, 604), 24);
    EXPECT(stale.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(stale.reason == CompanionAuthorizationDenyReason::owner_state_conflict);
    EXPECT(stale.controller_binding == 0);
}

void test_uncertainty_reentry_and_unrestored_results_are_private() {
    FakeProtectedStore store{};
    store.commit_error = CompanionAuthorizationProtectedStoreError::uncertain;
    store.apply_before_error = true;
    DurableCompanionAuthorizationPersistence persistence{store};
    CompanionAuthorizationAuthority owner{persistence, 300};
    EXPECT(owner.restore().accepted());
    PersistentCompanionGattAuthorizationAuthority authority{owner};
    const auto owner_a = claim(kOwnerA, 300, 1, 701);
    EXPECT(owner.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOwnerA,
               300, 1, 30)).accepted());
    const auto uncertain = authority.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, owner_a, 31);
    EXPECT(uncertain.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(uncertain.reason == CompanionAuthorizationDenyReason::persistence_unavailable);
    EXPECT(uncertain.controller_binding == 0);
    EXPECT(owner.status(31).faulted && owner.status(31).persistence_uncertain);

    FakeProtectedStore reentrant_store{};
    DurableCompanionAuthorizationPersistence reentrant_persistence{reentrant_store};
    CompanionAuthorizationAuthority reentrant_owner{reentrant_persistence, 400};
    EXPECT(reentrant_owner.restore().accepted());
    PersistentCompanionGattAuthorizationAuthority reentrant{reentrant_owner};
    const auto reentrant_claim = claim(kOwnerA, 400, 1, 801);
    EXPECT(reentrant_owner.open_physical_window(physical(
               CompanionPhysicalAuthorizationAction::claim_owner, kOwnerA,
               400, 1, 40)).accepted());
    reentrant_store.callback = &reentrant;
    reentrant_store.callback_claim = reentrant_claim;
    reentrant_store.reenter_commit = true;
    const auto outer = reentrant.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller,
        reentrant_claim, 41);
    EXPECT(reentrant_store.callback_error == CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(outer.error == CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(outer.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(outer.reason == CompanionAuthorizationDenyReason::unknown);
    EXPECT(outer.controller_binding == 0);

    FakeProtectedStore unrestored_store{};
    DurableCompanionAuthorizationPersistence unrestored_persistence{unrestored_store};
    CompanionAuthorizationAuthority unrestored_owner{unrestored_persistence, 500};
    PersistentCompanionGattAuthorizationAuthority unrestored{unrestored_owner};
    const auto unavailable = unrestored.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller,
        claim(kOwnerA, 500, 1, 901), 50);
    EXPECT(unavailable.error == CompanionGattAuthorizationAuthorityError::not_ready);
    EXPECT(unavailable.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(unavailable.reason == CompanionAuthorizationDenyReason::unknown);
    EXPECT(unavailable.controller_binding == 0);
}
}  // namespace

int main() {
    test_binding_failures_are_redacted_and_not_cached();
    test_binding_cache_generation_and_reentry();
    test_private_reference_is_pinned_across_downstream_retry();
    test_new_generation_repair_mints_distinct_owner_token();
    test_invalid_private_session_is_never_published();
    test_first_claim_gate_reconnect_and_release();
    test_wrong_phone_and_explicit_replacement();
    test_uncertainty_reentry_and_unrestored_results_are_private();
    if (failures != 0) {
        std::cerr << failures
                  << " companion GATT authority composition test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout
        << "companion GATT authority composition tests passed: 8 groups\n";
    return EXIT_SUCCESS;
}
