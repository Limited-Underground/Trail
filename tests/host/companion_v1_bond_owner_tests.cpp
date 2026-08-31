#include <array>
#include <cstdint>
#include <iostream>

#include "opentrail/companion_v1_bond_owner.hpp"

namespace {
using namespace opentrail::companion;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(value) expect((value), #value, __LINE__)

constexpr CompanionBondIdentityToken kOwnerA{0x1011121314151617ULL,
                                              0x2021222324252627ULL};
constexpr CompanionBondIdentityToken kOwnerB{0x3031323334353637ULL,
                                              0x4041424344454647ULL};

class MemoryStore final : public CompanionV1OwnerStoragePort {
public:
    CompanionV1OwnerStorageSnapshot load_result{
        CompanionV1OwnerStorageError::none, false, {}};
    CompanionV1OwnerStorageError commit_error{
        CompanionV1OwnerStorageError::none};
    bool wrong_readback{false};
    std::uint32_t commits{0};
    CompanionV1BondOwnerBridge* reenter_owner{nullptr};
    bool reenter_on_load{false};

    CompanionV1OwnerStorageSnapshot load() override {
        if (reenter_on_load && reenter_owner != nullptr) {
            reenter_on_load = false;
            (void)reenter_owner->restore();
        }
        return load_result;
    }
    CompanionV1OwnerStorageSnapshot commit_absent_and_readback(
        const std::array<std::uint8_t, kCompanionV1OwnerRecordBytes>& record)
        override {
        ++commits;
        if (load_result.record_present) {
            return {CompanionV1OwnerStorageError::conflict, true,
                    load_result.record};
        }
        if (commit_error != CompanionV1OwnerStorageError::none) {
            return {commit_error, false, {}};
        }
        load_result = {CompanionV1OwnerStorageError::none, true, record};
        if (wrong_readback) load_result.record[12] ^= 0x80U;
        return load_result;
    }
};

class Inventory final : public CompanionV1BondInventoryPort {
public:
    CompanionV1BondInventorySnapshot result{
        CompanionV1BondInventoryError::none, 0, {}};
    CompanionV1BondInventorySnapshot drift_result{};
    std::uint32_t calls{0};
    std::uint32_t drift_on_call{0};
    CompanionV1GattAuthorizationAuthority* reenter_authority{nullptr};
    CompanionControllerClaim reenter_claim{};
    CompanionGattAuthorizationDecision reenter_result{};
    bool reenter_on_snapshot{false};
    CompanionV1BondInventorySnapshot snapshot() override {
        ++calls;
        if (reenter_on_snapshot && reenter_authority != nullptr) {
            reenter_on_snapshot = false;
            reenter_result = reenter_authority->apply_claim(
                CompanionAuthorizationPurpose::authorize_controller,
                reenter_claim, 0);
        }
        return drift_on_call != 0 && calls >= drift_on_call
                   ? drift_result
                   : result;
    }
    void exact(CompanionBondIdentityToken owner) {
        result = {CompanionV1BondInventoryError::none, 1, {owner, {}}};
    }
};

void test_clean_claim_reboot_reconnect_and_wrong_phone() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge first{store, inventory};
    EXPECT(first.restore().accepted());
    EXPECT(first.status().phase == CompanionV1BondOwnerPhase::closed_unowned);
    inventory.exact(kOwnerA);
    EXPECT(first.accept_initial_bond(kOwnerA).accepted());
    EXPECT(store.commits == 1);

    CompanionV1BondOwnerBridge rebooted{store, inventory};
    EXPECT(rebooted.restore().accepted());
    EXPECT(rebooted.status().phase == CompanionV1BondOwnerPhase::closed_owned);
    CompanionV1GattAuthorizationAuthority gatt{rebooted, 1};
    const CompanionControllerClaim owner_claim{kOwnerA, 1, 2, 3, true, true};
    const auto accepted = gatt.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, owner_claim, 10);
    EXPECT(accepted.error == CompanionGattAuthorizationAuthorityError::none);
    EXPECT(accepted.outcome == CompanionAuthorizationClaimOutcome::accepted);
    EXPECT(accepted.controller_binding == 3);
    EXPECT(rebooted.status().controller_active);
    EXPECT(store.commits == 1);

    const CompanionControllerClaim wrong{kOwnerB, 1, 4, 5, true, true};
    const auto denied = gatt.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, wrong, 11);
    EXPECT(denied.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(denied.reason ==
           CompanionAuthorizationDenyReason::owner_state_conflict);
    EXPECT(store.commits == 1);
    EXPECT(gatt.release_connection(3) ==
           CompanionGattAuthorizationAuthorityError::none);
    EXPECT(rebooted.status().phase == CompanionV1BondOwnerPhase::closed_owned);
}

void test_absent_owner_with_orphan_or_multiple_bonds_fails_closed() {
    for (std::uint8_t count : {std::uint8_t{1}, std::uint8_t{2}}) {
        MemoryStore store{};
        Inventory inventory{};
        inventory.result.bond_count = count;
        inventory.result.private_references = {kOwnerA, kOwnerB};
        CompanionV1BondOwnerBridge owner{store, inventory};
        EXPECT(owner.restore().error ==
               CompanionV1BondOwnerError::bond_inventory_mismatch);
        EXPECT(owner.status().phase ==
               CompanionV1BondOwnerPhase::reconcile_required);
        EXPECT(!owner.status().owner_present);
    }
}

void test_corrupt_record_and_inventory_mismatch_fail_closed() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge first{store, inventory};
    EXPECT(first.restore().accepted());
    inventory.exact(kOwnerA);
    EXPECT(first.accept_initial_bond(kOwnerA).accepted());

    store.load_result.record[0] ^= 0x01U;
    CompanionV1BondOwnerBridge corrupt{store, inventory};
    EXPECT(corrupt.restore().error ==
           CompanionV1BondOwnerError::record_invalid);
    EXPECT(corrupt.status().phase ==
           CompanionV1BondOwnerPhase::reconcile_required);

    store.load_result.record[0] ^= 0x01U;
    inventory.exact(kOwnerB);
    CompanionV1BondOwnerBridge mismatched{store, inventory};
    EXPECT(mismatched.restore().error ==
           CompanionV1BondOwnerError::bond_inventory_mismatch);
    EXPECT(!mismatched.status().owner_present);

    MemoryStore inexact_store{};
    inexact_store.load_result.record[31] = 1;
    Inventory empty_inventory{};
    CompanionV1BondOwnerBridge inexact_absent{inexact_store,
                                               empty_inventory};
    EXPECT(inexact_absent.restore().error ==
           CompanionV1BondOwnerError::bond_inventory_mismatch);

    MemoryStore exact_store{};
    Inventory inexact_inventory{};
    inexact_inventory.result.private_references[1] = kOwnerB;
    CompanionV1BondOwnerBridge inexact_empty{exact_store,
                                             inexact_inventory};
    EXPECT(inexact_empty.restore().error ==
           CompanionV1BondOwnerError::bond_inventory_mismatch);
}

void test_uncertain_commit_and_inexact_readback_never_publish_owner() {
    for (std::uint8_t mode = 0; mode < 2; ++mode) {
        MemoryStore store{};
        Inventory inventory{};
        CompanionV1BondOwnerBridge owner{store, inventory};
        EXPECT(owner.restore().accepted());
        inventory.exact(kOwnerA);
        if (mode == 0) {
            store.commit_error = CompanionV1OwnerStorageError::uncertain;
        } else {
            store.wrong_readback = true;
        }
        EXPECT(!owner.accept_initial_bond(kOwnerA).accepted());
        EXPECT(owner.status().phase ==
               CompanionV1BondOwnerPhase::reconcile_required);
        EXPECT(owner.status().persistence_uncertain);
        EXPECT(!owner.status().owner_present);
        CompanionV1GattAuthorizationAuthority gatt{owner, 1};
        const CompanionControllerClaim claim{kOwnerA, 1, 2, 3, true, true};
        EXPECT(gatt.apply_claim(
                   CompanionAuthorizationPurpose::authorize_controller,
                   claim, 10).error ==
               CompanionGattAuthorizationAuthorityError::not_ready);
    }
}

void test_security_replacement_and_controller_guards() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge owner{store, inventory};
    EXPECT(owner.restore().accepted());
    inventory.exact(kOwnerA);
    EXPECT(owner.accept_initial_bond(kOwnerA).accepted());
    CompanionV1GattAuthorizationAuthority gatt{owner, 1};

    CompanionControllerClaim insecure{kOwnerA, 1, 2, 3, false, true};
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               insecure, 1).reason ==
           CompanionAuthorizationDenyReason::policy_denied);
    CompanionControllerClaim secure{kOwnerA, 1, 2, 3, true, true};
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::replace_controller,
               secure, 2).reason ==
           CompanionAuthorizationDenyReason::unsupported);
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               secure, 3).outcome ==
           CompanionAuthorizationClaimOutcome::accepted);
    secure.controller_binding = 4;
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               secure, 4).reason ==
           CompanionAuthorizationDenyReason::owner_state_conflict);
    EXPECT(gatt.release_connection(4) ==
           CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(gatt.release_connection(3) ==
           CompanionGattAuthorizationAuthorityError::none);
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               secure, 5).reason ==
           CompanionAuthorizationDenyReason::owner_state_conflict);

    secure.session_challenge = 4;
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               secure, 6).outcome ==
           CompanionAuthorizationClaimOutcome::accepted);
    auto altered_active = secure;
    altered_active.session_challenge = 5;
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               altered_active, 7).reason ==
           CompanionAuthorizationDenyReason::owner_state_conflict);
    EXPECT(gatt.release_connection(4) ==
           CompanionGattAuthorizationAuthorityError::none);

    secure.boot_challenge = 2;
    secure.session_challenge = 6;
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               secure, 8).reason ==
           CompanionAuthorizationDenyReason::policy_denied);
}

void test_post_commit_and_authorization_inventory_drift_fail_closed() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge owner{store, inventory};
    EXPECT(owner.restore().accepted());
    inventory.exact(kOwnerA);
    inventory.drift_result = inventory.result;
    inventory.drift_result.private_references[0] = kOwnerB;
    inventory.drift_on_call = inventory.calls + 2;
    EXPECT(owner.accept_initial_bond(kOwnerA).error ==
           CompanionV1BondOwnerError::bond_inventory_mismatch);
    EXPECT(owner.status().phase ==
           CompanionV1BondOwnerPhase::reconcile_required);

    MemoryStore second_store{};
    Inventory second_inventory{};
    CompanionV1BondOwnerBridge second{second_store, second_inventory};
    EXPECT(second.restore().accepted());
    second_inventory.exact(kOwnerA);
    EXPECT(second.accept_initial_bond(kOwnerA).accepted());
    second_inventory.exact(kOwnerB);
    CompanionV1GattAuthorizationAuthority gatt{second, 1};
    const CompanionControllerClaim claim{kOwnerA, 1, 2, 3, true, true};
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               claim, 1).reason ==
           CompanionAuthorizationDenyReason::internal_failure);
    EXPECT(second.status().phase ==
           CompanionV1BondOwnerPhase::reconcile_required);
}

void test_storage_callback_reentry_contains_outer_restore() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge owner{store, inventory};
    store.reenter_owner = &owner;
    store.reenter_on_load = true;
    EXPECT(owner.restore().error == CompanionV1BondOwnerError::reentrant_call);
    EXPECT(owner.status().phase ==
           CompanionV1BondOwnerPhase::reconcile_required);
}

void test_gatt_authority_callback_reentry_cannot_publish_outer_claim() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge owner{store, inventory};
    EXPECT(owner.restore().accepted());
    inventory.exact(kOwnerA);
    EXPECT(owner.accept_initial_bond(kOwnerA).accepted());

    CompanionV1GattAuthorizationAuthority gatt{owner, 1};
    const CompanionControllerClaim claim{kOwnerA, 1, 2, 3, true, true};
    inventory.reenter_authority = &gatt;
    inventory.reenter_claim = claim;
    inventory.reenter_on_snapshot = true;
    const auto outer = gatt.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, claim, 1);
    EXPECT(inventory.reenter_result.error ==
           CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(inventory.reenter_result.outcome ==
           CompanionAuthorizationClaimOutcome::denied);
    EXPECT(outer.error == CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(outer.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(outer.reason == CompanionAuthorizationDenyReason::internal_failure);
    EXPECT(owner.status().phase == CompanionV1BondOwnerPhase::closed_owned);
    EXPECT(!owner.status().controller_active);
    EXPECT(gatt.release_connection(3) ==
           CompanionGattAuthorizationAuthorityError::failed);
}

void test_gatt_authority_reentry_preserves_existing_exact_lease() {
    MemoryStore store{};
    Inventory inventory{};
    CompanionV1BondOwnerBridge owner{store, inventory};
    EXPECT(owner.restore().accepted());
    inventory.exact(kOwnerA);
    EXPECT(owner.accept_initial_bond(kOwnerA).accepted());

    CompanionV1GattAuthorizationAuthority gatt{owner, 1};
    const CompanionControllerClaim claim{kOwnerA, 1, 2, 3, true, true};
    EXPECT(gatt.apply_claim(
               CompanionAuthorizationPurpose::authorize_controller,
               claim, 1).outcome ==
           CompanionAuthorizationClaimOutcome::accepted);
    EXPECT(owner.status().controller_active);

    inventory.reenter_authority = &gatt;
    inventory.reenter_claim = claim;
    inventory.reenter_on_snapshot = true;
    const auto retry = gatt.apply_claim(
        CompanionAuthorizationPurpose::authorize_controller, claim, 2);
    EXPECT(inventory.reenter_result.error ==
           CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(retry.error == CompanionGattAuthorizationAuthorityError::failed);
    EXPECT(retry.outcome == CompanionAuthorizationClaimOutcome::denied);
    EXPECT(owner.status().phase ==
           CompanionV1BondOwnerPhase::controller_active);
    EXPECT(owner.status().controller_active);
    EXPECT(gatt.release_connection(3) ==
           CompanionGattAuthorizationAuthorityError::none);
    EXPECT(owner.status().phase == CompanionV1BondOwnerPhase::closed_owned);
}

}  // namespace

int main() {
    test_clean_claim_reboot_reconnect_and_wrong_phone();
    test_absent_owner_with_orphan_or_multiple_bonds_fails_closed();
    test_corrupt_record_and_inventory_mismatch_fail_closed();
    test_uncertain_commit_and_inexact_readback_never_publish_owner();
    test_security_replacement_and_controller_guards();
    test_post_commit_and_authorization_inventory_drift_fail_closed();
    test_storage_callback_reentry_contains_outer_restore();
    test_gatt_authority_callback_reentry_cannot_publish_outer_claim();
    test_gatt_authority_reentry_preserves_existing_exact_lease();
    if (failures != 0) {
        std::cerr << failures << " V1 bond-owner assertion(s) failed\n";
        return 1;
    }
    std::cout << "PASS: 9 V1 durable bond-owner scenario groups\n";
    return 0;
}
