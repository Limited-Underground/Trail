#include "opentrail/companion_protected_root_inventory.hpp"

namespace opentrail::companion {
namespace {

bool IsKnownPurpose(InventoryKeyPurpose purpose) noexcept {
  switch (purpose) {
    case InventoryKeyPurpose::kUser:
    case InventoryKeyPurpose::kHmacUp:
    case InventoryKeyPurpose::kOther:
      return true;
    case InventoryKeyPurpose::kUnknown:
      return false;
  }
  return false;
}

bool HasExactBinding(const InventoryEvidenceBinding& candidate,
                     const InventoryEvidenceBinding& expected) noexcept {
  return expected.operation_id != 0U && expected.evidence_id != 0U &&
         expected.fresh && candidate.operation_id == expected.operation_id &&
         candidate.evidence_id == expected.evidence_id && candidate.fresh;
}

bool IsCompleteKeySlot(const InventoryKeySlotObservation& slot,
                       const InventoryEvidenceBinding& binding) noexcept {
  if (!HasExactBinding(slot.binding, binding) || !slot.observation_present ||
      !slot.purpose_known || !IsKnownPurpose(slot.purpose) ||
      !slot.provisioned_state_known || !slot.unused_state_known ||
      !slot.read_protection_known ||
      !slot.write_protection_known ||
      !slot.purpose_write_protection_known ||
      !slot.reservation_state_known) {
    return false;
  }

  // The platform definition of an unused key slot is exact: USER purpose,
  // neither protection set, and not reserved. Any contradictory normalized
  // observation is malformed and denied rather than inferred.
  if (slot.unused &&
      (slot.purpose != InventoryKeyPurpose::kUser || slot.read_protected ||
       slot.write_protected || slot.purpose_write_protected || slot.reserved ||
       slot.provisioned)) {
    return false;
  }

  if ((slot.purpose != InventoryKeyPurpose::kUser && slot.unused) ||
      (slot.provisioned && slot.unused)) {
    return false;
  }

  return true;
}

bool IsCompleteFloorObservation(
    const InventoryFloorObservation& floor,
    const InventoryEvidenceBinding& binding) noexcept {
  return HasExactBinding(floor.binding, binding) &&
         floor.observation_present && floor.allocation_map_complete &&
         floor.eligible_user_region_classification_known &&
         floor.reserved_overlap_known && floor.known_field_overlap_known &&
         floor.bit_state_known && floor.contiguous_capacity_known &&
         floor.read_protection_known && floor.write_protection_known &&
         floor.protection_granularity_known && floor.coding_constraints_known &&
         floor.independent_one_bit_programming_known;
}

bool HasForbiddenClaim(const ProtectedRootInventoryEvidence& evidence) noexcept {
  return evidence.key_material_observed ||
         evidence.raw_efuse_output_retained ||
         evidence.public_identity_or_transport_detail_present ||
         evidence.public_block_or_range_detail_present ||
         evidence.provider_selected || evidence.physical_admission_granted ||
         evidence.provisioning_authorized || evidence.protection_authorized ||
         evidence.efuse_write_authorized ||
         evidence.runtime_activation_authorized;
}

}  // namespace

ProtectedRootInventoryDecision EvaluateProtectedRootInventory(
    const ProtectedRootInventoryEvidence& evidence) noexcept {
  if (evidence.binding.operation_id == 0U ||
      evidence.binding.evidence_id == 0U || !evidence.binding.fresh ||
      !evidence.logical_target_verified || !evidence.esp32s3_profile_verified ||
      !evidence.tool_api_version_pinned ||
      !evidence.complete_key_slot_roster ||
      !evidence.configured_nvs_binding_observed ||
      !evidence.configured_nvs_binding_conflict_known ||
      !evidence.security_context_complete ||
      !evidence.secure_boot_state_known ||
      evidence.secure_boot_enabled ||
      !evidence.flash_encryption_state_known ||
      evidence.flash_encryption_enabled ||
      !evidence.download_mode_state_known ||
      evidence.secure_download_mode_enabled || !evidence.one_use_consumed ||
      !evidence.connection_closed || !evidence.transient_cleanup_verified ||
      evidence.reentry_observed || evidence.retry_observed ||
      HasForbiddenClaim(evidence)) {
    return ProtectedRootInventoryDecision::kDeny;
  }

  for (const auto& slot : evidence.key_slots) {
    if (!IsCompleteKeySlot(slot, evidence.binding)) {
      return ProtectedRootInventoryDecision::kDeny;
    }
  }

  if (!IsCompleteFloorObservation(evidence.floor, evidence.binding)) {
    return ProtectedRootInventoryDecision::kDeny;
  }

  return ProtectedRootInventoryDecision::
      kAcceptCompletePrivateInventoryForReviewOnly;
}

const char* SanitizeProtectedRootInventoryDecision(
    ProtectedRootInventoryDecision decision) noexcept {
  if (decision == ProtectedRootInventoryDecision::
                      kAcceptCompletePrivateInventoryForReviewOnly) {
    return kPrivateInventoryCapturedSelectionPending;
  }
  return kDeniedProtectedRootInventory;
}

}  // namespace opentrail::companion
