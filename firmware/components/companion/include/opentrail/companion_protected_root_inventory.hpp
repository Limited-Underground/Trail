#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

inline constexpr std::size_t kProtectedRootInventoryKeySlotCount = 6U;
inline constexpr char kPrivateInventoryCapturedSelectionPending[] =
    "PRIVATE-INVENTORY-CAPTURED-SELECTION-PENDING";
inline constexpr char kDeniedProtectedRootInventory[] = "DENY-INVENTORY";

enum class InventoryKeyPurpose : std::uint8_t {
  kUnknown = 0,
  kUser = 1,
  kHmacUp = 2,
  kOther = 3,
};

enum class ProtectedRootInventoryDecision : std::uint8_t {
  kDeny = 0,
  kAcceptCompletePrivateInventoryForReviewOnly = 1,
};

struct InventoryEvidenceBinding {
  std::uint64_t operation_id{0U};
  std::uint64_t evidence_id{0U};
  bool fresh{false};
};

struct InventoryKeySlotObservation {
  InventoryEvidenceBinding binding{};
  bool observation_present{false};
  bool purpose_known{false};
  InventoryKeyPurpose purpose{InventoryKeyPurpose::kUnknown};
  bool provisioned_state_known{false};
  bool provisioned{false};
  bool unused_state_known{false};
  bool unused{false};
  bool read_protection_known{false};
  bool read_protected{false};
  bool write_protection_known{false};
  bool write_protected{false};
  bool purpose_write_protection_known{false};
  bool purpose_write_protected{false};
  bool reservation_state_known{false};
  bool reserved{false};
};

struct InventoryFloorObservation {
  InventoryEvidenceBinding binding{};
  bool observation_present{false};
  bool allocation_map_complete{false};
  bool eligible_user_region_classification_known{false};
  bool eligible_user_region_only{false};
  bool reserved_overlap_known{false};
  bool reserved_overlap_absent{false};
  bool known_field_overlap_known{false};
  bool known_field_overlap_absent{false};
  bool bit_state_known{false};
  bool all_bits_zero{false};
  bool contiguous_capacity_known{false};
  bool contiguous_capacity_sufficient{false};
  bool read_protection_known{false};
  bool read_protected{false};
  bool write_protection_known{false};
  bool write_protected{false};
  bool protection_granularity_known{false};
  bool protection_granularity_compatible{false};
  bool coding_constraints_known{false};
  bool coding_constraints_supported{false};
  bool independent_one_bit_programming_known{false};
  bool independent_one_bit_programming_supported{false};
};

struct ProtectedRootInventoryEvidence {
  InventoryEvidenceBinding binding{};
  bool logical_target_verified{false};
  bool esp32s3_profile_verified{false};
  bool tool_api_version_pinned{false};
  bool complete_key_slot_roster{false};
  std::array<InventoryKeySlotObservation,
             kProtectedRootInventoryKeySlotCount>
      key_slots{};
  bool configured_nvs_binding_observed{false};
  bool configured_nvs_binding_conflict_known{false};
  bool configured_nvs_binding_conflict{false};
  InventoryFloorObservation floor{};
  bool security_context_complete{false};
  bool secure_boot_state_known{false};
  bool secure_boot_enabled{false};
  bool flash_encryption_state_known{false};
  bool flash_encryption_enabled{false};
  bool download_mode_state_known{false};
  bool secure_download_mode_enabled{false};
  bool one_use_consumed{false};
  bool connection_closed{false};
  bool transient_cleanup_verified{false};
  bool reentry_observed{false};
  bool retry_observed{false};

  // These are forbidden claims, not capabilities. Any true value fails closed.
  bool key_material_observed{false};
  bool raw_efuse_output_retained{false};
  bool public_identity_or_transport_detail_present{false};
  bool public_block_or_range_detail_present{false};
  bool provider_selected{false};
  bool physical_admission_granted{false};
  bool provisioning_authorized{false};
  bool protection_authorized{false};
  bool efuse_write_authorized{false};
  bool runtime_activation_authorized{false};
};

// Pure evidence evaluation only. This function performs no I/O and grants no
// selection, physical admission, provisioning, protection, write, or runtime
// authority.
ProtectedRootInventoryDecision EvaluateProtectedRootInventory(
    const ProtectedRootInventoryEvidence& evidence) noexcept;

// Returns one of two fixed public strings and never serializes evidence.
const char* SanitizeProtectedRootInventoryDecision(
    ProtectedRootInventoryDecision decision) noexcept;

}  // namespace opentrail::companion
