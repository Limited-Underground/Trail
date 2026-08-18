#include "opentrail/companion_protected_root_inventory_reader_route.hpp"

#include <algorithm>

namespace opentrail::companion {
namespace {

template <std::size_t Size>
bool ExactText(const std::array<char, Size>& value,
               const char (&expected)[Size]) noexcept {
  return std::equal(value.begin(), value.end(), expected);
}

bool ExactSources(
    const ProtectedRootInventoryReaderSourceEvidence& sources) noexcept {
  return sources.esp_idf_key_purpose_api_sha256 ==
             kEspIdfKeyPurposeApiSha256 &&
         sources.esp_idf_efuse_api_sha256 == kEspIdfEfuseApiSha256 &&
         sources.esp_idf_esp32s3_efuse_table_sha256 ==
             kEspIdfEsp32s3EfuseTableSha256 &&
         sources.esp_idf_efuse_header_sha256 == kEspIdfEfuseHeaderSha256 &&
         sources.esp_idf_esp32s3_efuse_chip_header_sha256 ==
             kEspIdfEsp32s3EfuseChipHeaderSha256;
}

bool ExactApiAllowlist(
    const ProtectedRootInventoryReaderApiAllowlist& api) noexcept {
  return api.get_key_purpose && api.get_key_dis_read &&
         api.get_key_dis_write && api.get_keypurpose_dis_write &&
         api.key_block_unused && !api.get_or_read_raw_key_block &&
         !api.raw_block_or_dump_api && !api.hmac_operation &&
         !api.any_write_set_burn_protect_or_destroy_api &&
         !api.any_unlisted_api;
}

bool ExactMetadataScope(
    const ProtectedRootInventoryReaderMetadataScope& metadata) noexcept {
  return metadata.complete_six_slot_roster_required &&
         metadata.key_purpose_metadata_required &&
         metadata.key_provisioned_and_unused_metadata_required &&
         metadata.key_protection_metadata_required &&
         metadata.configured_nvs_binding_and_conflict_required &&
         metadata.security_states_known_and_disabled_required &&
         metadata.floor_offline_allocation_constraints_recorded &&
         metadata.floor_offline_coding_constraints_recorded &&
         metadata.floor_exact_nonsecret_descriptor_required_before_read &&
         metadata.floor_physical_facts_pending &&
         !metadata.any_unbounded_or_extra_metadata;
}

bool PrivacyClosed(
    const ProtectedRootInventoryReaderPrivacyPolicy& privacy) noexcept {
  return privacy.host_python_full_efuse_route_reviewed &&
         privacy.host_python_full_efuse_route_materializes_raw_key_blocks &&
         privacy.host_python_full_efuse_route_rejected &&
         !privacy.host_python_full_efuse_route_allowed &&
         !privacy.raw_key_material_requested &&
         !privacy.raw_key_material_emitted_or_retained &&
         !privacy.raw_key_digest_emitted_or_retained &&
         !privacy.raw_key_block_emitted_or_retained &&
         !privacy.raw_floor_bitmap_emitted_or_retained &&
         !privacy.raw_unbounded_output_emitted_or_retained &&
         !privacy.public_identity_or_transport_detail &&
         !privacy.public_block_or_range_detail &&
         privacy.fixed_sanitized_public_output;
}

bool AllAuthorityAbsent(
    const ProtectedRootInventoryReaderAuthority& authority) noexcept {
  return !authority.owner_authorized &&
         !authority.device_access_authorized &&
         !authority.connection_authorized &&
         !authority.reset_or_bootloader_authorized &&
         !authority.adapter_deployment_authorized &&
         !authority.security_metadata_read_authorized &&
         !authority.key_metadata_read_authorized &&
         !authority.floor_metadata_read_authorized &&
         !authority.raw_dump_authorized &&
         !authority.key_material_read_authorized &&
         !authority.efuse_write_or_burn_authorized &&
         !authority.protection_change_authorized &&
         !authority.provisioning_authorized &&
         !authority.provider_admission_authorized &&
         !authority.runtime_integration_authorized;
}

}  // namespace

ProtectedRootInventoryReaderRouteDecision
EvaluateProtectedRootInventoryReaderRoute(
    const ProtectedRootInventoryReaderRoute& route) noexcept {
  if (!ExactText(route.schema, kProtectedRootInventoryReaderRouteSchema) ||
      !ExactText(route.route_id, kProtectedRootInventoryReaderRouteId) ||
      route.chip != InventoryReaderChip::kEsp32s3 ||
      route.esp_idf_version.major != 6U ||
      route.esp_idf_version.minor != 0U ||
      route.esp_idf_version.patch != 2U || !ExactSources(route.sources) ||
      route.interface != InventoryReaderInterface::
                             kAuditedTargetSideEspIdfMetadataAdapterRequired ||
      !route.metadata_interface_only || !route.target_side_adapter_required ||
      route.target_side_adapter_present ||
      route.physical_execution_possible || route.floor_descriptor_selected ||
      route.floor_physical_read_available || !ExactApiAllowlist(route.api) ||
      !ExactMetadataScope(route.metadata) ||
      !route.private_nonzero_operation_binding_required ||
      !route.private_nonzero_evidence_binding_required ||
      !route.same_operation_evidence_binding_required ||
      !route.future_one_use_consumed_on_failure ||
      !route.future_transient_cleanup_required ||
      !PrivacyClosed(route.privacy) || !AllAuthorityAbsent(route.authority)) {
    return ProtectedRootInventoryReaderRouteDecision::kDeny;
  }
  return ProtectedRootInventoryReaderRouteDecision::
      kAcceptOfflineMetadataInterfaceExecutionNotAuthorized;
}

const char* SanitizeProtectedRootInventoryReaderRouteDecision(
    ProtectedRootInventoryReaderRouteDecision decision) noexcept {
  if (decision == ProtectedRootInventoryReaderRouteDecision::
                      kAcceptOfflineMetadataInterfaceExecutionNotAuthorized) {
    return kProtectedRootInventoryReaderRouteAccepted;
  }
  return kProtectedRootInventoryReaderRouteDenied;
}

}  // namespace opentrail::companion
