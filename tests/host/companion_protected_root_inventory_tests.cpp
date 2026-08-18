#include "opentrail/companion_protected_root_inventory.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

using opentrail::companion::InventoryEvidenceBinding;
using opentrail::companion::InventoryKeyPurpose;
using opentrail::companion::ProtectedRootInventoryDecision;
using opentrail::companion::ProtectedRootInventoryEvidence;

int failures = 0;

void Expect(bool condition, const char* message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool IsAccepted(const ProtectedRootInventoryEvidence& evidence) {
  return opentrail::companion::EvaluateProtectedRootInventory(evidence) ==
         ProtectedRootInventoryDecision::
             kAcceptCompletePrivateInventoryForReviewOnly;
}

ProtectedRootInventoryEvidence CompleteEvidence() {
  ProtectedRootInventoryEvidence evidence{};
  evidence.binding = InventoryEvidenceBinding{41U, 79U, true};
  evidence.logical_target_verified = true;
  evidence.esp32s3_profile_verified = true;
  evidence.tool_api_version_pinned = true;
  evidence.complete_key_slot_roster = true;
  for (auto& slot : evidence.key_slots) {
    slot.binding = evidence.binding;
    slot.observation_present = true;
    slot.purpose_known = true;
    slot.purpose = InventoryKeyPurpose::kUser;
    slot.provisioned_state_known = true;
    slot.provisioned = false;
    slot.unused_state_known = true;
    slot.unused = true;
    slot.read_protection_known = true;
    slot.read_protected = false;
    slot.write_protection_known = true;
    slot.write_protected = false;
    slot.purpose_write_protection_known = true;
    slot.purpose_write_protected = false;
    slot.reservation_state_known = true;
    slot.reserved = false;
  }
  // A complete inventory may contain occupied or reserved slots; capture is
  // still valid and no selection follows from this evidence.
  evidence.key_slots[1].unused = false;
  evidence.key_slots[1].purpose = InventoryKeyPurpose::kHmacUp;
  evidence.key_slots[1].provisioned = true;
  evidence.key_slots[1].read_protected = true;
  evidence.key_slots[1].write_protected = true;
  evidence.key_slots[1].purpose_write_protected = true;
  evidence.key_slots[4].unused = false;
  evidence.key_slots[4].reserved = true;

  evidence.configured_nvs_binding_observed = true;
  evidence.configured_nvs_binding_conflict_known = true;
  evidence.configured_nvs_binding_conflict = false;
  evidence.floor.binding = evidence.binding;
  evidence.floor.observation_present = true;
  evidence.floor.allocation_map_complete = true;
  evidence.floor.eligible_user_region_classification_known = true;
  evidence.floor.eligible_user_region_only = true;
  evidence.floor.reserved_overlap_known = true;
  evidence.floor.reserved_overlap_absent = true;
  evidence.floor.known_field_overlap_known = true;
  evidence.floor.known_field_overlap_absent = true;
  evidence.floor.bit_state_known = true;
  evidence.floor.all_bits_zero = true;
  evidence.floor.contiguous_capacity_known = true;
  evidence.floor.contiguous_capacity_sufficient = true;
  evidence.floor.read_protection_known = true;
  evidence.floor.read_protected = false;
  evidence.floor.write_protection_known = true;
  evidence.floor.write_protected = false;
  evidence.floor.protection_granularity_known = true;
  evidence.floor.protection_granularity_compatible = true;
  evidence.floor.coding_constraints_known = true;
  evidence.floor.coding_constraints_supported = true;
  evidence.floor.independent_one_bit_programming_known = true;
  evidence.floor.independent_one_bit_programming_supported = true;
  evidence.security_context_complete = true;
  evidence.secure_boot_state_known = true;
  evidence.secure_boot_enabled = false;
  evidence.flash_encryption_state_known = true;
  evidence.flash_encryption_enabled = false;
  evidence.download_mode_state_known = true;
  evidence.secure_download_mode_enabled = false;
  evidence.one_use_consumed = true;
  evidence.connection_closed = true;
  evidence.transient_cleanup_verified = true;
  return evidence;
}

void TestDefaultAndExactAcceptance() {
  Expect(!IsAccepted(ProtectedRootInventoryEvidence{}),
         "default evidence must deny");
  const auto evidence = CompleteEvidence();
  Expect(IsAccepted(evidence), "complete synthetic evidence must be captured");
  const auto decision =
      opentrail::companion::EvaluateProtectedRootInventory(evidence);
  Expect(std::strcmp(
             opentrail::companion::SanitizeProtectedRootInventoryDecision(
                 decision),
             "PRIVATE-INVENTORY-CAPTURED-SELECTION-PENDING") == 0,
         "accepted public result must be the fixed pending string");
  Expect(std::strcmp(
             opentrail::companion::SanitizeProtectedRootInventoryDecision(
                 ProtectedRootInventoryDecision::kDeny),
             "DENY-INVENTORY") == 0,
         "denial must use one fixed public string");
  Expect(std::strcmp(
             opentrail::companion::SanitizeProtectedRootInventoryDecision(
                 static_cast<ProtectedRootInventoryDecision>(255)),
             "DENY-INVENTORY") == 0,
         "unknown decisions must sanitize as denial");
}

void TestCompleteInventoryWithoutViableProviderIsReviewable() {
  auto evidence = CompleteEvidence();
  for (auto& slot : evidence.key_slots) {
    slot.purpose = InventoryKeyPurpose::kHmacUp;
    slot.provisioned = true;
    slot.unused = false;
    slot.read_protected = true;
    slot.write_protected = true;
    slot.purpose_write_protected = true;
    slot.reserved = true;
  }
  evidence.configured_nvs_binding_conflict = true;
  evidence.floor.eligible_user_region_only = false;
  evidence.floor.reserved_overlap_absent = false;
  evidence.floor.known_field_overlap_absent = false;
  evidence.floor.all_bits_zero = false;
  evidence.floor.contiguous_capacity_sufficient = false;
  evidence.floor.read_protected = true;
  evidence.floor.write_protected = true;
  evidence.floor.protection_granularity_compatible = false;
  evidence.floor.coding_constraints_supported = false;
  evidence.floor.independent_one_bit_programming_supported = false;
  Expect(IsAccepted(evidence),
         "complete no-provider inventory must remain review-only evidence");
}

void TestTopLevelGates() {
  auto Denies = [](auto mutate, const char* message) {
    auto evidence = CompleteEvidence();
    mutate(evidence);
    Expect(!IsAccepted(evidence), message);
  };

  Denies([](auto& e) { e.binding.operation_id = 0U; }, "zero operation id");
  Denies([](auto& e) { e.binding.evidence_id = 0U; }, "zero evidence id");
  Denies([](auto& e) { e.binding.fresh = false; }, "stale root evidence");
  Denies([](auto& e) { e.logical_target_verified = false; }, "target unknown");
  Denies([](auto& e) { e.esp32s3_profile_verified = false; }, "profile unknown");
  Denies([](auto& e) { e.tool_api_version_pinned = false; }, "tool unpinned");
  Denies([](auto& e) { e.complete_key_slot_roster = false; }, "roster incomplete");
  Denies([](auto& e) { e.configured_nvs_binding_observed = false; },
         "NVS binding unknown");
  Denies([](auto& e) { e.configured_nvs_binding_conflict_known = false; },
         "NVS binding conflict state unknown");
  Denies([](auto& e) { e.security_context_complete = false; },
         "security context incomplete");
  Denies([](auto& e) { e.secure_boot_state_known = false; },
         "secure boot unknown");
  Denies([](auto& e) { e.secure_boot_enabled = true; },
         "secure boot state mismatch");
  Denies([](auto& e) { e.flash_encryption_state_known = false; },
         "flash encryption unknown");
  Denies([](auto& e) { e.flash_encryption_enabled = true; },
         "flash encryption state mismatch");
  Denies([](auto& e) { e.download_mode_state_known = false; },
         "download mode unknown");
  Denies([](auto& e) { e.secure_download_mode_enabled = true; },
         "secure download mode state mismatch");
  Denies([](auto& e) { e.one_use_consumed = false; }, "one-use not consumed");
  Denies([](auto& e) { e.connection_closed = false; }, "connection not closed");
  Denies([](auto& e) { e.transient_cleanup_verified = false; },
         "cleanup unverified");
  Denies([](auto& e) { e.reentry_observed = true; }, "reentry");
  Denies([](auto& e) { e.retry_observed = true; }, "retry");
}

void TestKeyRosterGates() {
  auto Denies = [](auto mutate, const char* message) {
    auto evidence = CompleteEvidence();
    mutate(evidence.key_slots[0], evidence);
    Expect(!IsAccepted(evidence), message);
  };

  Denies([](auto& s, auto&) { s.observation_present = false; }, "slot absent");
  Denies([](auto& s, auto&) { s.purpose_known = false; }, "purpose unknown");
  Denies([](auto& s, auto&) { s.purpose = InventoryKeyPurpose::kUnknown; },
         "unknown purpose enum");
  Denies([](auto& s, auto&) {
           s.purpose = static_cast<InventoryKeyPurpose>(255);
         },
         "malformed purpose enum");
  Denies([](auto& s, auto&) { s.unused_state_known = false; }, "unused unknown");
  Denies([](auto& s, auto&) { s.provisioned_state_known = false; },
         "provisioning unknown");
  Denies([](auto& s, auto&) { s.read_protection_known = false; },
         "read protection unknown");
  Denies([](auto& s, auto&) { s.write_protection_known = false; },
         "write protection unknown");
  Denies([](auto& s, auto&) { s.purpose_write_protection_known = false; },
         "purpose protection unknown");
  Denies([](auto& s, auto&) { s.reservation_state_known = false; },
         "reservation unknown");
  Denies([](auto& s, auto&) { s.binding.fresh = false; }, "stale slot binding");
  Denies([](auto& s, auto&) { ++s.binding.operation_id; },
         "mixed slot operation");
  Denies([](auto& s, auto&) { ++s.binding.evidence_id; },
         "mixed slot evidence");
  Denies([](auto& s, auto&) {
           s.purpose = InventoryKeyPurpose::kHmacUp;
         },
         "unused HMAC_UP contradiction");
  Denies([](auto& s, auto&) { s.read_protected = true; },
         "unused read-protected contradiction");
  Denies([](auto& s, auto&) { s.write_protected = true; },
         "unused write-protected contradiction");
  Denies([](auto& s, auto&) { s.purpose_write_protected = true; },
         "unused purpose-protected contradiction");
  Denies([](auto& s, auto&) { s.reserved = true; },
         "unused reserved contradiction");
  Denies([](auto& s, auto&) { s.provisioned = true; },
         "unused provisioned contradiction");
}

void TestFloorGates() {
  auto Denies = [](auto mutate, const char* message) {
    auto evidence = CompleteEvidence();
    mutate(evidence.floor);
    Expect(!IsAccepted(evidence), message);
  };

  Denies([](auto& f) { f.observation_present = false; }, "floor absent");
  Denies([](auto& f) { f.allocation_map_complete = false; }, "map incomplete");
  Denies([](auto& f) {
           f.eligible_user_region_classification_known = false;
         },
         "floor region classification unknown");
  Denies([](auto& f) { f.reserved_overlap_known = false; },
         "reserved overlap unknown");
  Denies([](auto& f) { f.known_field_overlap_known = false; },
         "field overlap unknown");
  Denies([](auto& f) { f.bit_state_known = false; }, "bit state unknown");
  Denies([](auto& f) { f.contiguous_capacity_known = false; },
         "capacity unknown");
  Denies([](auto& f) { f.read_protection_known = false; },
         "floor read protection unknown");
  Denies([](auto& f) { f.write_protection_known = false; },
         "floor write protection unknown");
  Denies([](auto& f) { f.protection_granularity_known = false; },
         "protection granularity unknown");
  Denies([](auto& f) { f.coding_constraints_known = false; },
         "coding unknown");
  Denies([](auto& f) { f.independent_one_bit_programming_known = false; },
         "one-bit behavior unknown");
  Denies([](auto& f) { f.binding.operation_id = 0U; }, "floor zero operation");
  Denies([](auto& f) { ++f.binding.evidence_id; }, "mixed floor evidence");
  Denies([](auto& f) { f.binding.fresh = false; }, "stale floor evidence");
}

void TestForbiddenClaimsAndDeterminism() {
  auto Denies = [](auto mutate, const char* message) {
    auto evidence = CompleteEvidence();
    mutate(evidence);
    Expect(!IsAccepted(evidence), message);
  };

  Denies([](auto& e) { e.key_material_observed = true; }, "key material");
  Denies([](auto& e) { e.raw_efuse_output_retained = true; }, "raw output");
  Denies([](auto& e) { e.public_identity_or_transport_detail_present = true; },
         "public identity or transport detail");
  Denies([](auto& e) { e.public_block_or_range_detail_present = true; },
         "public allocation detail");
  Denies([](auto& e) { e.provider_selected = true; }, "provider selection");
  Denies([](auto& e) { e.physical_admission_granted = true; },
         "physical admission");
  Denies([](auto& e) { e.provisioning_authorized = true; }, "provisioning");
  Denies([](auto& e) { e.protection_authorized = true; }, "protection");
  Denies([](auto& e) { e.efuse_write_authorized = true; }, "eFuse write");
  Denies([](auto& e) { e.runtime_activation_authorized = true; }, "runtime");

  const auto evidence = CompleteEvidence();
  for (int index = 0; index < 100; ++index) {
    Expect(IsAccepted(evidence), "evaluation must be deterministic");
  }
}

}  // namespace

int main() {
  TestDefaultAndExactAcceptance();
  TestCompleteInventoryWithoutViableProviderIsReviewable();
  TestTopLevelGates();
  TestKeyRosterGates();
  TestFloorGates();
  TestForbiddenClaimsAndDeterminism();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "companion protected-root inventory tests passed\n";
  return EXIT_SUCCESS;
}
