#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/companion_authorization_persistence.hpp"

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

constexpr CompanionBondIdentityToken kOwner{0x1122334455667788ULL,
                                             0x8877665544332211ULL};

CompanionAuthorizationRecord owned(std::uint32_t generation = 1) {
    return {0, CompanionAuthorizationRecordState::owned, 0, generation,
            kOwner};
}

CompanionAuthorizationRecord tombstone(std::uint32_t generation) {
    return {0, CompanionAuthorizationRecordState::unowned, 0, generation, {}};
}

class FakeProtectedStore final : public CompanionAuthorizationProtectedStore {
public:
    bool present{false};
    std::uint32_t floor{0};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
    CompanionAuthorizationProtectedStoreError load_error{
        CompanionAuthorizationProtectedStoreError::none};
    CompanionAuthorizationProtectedStoreError commit_error{
        CompanionAuthorizationProtectedStoreError::none};
    bool apply_before_error{false};
    bool corrupt_readback{false};
    bool wrong_floor_readback{false};
    bool drop_record_readback{false};
    bool reenter_load{false};
    bool reenter_commit{false};
    DurableCompanionAuthorizationPersistence* callback{nullptr};
    CompanionAuthorizationPersistenceError callback_error{
        CompanionAuthorizationPersistenceError::none};
    std::uint32_t load_calls{0};
    std::uint32_t commit_calls{0};

    CompanionAuthorizationProtectedSnapshot load_verified() override {
        ++load_calls;
        if (reenter_load && callback != nullptr) {
            reenter_load = false;
            callback_error = callback->load().error;
        }
        return {load_error, present, floor, record};
    }

    CompanionAuthorizationProtectedSnapshot compare_commit_and_verify(
        std::uint32_t expected_generation,
        std::uint32_t new_generation,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>&
            candidate) override {
        ++commit_calls;
        if (reenter_commit && callback != nullptr) {
            reenter_commit = false;
            callback_error = callback->load().error;
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
        auto returned_record = record;
        if (corrupt_readback) {
            returned_record[12] ^= 0x80U;
        }
        return {CompanionAuthorizationProtectedStoreError::none,
                drop_record_readback ? false : true,
                wrong_floor_readback ? new_generation + 1 : new_generation,
                returned_record};
    }
};

class FakePrf final : public CompanionBondBindingPrf {
public:
    CompanionBondBindingPrfError error{CompanionBondBindingPrfError::none};
    bool zero_output{false};
    bool reenter{false};
    CompanionBondBindingResolver* callback{nullptr};
    CompanionPrivateBondReference callback_reference{};
    CompanionBondBindingError callback_error{CompanionBondBindingError::none};
    std::uint32_t calls{0};

    CompanionBondBindingPrfError calculate(
        const std::array<std::uint8_t,
                         kCompanionBondBindingPrfMessageBytes>& message,
        std::array<std::uint8_t, kCompanionBondBindingPrfBytes>& output)
        override {
        ++calls;
        output.fill(0xD3U);
        if (reenter && callback != nullptr) {
            reenter = false;
            callback_error = callback->resolve(callback_reference).error;
        }
        if (error != CompanionBondBindingPrfError::none) {
            return error;
        }
        output.fill(0);
        if (!zero_output) {
            for (std::size_t index = 0; index < output.size(); ++index) {
                output[index] = static_cast<std::uint8_t>(
                    message[index % message.size()] ^
                    message[(index + 17) % message.size()] ^
                    static_cast<std::uint8_t>(0xA5U + index));
            }
        }
        return CompanionBondBindingPrfError::none;
    }
};

CompanionPrivateBondReference reference(std::uint8_t seed = 1,
                                        std::uint32_t generation = 1) {
    CompanionPrivateBondReference value{};
    for (std::size_t index = 0; index < value.value.size(); ++index) {
        value.value[index] =
            static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index));
    }
    value.bond_generation = generation;
    return value;
}

CompanionPhysicalPresenceInput physical(
    CompanionPhysicalAuthorizationAction action,
    CompanionBondIdentityToken candidate,
    std::uint64_t boot,
    std::uint64_t event,
    std::uint64_t now) {
    return {action, candidate, boot, event, now};
}

CompanionControllerClaim claim(CompanionBondIdentityToken owner,
                               std::uint64_t boot,
                               std::uint64_t session,
                               std::uint64_t binding) {
    return {owner, boot, session, binding, true, true};
}

void put(FakeProtectedStore& store,
         const CompanionAuthorizationRecord& value) {
    EXPECT(encode_companion_authorization_durable_record(value, store.record) ==
           CompanionAuthorizationDurableCodecError::none);
    store.present = true;
    store.floor = value.generation;
}

void test_exact_codec_vectors_and_tombstone() {
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> bytes{};
    EXPECT(encode_companion_authorization_durable_record(owned(), bytes) ==
           CompanionAuthorizationDurableCodecError::none);
    const std::array<std::uint8_t,
                     kCompanionAuthorizationDurableRecordBytes> expected{
        0x4F, 0x41, 0x50, 0x30, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55,
        0x44, 0x33, 0x22, 0x11, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x2B, 0xE2, 0xEE, 0x04};
    EXPECT(bytes == expected);
    const auto decoded = decode_companion_authorization_durable_record(bytes);
    EXPECT(decoded.decoded());
    EXPECT(decoded.record.owner == kOwner);

    EXPECT(encode_companion_authorization_durable_record(tombstone(2), bytes) ==
           CompanionAuthorizationDurableCodecError::none);
    const auto decoded_tombstone =
        decode_companion_authorization_durable_record(bytes);
    EXPECT(decoded_tombstone.decoded());
    EXPECT(decoded_tombstone.record.state ==
           CompanionAuthorizationRecordState::unowned);
    EXPECT(!valid_bond_identity(decoded_tombstone.record.owner));
}

void test_codec_rejects_malformed_and_incoherent_records() {
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> bytes{};
    auto invalid = owned();
    invalid.reserved = 1;
    EXPECT(encode_companion_authorization_durable_record(invalid, bytes) ==
           CompanionAuthorizationDurableCodecError::invalid_record);
    invalid = owned();
    invalid.owner = {};
    EXPECT(encode_companion_authorization_durable_record(invalid, bytes) ==
           CompanionAuthorizationDurableCodecError::invalid_record);
    invalid = tombstone(1);
    invalid.owner = kOwner;
    EXPECT(encode_companion_authorization_durable_record(invalid, bytes) ==
           CompanionAuthorizationDurableCodecError::invalid_record);

    EXPECT(encode_companion_authorization_durable_record(owned(), bytes) ==
           CompanionAuthorizationDurableCodecError::none);
    auto malformed = bytes;
    malformed[0] = 0;
    EXPECT(decode_companion_authorization_durable_record(malformed).error ==
           CompanionAuthorizationDurableCodecError::invalid_magic);
    malformed = bytes;
    malformed[4] = 1;
    EXPECT(decode_companion_authorization_durable_record(malformed).error ==
           CompanionAuthorizationDurableCodecError::unsupported_version);
    malformed = bytes;
    malformed[6] = 1;
    EXPECT(decode_companion_authorization_durable_record(malformed).error ==
           CompanionAuthorizationDurableCodecError::invalid_record);
    malformed = bytes;
    malformed[12] ^= 1;
    EXPECT(decode_companion_authorization_durable_record(malformed).error ==
           CompanionAuthorizationDurableCodecError::integrity_failure);
}

void test_empty_restore_and_exact_owner_reboot() {
    FakeProtectedStore store{};
    DurableCompanionAuthorizationPersistence persistence{store};
    auto loaded = persistence.load();
    EXPECT(loaded.error == CompanionAuthorizationPersistenceError::none);
    EXPECT(!loaded.record_present);
    EXPECT(loaded.trusted_generation == 0);

    const auto committed = persistence.commit_and_verify(0, owned());
    EXPECT(committed.committed_and_verified());
    DurableCompanionAuthorizationPersistence rebooted{store};
    loaded = rebooted.load();
    EXPECT(loaded.error == CompanionAuthorizationPersistenceError::none);
    EXPECT(loaded.record_present);
    EXPECT(loaded.trusted_generation == 1);
    EXPECT(loaded.record.owner == kOwner);
}

void test_tombstone_survives_reboot() {
    FakeProtectedStore store{};
    put(store, owned());
    DurableCompanionAuthorizationPersistence persistence{store};
    EXPECT(persistence.commit_and_verify(1, tombstone(2)).
           committed_and_verified());
    DurableCompanionAuthorizationPersistence rebooted{store};
    const auto loaded = rebooted.load();
    EXPECT(loaded.error == CompanionAuthorizationPersistenceError::none);
    EXPECT(loaded.record_present);
    EXPECT(loaded.record.generation == 2);
    EXPECT(loaded.record.state == CompanionAuthorizationRecordState::unowned);
}

void test_rollback_and_malformed_load_fail_uncertain() {
    FakeProtectedStore missing{};
    missing.floor = 2;
    DurableCompanionAuthorizationPersistence missing_persistence{missing};
    EXPECT(missing_persistence.load().error ==
           CompanionAuthorizationPersistenceError::uncertain);

    FakeProtectedStore behind{};
    put(behind, owned(1));
    behind.floor = 2;
    DurableCompanionAuthorizationPersistence behind_persistence{behind};
    EXPECT(behind_persistence.load().error ==
           CompanionAuthorizationPersistenceError::uncertain);

    FakeProtectedStore corrupt{};
    put(corrupt, owned(1));
    corrupt.record[20] ^= 1;
    DurableCompanionAuthorizationPersistence corrupt_persistence{corrupt};
    EXPECT(corrupt_persistence.load().error ==
           CompanionAuthorizationPersistenceError::uncertain);
}

void test_stale_compare_and_invalid_candidate_do_not_mutate() {
    FakeProtectedStore store{};
    put(store, owned(2));
    const auto before = store.record;
    DurableCompanionAuthorizationPersistence persistence{store};
    EXPECT(persistence.commit_and_verify(1, tombstone(2)).error ==
           CompanionAuthorizationPersistenceError::conflict);
    EXPECT(store.floor == 2);
    EXPECT(store.record == before);
    auto invalid = tombstone(3);
    invalid.owner = kOwner;
    EXPECT(persistence.commit_and_verify(2, invalid).error ==
           CompanionAuthorizationPersistenceError::failed);
    EXPECT(store.commit_calls == 1);
    EXPECT(store.record == before);
}

void test_prewrite_failure_guarantees_no_mutation() {
    FakeProtectedStore store{};
    store.commit_error = CompanionAuthorizationProtectedStoreError::failed;
    DurableCompanionAuthorizationPersistence persistence{store};
    EXPECT(persistence.commit_and_verify(0, owned()).error ==
           CompanionAuthorizationPersistenceError::failed);
    EXPECT(!store.present);
    EXPECT(store.floor == 0);
}

void test_postwrite_failure_is_uncertain_and_reboot_resolves_exact_state() {
    FakeProtectedStore store{};
    store.commit_error = CompanionAuthorizationProtectedStoreError::uncertain;
    store.apply_before_error = true;
    DurableCompanionAuthorizationPersistence persistence{store};
    EXPECT(persistence.commit_and_verify(0, owned()).error ==
           CompanionAuthorizationPersistenceError::uncertain);
    EXPECT(store.present);
    EXPECT(store.floor == 1);
    store.commit_error = CompanionAuthorizationProtectedStoreError::none;
    DurableCompanionAuthorizationPersistence rebooted{store};
    EXPECT(rebooted.load().record.owner == kOwner);
}

void test_inexact_readback_is_uncertain() {
    for (std::uint8_t mode = 0; mode < 3; ++mode) {
        FakeProtectedStore store{};
        store.corrupt_readback = mode == 0;
        store.wrong_floor_readback = mode == 1;
        store.drop_record_readback = mode == 2;
        DurableCompanionAuthorizationPersistence persistence{store};
        EXPECT(persistence.commit_and_verify(0, owned()).error ==
               CompanionAuthorizationPersistenceError::uncertain);
    }
}

void test_storage_callback_reentry_fails_uncertain() {
    FakeProtectedStore load_store{};
    DurableCompanionAuthorizationPersistence load_persistence{load_store};
    load_store.callback = &load_persistence;
    load_store.reenter_load = true;
    EXPECT(load_persistence.load().error ==
           CompanionAuthorizationPersistenceError::uncertain);
    EXPECT(load_store.callback_error ==
           CompanionAuthorizationPersistenceError::uncertain);

    FakeProtectedStore commit_store{};
    DurableCompanionAuthorizationPersistence commit_persistence{commit_store};
    commit_store.callback = &commit_persistence;
    commit_store.reenter_commit = true;
    EXPECT(commit_persistence.commit_and_verify(0, owned()).error ==
           CompanionAuthorizationPersistenceError::uncertain);
}

void test_bond_binding_is_stable_private_and_repair_distinct() {
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    const auto first = resolver.resolve(reference(1, 1));
    const auto reboot = resolver.resolve(reference(1, 1));
    const auto repaired = resolver.resolve(reference(1, 2));
    const auto other = resolver.resolve(reference(2, 1));
    EXPECT(first.resolved());
    EXPECT(first.token == reboot.token);
    EXPECT(first.token != repaired.token);
    EXPECT(first.token != other.token);
    EXPECT(prf.calls == 4);
}

void test_bond_binding_fail_closed_edges() {
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    EXPECT(resolver.resolve({}).error ==
           CompanionBondBindingError::invalid_argument);
    EXPECT(prf.calls == 0);
    prf.error = CompanionBondBindingPrfError::not_ready;
    EXPECT(resolver.resolve(reference()).error ==
           CompanionBondBindingError::not_ready);
    prf.error = CompanionBondBindingPrfError::failed;
    EXPECT(resolver.resolve(reference()).error ==
           CompanionBondBindingError::derivation_failed);
    prf.error = CompanionBondBindingPrfError::none;
    prf.zero_output = true;
    EXPECT(resolver.resolve(reference()).error ==
           CompanionBondBindingError::invalid_output);
}

void test_bond_binding_callback_reentry_rejects_outer_result() {
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    prf.callback = &resolver;
    prf.callback_reference = reference();
    prf.reenter = true;
    EXPECT(resolver.resolve(reference()).error ==
           CompanionBondBindingError::reentrant_call);
    EXPECT(prf.callback_error == CompanionBondBindingError::reentrant_call);
}

void test_target_admission_requires_every_protected_prerequisite() {
    CompanionAuthorizationTargetSecurityEvidence evidence{};
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               nvs_encryption_not_configured);
    evidence.nvs_encryption_configured = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               protected_nvs_not_verified);
    evidence.protected_nvs_initialized_and_verified = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               nvs_hmac_key_protection_not_configured);
    evidence.nvs_hmac_key_protection_configured = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               nvs_hmac_key_not_verified);
    evidence.nvs_hmac_key_provisioned_and_usable = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               private_bond_store_missing);
    evidence.private_bond_store_available = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               separate_binding_prf_key_missing);
    evidence.separate_binding_prf_key_provisioned = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               atomic_record_floor_missing);
    evidence.atomic_record_and_floor_backend = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::rollback_floor_missing);
    evidence.independent_rollback_floor = true;
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::none);
}

void test_authority_owner_commit_and_reboot_authorization() {
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    const auto owner_a = resolver.resolve(reference(1, 1));
    const auto owner_b = resolver.resolve(reference(2, 1));
    EXPECT(owner_a.resolved());
    EXPECT(owner_b.resolved());

    FakeProtectedStore store{};
    {
        DurableCompanionAuthorizationPersistence persistence{store};
        CompanionAuthorizationAuthority authority{persistence, 100};
        EXPECT(authority.restore().accepted());
        EXPECT(authority.open_physical_window(physical(
                   CompanionPhysicalAuthorizationAction::claim_owner,
                   owner_a.token, 100, 1, 10)).accepted());
        const auto claimed =
            authority.claim_owner(claim(owner_a.token, 100, 1, 11), 11);
        EXPECT(claimed.connection_authorized());
        EXPECT(authority.status(11).owner_generation == 1);
    }
    {
        DurableCompanionAuthorizationPersistence persistence{store};
        CompanionAuthorizationAuthority authority{persistence, 200};
        EXPECT(authority.restore().accepted());
        EXPECT(authority.authorize_connection(
                   claim(owner_a.token, 200, 1, 21), 20).
                   connection_authorized());
        EXPECT(authority.release_connection(21).accepted());
        EXPECT(authority.authorize_connection(
                   claim(owner_b.token, 200, 2, 22), 21).error ==
               CompanionAuthorizationError::owner_mismatch);
    }
}

void test_physical_replace_and_tombstones_survive_reboot() {
    FakePrf prf{};
    CompanionBondBindingResolver resolver{prf};
    const auto owner_a = resolver.resolve(reference(1, 1)).token;
    const auto owner_b = resolver.resolve(reference(1, 2)).token;
    EXPECT(owner_a != owner_b);
    FakeProtectedStore store{};
    put(store, {0, CompanionAuthorizationRecordState::owned, 0, 1, owner_a});

    {
        DurableCompanionAuthorizationPersistence persistence{store};
        CompanionAuthorizationAuthority authority{persistence, 300};
        EXPECT(authority.restore().accepted());
        EXPECT(authority.open_physical_window(physical(
                   CompanionPhysicalAuthorizationAction::replace_owner,
                   owner_b, 300, 1, 30)).accepted());
        EXPECT(authority.replace_owner(
                   claim(owner_b, 300, 1, 31), 31).
                   connection_authorized());
    }
    {
        DurableCompanionAuthorizationPersistence persistence{store};
        CompanionAuthorizationAuthority authority{persistence, 400};
        EXPECT(authority.restore().accepted());
        EXPECT(authority.authorize_connection(
                   claim(owner_a, 400, 1, 41), 40).error ==
               CompanionAuthorizationError::owner_mismatch);
        EXPECT(authority.authorize_connection(
                   claim(owner_b, 400, 2, 42), 41).
                   connection_authorized());
        EXPECT(authority.release_connection(42).accepted());
        EXPECT(authority.open_physical_window(physical(
                   CompanionPhysicalAuthorizationAction::revoke_owner, {},
                   400, 1, 42)).accepted());
        EXPECT(authority.revoke_owner(43).accepted());
    }
    {
        DurableCompanionAuthorizationPersistence persistence{store};
        CompanionAuthorizationAuthority authority{persistence, 500};
        EXPECT(authority.restore().accepted());
        EXPECT(!authority.status(50).owner_present);
        EXPECT(authority.status(50).owner_generation == 3);
        EXPECT(authority.authorize_connection(
                   claim(owner_b, 500, 1, 51), 50).error ==
               CompanionAuthorizationError::owner_not_claimed);
        EXPECT(authority.open_physical_window(physical(
                   CompanionPhysicalAuthorizationAction::reset_authorization,
                   {}, 500, 1, 51)).accepted());
        EXPECT(authority.reset_authorization(52).accepted());
    }
    DurableCompanionAuthorizationPersistence final_persistence{store};
    CompanionAuthorizationAuthority final_authority{final_persistence, 600};
    EXPECT(final_authority.restore().accepted());
    EXPECT(!final_authority.status(60).owner_present);
    EXPECT(final_authority.status(60).owner_generation == 4);
}

void test_authority_never_publishes_uncertain_or_conflicted_owner() {
    for (std::uint8_t mode = 0; mode < 2; ++mode) {
        FakeProtectedStore store{};
        store.commit_error = mode == 0
                                 ? CompanionAuthorizationProtectedStoreError::
                                       uncertain
                                 : CompanionAuthorizationProtectedStoreError::
                                       conflict;
        store.apply_before_error = mode == 0;
        DurableCompanionAuthorizationPersistence persistence{store};
        const auto boot = static_cast<std::uint64_t>(700U + mode);
        CompanionAuthorizationAuthority authority{persistence, boot};
        EXPECT(authority.restore().accepted());
        EXPECT(authority.open_physical_window(physical(
                   CompanionPhysicalAuthorizationAction::claim_owner, kOwner,
                   boot, 1, 70)).accepted());
        const auto result = authority.claim_owner(
            claim(kOwner, boot, 1, 71), 71);
        EXPECT(!result.accepted());
        EXPECT(!result.connection_authorized());
        const auto status = authority.status(71);
        EXPECT(status.faulted);
        EXPECT(status.persistence_uncertain);
        EXPECT(!status.owner_present);
        EXPECT(!status.controller_active);
        EXPECT(status.owner_generation == 0);
    }
}

}  // namespace

int main() {
    test_exact_codec_vectors_and_tombstone();
    test_codec_rejects_malformed_and_incoherent_records();
    test_empty_restore_and_exact_owner_reboot();
    test_tombstone_survives_reboot();
    test_rollback_and_malformed_load_fail_uncertain();
    test_stale_compare_and_invalid_candidate_do_not_mutate();
    test_prewrite_failure_guarantees_no_mutation();
    test_postwrite_failure_is_uncertain_and_reboot_resolves_exact_state();
    test_inexact_readback_is_uncertain();
    test_storage_callback_reentry_fails_uncertain();
    test_bond_binding_is_stable_private_and_repair_distinct();
    test_bond_binding_fail_closed_edges();
    test_bond_binding_callback_reentry_rejects_outer_result();
    test_target_admission_requires_every_protected_prerequisite();
    test_authority_owner_commit_and_reboot_authorization();
    test_physical_replace_and_tombstones_survive_reboot();
    test_authority_never_publishes_uncertain_or_conflicted_owner();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "companion authorization persistence tests passed: 17 groups\n";
    return EXIT_SUCCESS;
}
