#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/companion_authorization_storage_transition.hpp"

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

CompanionAuthorizationStorageTransitionPlan admitted_blank_plan() {
    CompanionAuthorizationStorageTransitionPlan plan{};
    plan.operation_id = 0x4F545030ULL;
    plan.evidence_set_id = 7;
    plan.source_layout = CompanionAuthorizationStorageLayout::othp0_v0;
    plan.candidate_layout = CompanionAuthorizationStorageLayout::otps0_v0;
    plan.source_layout_sha256 = kOthp0V0TableSha256;
    plan.candidate_layout_sha256 = kOtps0V0TableSha256;
    plan.installed_layout_readback_present = true;
    plan.installed_layout_operation_id = plan.operation_id;
    plan.installed_layout_evidence_set_id = plan.evidence_set_id;
    plan.installed_layout_evidence_generation = 11;
    plan.installed_layout = CompanionAuthorizationStorageLayout::othp0_v0;
    plan.installed_layout_sha256 = kOthp0V0TableSha256;
    plan.source_evidence_operation_id = plan.operation_id;
    plan.source_evidence_set_id = plan.evidence_set_id;
    plan.source_evidence_generation = 12;
    plan.source_route =
        CompanionAuthorizationStorageSourceRoute::verified_blank;
    plan.source_region_sha256 = kOneMibAllFfSha256;
    plan.source_state_blank_verified = true;
    plan.recovery.operation_id = plan.operation_id;
    plan.recovery.evidence_set_id = plan.evidence_set_id;
    plan.recovery.generation = 13;
    plan.recovery.original_partition_table_layout =
        CompanionAuthorizationStorageLayout::othp0_v0;
    plan.recovery.original_partition_table_sha256 = kOthp0V0TableSha256;
    plan.recovery.original_partition_table_hash_verified = true;
    plan.recovery.recovery_application_sha256 = kOtps0V0TableSha256;
    plan.recovery.recovery_application_hash_verified = true;
    plan.recovery.rom_recovery_route_id = 17;
    plan.recovery.rom_recovery_route_verified = true;
    plan.partition_table_transition_requested = true;
    plan.authority.present = true;
    plan.authority.operation_id = plan.operation_id;
    plan.authority.evidence_set_id = plan.evidence_set_id;
    plan.authority.installed_evidence_generation = plan.installed_layout_evidence_generation;
    plan.authority.source_evidence_generation = plan.source_evidence_generation;
    plan.authority.recovery_evidence_generation = plan.recovery.generation;
    plan.authority.source_layout = plan.source_layout;
    plan.authority.candidate_layout = plan.candidate_layout;
    plan.authority.source_layout_sha256 = plan.source_layout_sha256;
    plan.authority.candidate_layout_sha256 = plan.candidate_layout_sha256;
    plan.authority.source_region_sha256 = plan.source_region_sha256;
    plan.authority.recovery_partition_table_sha256 = plan.recovery.original_partition_table_sha256;
    plan.authority.recovery_application_sha256 = plan.recovery.recovery_application_sha256;
    plan.authority.rom_recovery_route_id = plan.recovery.rom_recovery_route_id;
    plan.authority.partition_table_only = true;
    return plan;
}

void expect_error(const CompanionAuthorizationStorageTransitionPlan& plan,
                  CompanionAuthorizationStorageTransitionError error) {
    const auto result =
        evaluate_companion_authorization_storage_transition(plan);
    EXPECT(!result.admitted);
    EXPECT(result.error == error);
}

void test_exact_verified_blank_transition_is_admitted() {
    const auto result = evaluate_companion_authorization_storage_transition(
        admitted_blank_plan());
    EXPECT(result.admitted);
    EXPECT(result.error == CompanionAuthorizationStorageTransitionError::none);
}

void test_exact_verified_semantic_migration_is_admitted() {
    auto plan = admitted_blank_plan();
    plan.source_route =
        CompanionAuthorizationStorageSourceRoute::semantic_migration;
    plan.source_state_blank_verified = false;
    plan.source_region_sha256 = kOtps0V0TableSha256;
    plan.authority.source_region_sha256 = plan.source_region_sha256;
    plan.semantic_migration_implemented = true;
    plan.semantic_migration_verified = true;
    const auto result =
        evaluate_companion_authorization_storage_transition(plan);
    EXPECT(result.admitted);
    EXPECT(result.error == CompanionAuthorizationStorageTransitionError::none);
}

void test_unknown_numeric_values_are_invalid_before_other_checks() {
    auto plan = admitted_blank_plan();
    plan.source_layout =
        static_cast<CompanionAuthorizationStorageLayout>(0xFFU);
    plan.installed_layout_readback_present = false;
    expect_error(plan,
                 CompanionAuthorizationStorageTransitionError::invalid_plan);

    plan = admitted_blank_plan();
    plan.source_route =
        static_cast<CompanionAuthorizationStorageSourceRoute>(0xFFU);
    expect_error(plan,
                 CompanionAuthorizationStorageTransitionError::invalid_plan);
}

void test_evidence_is_operation_set_generation_and_digest_bound() {
    auto plan = admitted_blank_plan();
    plan.evidence_set_id = 0;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           evidence_identity_missing);

    plan = admitted_blank_plan();
    ++plan.installed_layout_operation_id;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           evidence_identity_mismatch);

    plan = admitted_blank_plan();
    ++plan.source_evidence_set_id;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           evidence_identity_mismatch);

    plan = admitted_blank_plan();
    plan.recovery.generation = 0;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           evidence_identity_missing);

    plan = admitted_blank_plan();
    plan.source_layout_sha256 = kOtps0V0TableSha256;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_layout_digest_mismatch);

    plan = admitted_blank_plan();
    plan.installed_layout_sha256 = kOtps0V0TableSha256;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           installed_layout_digest_mismatch);

    plan = admitted_blank_plan();
    plan.source_region_sha256 = kOtps0V0TableSha256;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_region_digest_mismatch);

    plan = admitted_blank_plan();
    plan.recovery.original_partition_table_sha256 = kOtps0V0TableSha256;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           recovery_partition_table_digest_mismatch);

    plan = admitted_blank_plan();
    plan.authority.source_evidence_generation++;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           operation_authority_mismatch);
}

void test_exact_source_and_candidate_identities_are_required_in_order() {
    auto plan = admitted_blank_plan();
    plan.source_layout = CompanionAuthorizationStorageLayout::unknown;
    plan.candidate_layout = CompanionAuthorizationStorageLayout::unknown;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_layout_mismatch);

    plan = admitted_blank_plan();
    plan.candidate_layout = CompanionAuthorizationStorageLayout::othp0_v0;
    plan.installed_layout_readback_present = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           candidate_layout_mismatch);
}

void test_installed_layout_requires_readback_and_exact_source_match() {
    auto plan = admitted_blank_plan();
    plan.installed_layout_readback_present = false;
    plan.installed_layout = CompanionAuthorizationStorageLayout::unknown;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           installed_layout_readback_missing);

    plan = admitted_blank_plan();
    plan.installed_layout = CompanionAuthorizationStorageLayout::otps0_v0;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           installed_layout_mismatch);
}

void test_blank_route_requires_exact_nonmigration_evidence() {
    auto plan = admitted_blank_plan();
    plan.source_route = CompanionAuthorizationStorageSourceRoute::unknown;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_state_unverified);

    plan = admitted_blank_plan();
    plan.source_state_blank_verified = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_state_unverified);

    plan = admitted_blank_plan();
    plan.semantic_migration_implemented = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_state_contradictory);

    plan = admitted_blank_plan();
    plan.semantic_migration_verified = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_state_contradictory);
}

void test_migration_route_requires_implementation_then_verification() {
    auto plan = admitted_blank_plan();
    plan.source_route =
        CompanionAuthorizationStorageSourceRoute::semantic_migration;
    plan.source_state_blank_verified = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           migration_not_implemented);

    plan.semantic_migration_implemented = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           migration_not_verified);

    plan.source_state_blank_verified = true;
    plan.semantic_migration_verified = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           source_state_contradictory);
}

void test_recovery_artifacts_and_rom_route_are_required_in_order() {
    auto plan = admitted_blank_plan();
    plan.recovery.original_partition_table_layout =
        CompanionAuthorizationStorageLayout::otps0_v0;
    plan.recovery.original_partition_table_hash_verified = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           recovery_partition_table_mismatch);

    plan = admitted_blank_plan();
    plan.recovery.original_partition_table_hash_verified = false;
    plan.recovery.recovery_application_hash_verified = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           recovery_partition_table_unverified);

    plan = admitted_blank_plan();
    plan.recovery.recovery_application_hash_verified = false;
    plan.recovery.rom_recovery_route_verified = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           recovery_application_unverified);

    plan = admitted_blank_plan();
    plan.recovery.rom_recovery_route_verified = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           rom_recovery_route_unverified);
}

void test_runtime_activation_is_forbidden_before_irreversible_checks() {
    auto plan = admitted_blank_plan();
    plan.authorization_runtime_activation_requested = true;
    plan.efuse_operation_requested = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           runtime_activation_requested);
}

void test_irreversible_and_broader_flash_operations_are_forbidden_in_order() {
    auto plan = admitted_blank_plan();
    plan.efuse_operation_requested = true;
    plan.key_operation_requested = true;
    plan.other_flash_operation_requested = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           efuse_operation_requested);

    plan = admitted_blank_plan();
    plan.key_operation_requested = true;
    plan.other_flash_operation_requested = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           key_operation_requested);

    plan = admitted_blank_plan();
    plan.other_flash_operation_requested = true;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           other_flash_operation_requested);
}

void test_transition_request_and_nonzero_authority_are_required() {
    auto plan = admitted_blank_plan();
    plan.partition_table_transition_requested = false;
    plan.authority.present = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           partition_transition_not_requested);

    plan = admitted_blank_plan();
    plan.authority.present = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           operation_authority_missing);

    plan = admitted_blank_plan();
    plan.operation_id = 0;
    plan.authority.operation_id = 0;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           evidence_identity_missing);
}

void test_authority_is_bound_to_operation_layouts_and_exact_scope() {
    auto plan = admitted_blank_plan();
    ++plan.authority.operation_id;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           operation_authority_mismatch);

    plan = admitted_blank_plan();
    plan.authority.source_layout =
        CompanionAuthorizationStorageLayout::otps0_v0;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           operation_authority_mismatch);

    plan = admitted_blank_plan();
    plan.authority.candidate_layout =
        CompanionAuthorizationStorageLayout::othp0_v0;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           operation_authority_mismatch);

    plan = admitted_blank_plan();
    plan.authority.partition_table_only = false;
    expect_error(plan, CompanionAuthorizationStorageTransitionError::
                           operation_authority_scope_mismatch);
}

}  // namespace

int main() {
    test_exact_verified_blank_transition_is_admitted();
    test_exact_verified_semantic_migration_is_admitted();
    test_unknown_numeric_values_are_invalid_before_other_checks();
    test_evidence_is_operation_set_generation_and_digest_bound();
    test_exact_source_and_candidate_identities_are_required_in_order();
    test_installed_layout_requires_readback_and_exact_source_match();
    test_blank_route_requires_exact_nonmigration_evidence();
    test_migration_route_requires_implementation_then_verification();
    test_recovery_artifacts_and_rom_route_are_required_in_order();
    test_runtime_activation_is_forbidden_before_irreversible_checks();
    test_irreversible_and_broader_flash_operations_are_forbidden_in_order();
    test_transition_request_and_nonzero_authority_are_required();
    test_authority_is_bound_to_operation_layouts_and_exact_scope();

    if (failures != 0) {
        std::cerr << failures << " storage transition assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 protected storage transition scenario groups\n";
    return EXIT_SUCCESS;
}
