#include "opentrail/companion_authorization_storage_transition.hpp"

namespace opentrail::companion {
namespace {

bool known_layout(CompanionAuthorizationStorageLayout layout) {
    return layout == CompanionAuthorizationStorageLayout::unknown ||
           layout == CompanionAuthorizationStorageLayout::othp0_v0 ||
           layout == CompanionAuthorizationStorageLayout::otps0_v0;
}

bool known_route(CompanionAuthorizationStorageSourceRoute route) {
    return route == CompanionAuthorizationStorageSourceRoute::unknown ||
           route == CompanionAuthorizationStorageSourceRoute::verified_blank ||
           route ==
               CompanionAuthorizationStorageSourceRoute::semantic_migration;
}

bool digest_is_zero(const CompanionAuthorizationStorageDigest& digest) {
    for (const auto byte : digest) {
        if (byte != 0) return false;
    }
    return true;
}

CompanionAuthorizationStorageTransitionAdmission deny(
    CompanionAuthorizationStorageTransitionError error) {
    return {error, false};
}

}  // namespace

CompanionAuthorizationStorageTransitionAdmission
evaluate_companion_authorization_storage_transition(
    const CompanionAuthorizationStorageTransitionPlan& plan) {
    if (!known_layout(plan.source_layout) ||
        !known_layout(plan.candidate_layout) ||
        !known_layout(plan.installed_layout) ||
        !known_layout(plan.recovery.original_partition_table_layout) ||
        !known_layout(plan.authority.source_layout) ||
        !known_layout(plan.authority.candidate_layout) ||
        !known_route(plan.source_route)) {
        return deny(CompanionAuthorizationStorageTransitionError::invalid_plan);
    }
    if (plan.operation_id == 0 || plan.evidence_set_id == 0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_missing);
    }
    if (plan.source_layout !=
        CompanionAuthorizationStorageLayout::othp0_v0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        source_layout_mismatch);
    }
    if (plan.candidate_layout !=
        CompanionAuthorizationStorageLayout::otps0_v0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        candidate_layout_mismatch);
    }
    if (plan.source_layout_sha256 != kOthp0V0TableSha256) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        source_layout_digest_mismatch);
    }
    if (plan.candidate_layout_sha256 != kOtps0V0TableSha256) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        candidate_layout_digest_mismatch);
    }
    if (!plan.installed_layout_readback_present) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        installed_layout_readback_missing);
    }
    if (plan.installed_layout_operation_id == 0 ||
        plan.installed_layout_evidence_set_id == 0 ||
        plan.installed_layout_evidence_generation == 0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_missing);
    }
    if (plan.installed_layout_operation_id != plan.operation_id ||
        plan.installed_layout_evidence_set_id != plan.evidence_set_id) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_mismatch);
    }
    if (plan.installed_layout != plan.source_layout) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        installed_layout_mismatch);
    }
    if (plan.installed_layout_sha256 != plan.source_layout_sha256) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        installed_layout_digest_mismatch);
    }
    if (plan.source_evidence_operation_id == 0 ||
        plan.source_evidence_set_id == 0 ||
        plan.source_evidence_generation == 0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_missing);
    }
    if (plan.source_evidence_operation_id != plan.operation_id ||
        plan.source_evidence_set_id != plan.evidence_set_id) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_mismatch);
    }

    if (plan.source_route ==
        CompanionAuthorizationStorageSourceRoute::unknown) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        source_state_unverified);
    }
    if (plan.source_route ==
        CompanionAuthorizationStorageSourceRoute::verified_blank) {
        if (!plan.source_state_blank_verified) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            source_state_unverified);
        }
        if (plan.source_region_sha256 != kOneMibAllFfSha256) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            source_region_digest_mismatch);
        }
        if (plan.semantic_migration_implemented ||
            plan.semantic_migration_verified) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            source_state_contradictory);
        }
    } else {
        if (plan.source_state_blank_verified) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            source_state_contradictory);
        }
        if (digest_is_zero(plan.source_region_sha256)) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            source_region_digest_mismatch);
        }
        if (!plan.semantic_migration_implemented) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            migration_not_implemented);
        }
        if (!plan.semantic_migration_verified) {
            return deny(CompanionAuthorizationStorageTransitionError::
                            migration_not_verified);
        }
    }

    if (plan.recovery.operation_id == 0 ||
        plan.recovery.evidence_set_id == 0 ||
        plan.recovery.generation == 0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_missing);
    }
    if (plan.recovery.operation_id != plan.operation_id ||
        plan.recovery.evidence_set_id != plan.evidence_set_id) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        evidence_identity_mismatch);
    }
    if (plan.recovery.original_partition_table_layout != plan.source_layout) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        recovery_partition_table_mismatch);
    }
    if (!plan.recovery.original_partition_table_hash_verified) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        recovery_partition_table_unverified);
    }
    if (plan.recovery.original_partition_table_sha256 !=
        plan.source_layout_sha256) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        recovery_partition_table_digest_mismatch);
    }
    if (!plan.recovery.recovery_application_hash_verified) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        recovery_application_unverified);
    }
    if (digest_is_zero(plan.recovery.recovery_application_sha256)) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        recovery_application_digest_missing);
    }
    if (plan.recovery.rom_recovery_route_id == 0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        rom_recovery_route_identity_missing);
    }
    if (!plan.recovery.rom_recovery_route_verified) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        rom_recovery_route_unverified);
    }
    if (plan.authorization_runtime_activation_requested) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        runtime_activation_requested);
    }
    if (plan.efuse_operation_requested) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        efuse_operation_requested);
    }
    if (plan.key_operation_requested) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        key_operation_requested);
    }
    if (plan.other_flash_operation_requested) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        other_flash_operation_requested);
    }
    if (!plan.partition_table_transition_requested) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        partition_transition_not_requested);
    }
    if (!plan.authority.present || plan.authority.operation_id == 0 ||
        plan.authority.evidence_set_id == 0) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        operation_authority_missing);
    }
    if (plan.authority.operation_id != plan.operation_id ||
        plan.authority.evidence_set_id != plan.evidence_set_id ||
        plan.authority.installed_evidence_generation !=
            plan.installed_layout_evidence_generation ||
        plan.authority.source_evidence_generation !=
            plan.source_evidence_generation ||
        plan.authority.recovery_evidence_generation !=
            plan.recovery.generation ||
        plan.authority.source_layout != plan.source_layout ||
        plan.authority.candidate_layout != plan.candidate_layout ||
        plan.authority.source_layout_sha256 != plan.source_layout_sha256 ||
        plan.authority.candidate_layout_sha256 !=
            plan.candidate_layout_sha256 ||
        plan.authority.source_region_sha256 != plan.source_region_sha256 ||
        plan.authority.recovery_partition_table_sha256 !=
            plan.recovery.original_partition_table_sha256 ||
        plan.authority.recovery_application_sha256 !=
            plan.recovery.recovery_application_sha256 ||
        plan.authority.rom_recovery_route_id !=
            plan.recovery.rom_recovery_route_id) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        operation_authority_mismatch);
    }
    if (!plan.authority.partition_table_only) {
        return deny(CompanionAuthorizationStorageTransitionError::
                        operation_authority_scope_mismatch);
    }
    return {CompanionAuthorizationStorageTransitionError::none, true};
}

}  // namespace opentrail::companion
