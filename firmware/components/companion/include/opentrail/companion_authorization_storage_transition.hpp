#pragma once

#include <array>
#include <cstdint>

namespace opentrail::companion {

using CompanionAuthorizationStorageDigest = std::array<std::uint8_t, 32>;

inline constexpr CompanionAuthorizationStorageDigest kOthp0V0TableSha256{{
    0x4F, 0x06, 0x4C, 0x12, 0x5A, 0xA6, 0x41, 0x69,
    0x7E, 0x05, 0x39, 0xEA, 0xF9, 0xED, 0xA9, 0xD1,
    0xCD, 0xEC, 0xAB, 0x46, 0xDD, 0x8F, 0xF3, 0x87,
    0x98, 0x8B, 0x90, 0x0F, 0x3E, 0xFE, 0x23, 0x89,
}};
inline constexpr CompanionAuthorizationStorageDigest kOtps0V0TableSha256{{
    0x31, 0x0F, 0xD2, 0x07, 0x68, 0x7C, 0x6D, 0x99,
    0x64, 0xF8, 0xC1, 0xBF, 0x83, 0x03, 0x1A, 0xCF,
    0xD5, 0xEA, 0xFD, 0x83, 0x7E, 0x7D, 0xBF, 0xCF,
    0xF4, 0x29, 0xA5, 0xCE, 0xE1, 0x68, 0xC3, 0xCA,
}};
inline constexpr CompanionAuthorizationStorageDigest kOneMibAllFfSha256{{
    0xF5, 0xFB, 0x04, 0xAA, 0x5B, 0x88, 0x27, 0x06,
    0xB9, 0x30, 0x9E, 0x88, 0x5F, 0x19, 0x47, 0x72,
    0x61, 0x33, 0x6E, 0xF7, 0x6A, 0x15, 0x0C, 0x3B,
    0x4D, 0x34, 0x89, 0xDF, 0xAC, 0x39, 0x53, 0xEC,
}};

enum class CompanionAuthorizationStorageLayout : std::uint8_t {
    unknown = 0,
    othp0_v0,
    otps0_v0,
};

enum class CompanionAuthorizationStorageSourceRoute : std::uint8_t {
    unknown = 0,
    verified_blank,
    semantic_migration,
};

enum class CompanionAuthorizationStorageTransitionError : std::uint8_t {
    none = 0,
    invalid_plan,
    evidence_identity_missing,
    evidence_identity_mismatch,
    source_layout_mismatch,
    candidate_layout_mismatch,
    source_layout_digest_mismatch,
    candidate_layout_digest_mismatch,
    installed_layout_readback_missing,
    installed_layout_mismatch,
    installed_layout_digest_mismatch,
    source_state_unverified,
    source_region_digest_mismatch,
    source_state_contradictory,
    migration_not_implemented,
    migration_not_verified,
    recovery_partition_table_mismatch,
    recovery_partition_table_unverified,
    recovery_partition_table_digest_mismatch,
    recovery_application_unverified,
    recovery_application_digest_missing,
    rom_recovery_route_identity_missing,
    rom_recovery_route_unverified,
    runtime_activation_requested,
    efuse_operation_requested,
    key_operation_requested,
    other_flash_operation_requested,
    partition_transition_not_requested,
    operation_authority_missing,
    operation_authority_mismatch,
    operation_authority_scope_mismatch,
};

struct CompanionAuthorizationStorageRecoveryEvidence {
    std::uint64_t operation_id{0};
    std::uint64_t evidence_set_id{0};
    std::uint64_t generation{0};
    CompanionAuthorizationStorageLayout original_partition_table_layout{
        CompanionAuthorizationStorageLayout::unknown};
    CompanionAuthorizationStorageDigest original_partition_table_sha256{};
    bool original_partition_table_hash_verified{false};
    CompanionAuthorizationStorageDigest recovery_application_sha256{};
    bool recovery_application_hash_verified{false};
    std::uint64_t rom_recovery_route_id{0};
    bool rom_recovery_route_verified{false};
};

// One caller-owned, operation-scoped permit. The evaluator does not persist,
// consume, renew, or execute this authority. A caller that later performs a
// physical operation must separately guarantee one-shot consumption.
struct CompanionAuthorizationStorageTransitionAuthority {
    bool present{false};
    std::uint64_t operation_id{0};
    std::uint64_t evidence_set_id{0};
    std::uint64_t installed_evidence_generation{0};
    std::uint64_t source_evidence_generation{0};
    std::uint64_t recovery_evidence_generation{0};
    CompanionAuthorizationStorageLayout source_layout{
        CompanionAuthorizationStorageLayout::unknown};
    CompanionAuthorizationStorageLayout candidate_layout{
        CompanionAuthorizationStorageLayout::unknown};
    CompanionAuthorizationStorageDigest source_layout_sha256{};
    CompanionAuthorizationStorageDigest candidate_layout_sha256{};
    CompanionAuthorizationStorageDigest source_region_sha256{};
    CompanionAuthorizationStorageDigest recovery_partition_table_sha256{};
    CompanionAuthorizationStorageDigest recovery_application_sha256{};
    std::uint64_t rom_recovery_route_id{0};
    bool partition_table_only{false};
};

// Pure evidence input for a partition-table-only transition. This contract
// has no filesystem, device, flash, NVS, key, eFuse, or execution authority.
struct CompanionAuthorizationStorageTransitionPlan {
    std::uint64_t operation_id{0};
    std::uint64_t evidence_set_id{0};
    CompanionAuthorizationStorageLayout source_layout{
        CompanionAuthorizationStorageLayout::unknown};
    CompanionAuthorizationStorageLayout candidate_layout{
        CompanionAuthorizationStorageLayout::unknown};
    CompanionAuthorizationStorageDigest source_layout_sha256{};
    CompanionAuthorizationStorageDigest candidate_layout_sha256{};

    bool installed_layout_readback_present{false};
    std::uint64_t installed_layout_operation_id{0};
    std::uint64_t installed_layout_evidence_set_id{0};
    std::uint64_t installed_layout_evidence_generation{0};
    CompanionAuthorizationStorageLayout installed_layout{
        CompanionAuthorizationStorageLayout::unknown};
    CompanionAuthorizationStorageDigest installed_layout_sha256{};

    std::uint64_t source_evidence_operation_id{0};
    std::uint64_t source_evidence_set_id{0};
    std::uint64_t source_evidence_generation{0};
    CompanionAuthorizationStorageSourceRoute source_route{
        CompanionAuthorizationStorageSourceRoute::unknown};
    CompanionAuthorizationStorageDigest source_region_sha256{};
    bool source_state_blank_verified{false};
    bool semantic_migration_implemented{false};
    bool semantic_migration_verified{false};

    CompanionAuthorizationStorageRecoveryEvidence recovery{};

    bool authorization_runtime_activation_requested{false};
    bool efuse_operation_requested{false};
    bool key_operation_requested{false};
    bool other_flash_operation_requested{false};
    bool partition_table_transition_requested{false};

    CompanionAuthorizationStorageTransitionAuthority authority{};
};

struct CompanionAuthorizationStorageTransitionAdmission {
    CompanionAuthorizationStorageTransitionError error{
        CompanionAuthorizationStorageTransitionError::invalid_plan};
    bool admitted{false};
};

// Evaluates one exact OTHP0/v0 -> OTPS0/v0 partition-table-only operation.
// Checks are deliberately ordered so the earliest unmet prerequisite is the
// sole published denial. This function performs no I/O and changes no state.
[[nodiscard]] CompanionAuthorizationStorageTransitionAdmission
evaluate_companion_authorization_storage_transition(
    const CompanionAuthorizationStorageTransitionPlan& plan);

}  // namespace opentrail::companion
