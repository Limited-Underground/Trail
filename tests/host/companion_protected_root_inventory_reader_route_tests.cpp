#include "opentrail/companion_protected_root_inventory_reader_route.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

using namespace opentrail::companion;

int failures = 0;

void Expect(bool condition, const char* message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <std::size_t Size>
void SetText(std::array<char, Size>& target, const char (&source)[Size]) {
  std::copy(std::begin(source), std::end(source), target.begin());
}

bool Accepted(const ProtectedRootInventoryReaderRoute& route) {
  return EvaluateProtectedRootInventoryReaderRoute(route) ==
         ProtectedRootInventoryReaderRouteDecision::
             kAcceptOfflineMetadataInterfaceExecutionNotAuthorized;
}

ProtectedRootInventoryReaderRoute CompleteRoute() {
  ProtectedRootInventoryReaderRoute route{};
  SetText(route.schema, kProtectedRootInventoryReaderRouteSchema);
  SetText(route.route_id, kProtectedRootInventoryReaderRouteId);
  route.chip = InventoryReaderChip::kEsp32s3;
  route.esp_idf_version = {6U, 0U, 2U};
  route.sources.esp_idf_key_purpose_api_sha256 =
      kEspIdfKeyPurposeApiSha256;
  route.sources.esp_idf_efuse_api_sha256 = kEspIdfEfuseApiSha256;
  route.sources.esp_idf_esp32s3_efuse_table_sha256 =
      kEspIdfEsp32s3EfuseTableSha256;
  route.sources.esp_idf_efuse_header_sha256 = kEspIdfEfuseHeaderSha256;
  route.sources.esp_idf_esp32s3_efuse_chip_header_sha256 =
      kEspIdfEsp32s3EfuseChipHeaderSha256;
  route.interface = InventoryReaderInterface::
      kAuditedTargetSideEspIdfMetadataAdapterRequired;
  route.metadata_interface_only = true;
  route.target_side_adapter_required = true;
  route.api.get_key_purpose = true;
  route.api.get_key_dis_read = true;
  route.api.get_key_dis_write = true;
  route.api.get_keypurpose_dis_write = true;
  route.api.key_block_unused = true;
  route.metadata.complete_six_slot_roster_required = true;
  route.metadata.key_purpose_metadata_required = true;
  route.metadata.key_provisioned_and_unused_metadata_required = true;
  route.metadata.key_protection_metadata_required = true;
  route.metadata.configured_nvs_binding_and_conflict_required = true;
  route.metadata.security_states_known_and_disabled_required = true;
  route.metadata.floor_offline_allocation_constraints_recorded = true;
  route.metadata.floor_offline_coding_constraints_recorded = true;
  route.metadata.floor_exact_nonsecret_descriptor_required_before_read = true;
  route.metadata.floor_physical_facts_pending = true;
  route.private_nonzero_operation_binding_required = true;
  route.private_nonzero_evidence_binding_required = true;
  route.same_operation_evidence_binding_required = true;
  route.future_one_use_consumed_on_failure = true;
  route.future_transient_cleanup_required = true;
  route.privacy.host_python_full_efuse_route_reviewed = true;
  route.privacy.host_python_full_efuse_route_materializes_raw_key_blocks = true;
  route.privacy.host_python_full_efuse_route_rejected = true;
  route.privacy.fixed_sanitized_public_output = true;
  return route;
}

void TestIdentityAndFrozenSources() {
  Expect(!Accepted(ProtectedRootInventoryReaderRoute{}),
         "default route must deny");
  Expect(Accepted(CompleteRoute()), "exact offline route must pass");

  auto Denies = [](auto mutate, const char* message) {
    auto route = CompleteRoute();
    mutate(route);
    Expect(!Accepted(route), message);
  };
  Denies([](auto& r) { r.schema[0] = 'X'; }, "schema drift");
  Denies([](auto& r) { r.route_id[0] = 'X'; }, "route id drift");
  Denies([](auto& r) { r.chip = InventoryReaderChip::kUnknown; },
         "unknown chip");
  Denies([](auto& r) { r.chip = static_cast<InventoryReaderChip>(255); },
         "malformed chip");
  Denies([](auto& r) { ++r.esp_idf_version.major; }, "ESP-IDF major drift");
  Denies([](auto& r) { ++r.esp_idf_version.minor; }, "ESP-IDF minor drift");
  Denies([](auto& r) { ++r.esp_idf_version.patch; }, "ESP-IDF patch drift");
  Denies([](auto& r) { r.sources.esp_idf_key_purpose_api_sha256[0] ^= 1U; },
         "key-purpose source drift");
  Denies([](auto& r) { r.sources.esp_idf_efuse_api_sha256[0] ^= 1U; },
         "eFuse source drift");
  Denies([](auto& r) {
           r.sources.esp_idf_esp32s3_efuse_table_sha256[0] ^= 1U;
         },
         "ESP32-S3 table drift");
  Denies([](auto& r) { r.sources.esp_idf_efuse_header_sha256[0] ^= 1U; },
         "eFuse header drift");
  Denies([](auto& r) {
           r.sources.esp_idf_esp32s3_efuse_chip_header_sha256[0] ^= 1U;
         },
         "ESP32-S3 chip header drift");
  Denies([](auto& r) { r.interface = InventoryReaderInterface::kUnknown; },
         "unknown metadata interface");
  Denies([](auto& r) {
           r.interface = static_cast<InventoryReaderInterface>(255);
         },
         "malformed metadata interface");
}

void TestInterfaceRemainsNonExecutable() {
  auto Denies = [](auto mutate, const char* message) {
    auto route = CompleteRoute();
    mutate(route);
    Expect(!Accepted(route), message);
  };
  Denies([](auto& r) { r.metadata_interface_only = false; },
         "executable route implied");
  Denies([](auto& r) { r.target_side_adapter_required = false; },
         "target adapter not required");
  Denies([](auto& r) { r.target_side_adapter_present = true; },
         "unreviewed target adapter present");
  Denies([](auto& r) { r.physical_execution_possible = true; },
         "physical execution implied");
  Denies([](auto& r) { r.floor_descriptor_selected = true; },
         "floor descriptor selected");
  Denies([](auto& r) { r.floor_physical_read_available = true; },
         "floor physical read claimed");
  Denies([](auto& r) { r.private_nonzero_operation_binding_required = false; },
         "operation binding absent");
  Denies([](auto& r) { r.private_nonzero_evidence_binding_required = false; },
         "evidence binding absent");
  Denies([](auto& r) { r.same_operation_evidence_binding_required = false; },
         "same-operation binding absent");
  Denies([](auto& r) { r.future_one_use_consumed_on_failure = false; },
         "failure does not consume one use");
  Denies([](auto& r) { r.future_transient_cleanup_required = false; },
         "future cleanup not required");
}

void TestExactDecodedApiAllowlist() {
  using Member = bool ProtectedRootInventoryReaderApiAllowlist::*;
  const std::array<Member, 5U> required{
      &ProtectedRootInventoryReaderApiAllowlist::get_key_purpose,
      &ProtectedRootInventoryReaderApiAllowlist::get_key_dis_read,
      &ProtectedRootInventoryReaderApiAllowlist::get_key_dis_write,
      &ProtectedRootInventoryReaderApiAllowlist::get_keypurpose_dis_write,
      &ProtectedRootInventoryReaderApiAllowlist::key_block_unused};
  for (const auto member : required) {
    auto route = CompleteRoute();
    route.api.*member = false;
    Expect(!Accepted(route), "missing decoded key API must deny");
  }
  const std::array<Member, 5U> forbidden{
      &ProtectedRootInventoryReaderApiAllowlist::get_or_read_raw_key_block,
      &ProtectedRootInventoryReaderApiAllowlist::raw_block_or_dump_api,
      &ProtectedRootInventoryReaderApiAllowlist::hmac_operation,
      &ProtectedRootInventoryReaderApiAllowlist::any_write_set_burn_protect_or_destroy_api,
      &ProtectedRootInventoryReaderApiAllowlist::any_unlisted_api};
  for (const auto member : forbidden) {
    auto route = CompleteRoute();
    route.api.*member = true;
    Expect(!Accepted(route), "forbidden API surface must deny");
  }
}

void TestMetadataScope() {
  using Member = bool ProtectedRootInventoryReaderMetadataScope::*;
  const std::array<Member, 10U> required{
      &ProtectedRootInventoryReaderMetadataScope::complete_six_slot_roster_required,
      &ProtectedRootInventoryReaderMetadataScope::key_purpose_metadata_required,
      &ProtectedRootInventoryReaderMetadataScope::key_provisioned_and_unused_metadata_required,
      &ProtectedRootInventoryReaderMetadataScope::key_protection_metadata_required,
      &ProtectedRootInventoryReaderMetadataScope::configured_nvs_binding_and_conflict_required,
      &ProtectedRootInventoryReaderMetadataScope::security_states_known_and_disabled_required,
      &ProtectedRootInventoryReaderMetadataScope::floor_offline_allocation_constraints_recorded,
      &ProtectedRootInventoryReaderMetadataScope::floor_offline_coding_constraints_recorded,
      &ProtectedRootInventoryReaderMetadataScope::floor_exact_nonsecret_descriptor_required_before_read,
      &ProtectedRootInventoryReaderMetadataScope::floor_physical_facts_pending};
  for (const auto member : required) {
    auto route = CompleteRoute();
    route.metadata.*member = false;
    Expect(!Accepted(route), "missing metadata scope must deny");
  }
  auto extra = CompleteRoute();
  extra.metadata.any_unbounded_or_extra_metadata = true;
  Expect(!Accepted(extra), "unbounded metadata must deny");
}

void TestHostRoutePrivacyAndAuthority() {
  auto Denies = [](auto mutate, const char* message) {
    auto route = CompleteRoute();
    mutate(route);
    Expect(!Accepted(route), message);
  };
  Denies([](auto& r) { r.privacy.host_python_full_efuse_route_reviewed = false; },
         "unsafe host route not reviewed");
  Denies([](auto& r) {
           r.privacy.host_python_full_efuse_route_materializes_raw_key_blocks =
               false;
         },
         "unsafe host behavior not recorded");
  Denies([](auto& r) { r.privacy.host_python_full_efuse_route_rejected = false; },
         "unsafe host route not rejected");
  Denies([](auto& r) { r.privacy.host_python_full_efuse_route_allowed = true; },
         "unsafe host route allowed");
  Denies([](auto& r) { r.privacy.raw_key_material_requested = true; },
         "key material requested");
  Denies([](auto& r) { r.privacy.raw_key_material_emitted_or_retained = true; },
         "key material emitted");
  Denies([](auto& r) { r.privacy.raw_key_digest_emitted_or_retained = true; },
         "key digest emitted");
  Denies([](auto& r) { r.privacy.raw_key_block_emitted_or_retained = true; },
         "raw key block emitted");
  Denies([](auto& r) { r.privacy.raw_floor_bitmap_emitted_or_retained = true; },
         "floor bitmap emitted");
  Denies([](auto& r) { r.privacy.raw_unbounded_output_emitted_or_retained = true; },
         "unbounded output emitted");
  Denies([](auto& r) { r.privacy.public_identity_or_transport_detail = true; },
         "public identity detail");
  Denies([](auto& r) { r.privacy.public_block_or_range_detail = true; },
         "public allocation detail");
  Denies([](auto& r) { r.privacy.fixed_sanitized_public_output = false; },
         "nonfixed public output");

  using Authority = bool ProtectedRootInventoryReaderAuthority::*;
  const std::array<Authority, 15U> authorities{
      &ProtectedRootInventoryReaderAuthority::owner_authorized,
      &ProtectedRootInventoryReaderAuthority::device_access_authorized,
      &ProtectedRootInventoryReaderAuthority::connection_authorized,
      &ProtectedRootInventoryReaderAuthority::reset_or_bootloader_authorized,
      &ProtectedRootInventoryReaderAuthority::adapter_deployment_authorized,
      &ProtectedRootInventoryReaderAuthority::security_metadata_read_authorized,
      &ProtectedRootInventoryReaderAuthority::key_metadata_read_authorized,
      &ProtectedRootInventoryReaderAuthority::floor_metadata_read_authorized,
      &ProtectedRootInventoryReaderAuthority::raw_dump_authorized,
      &ProtectedRootInventoryReaderAuthority::key_material_read_authorized,
      &ProtectedRootInventoryReaderAuthority::efuse_write_or_burn_authorized,
      &ProtectedRootInventoryReaderAuthority::protection_change_authorized,
      &ProtectedRootInventoryReaderAuthority::provisioning_authorized,
      &ProtectedRootInventoryReaderAuthority::provider_admission_authorized,
      &ProtectedRootInventoryReaderAuthority::runtime_integration_authorized};
  for (const auto member : authorities) {
    auto route = CompleteRoute();
    route.authority.*member = true;
    Expect(!Accepted(route), "offline route must deny every authority");
  }
}

void TestSanitizerAndDeterminism() {
  const auto route = CompleteRoute();
  const auto decision = EvaluateProtectedRootInventoryReaderRoute(route);
  Expect(std::strcmp(SanitizeProtectedRootInventoryReaderRouteDecision(decision),
                     "OFFLINE-METADATA-INTERFACE-ACCEPTED-EXECUTION-NOT-AUTHORIZED") == 0,
         "pass output must be fixed");
  Expect(std::strcmp(SanitizeProtectedRootInventoryReaderRouteDecision(
                         ProtectedRootInventoryReaderRouteDecision::kDeny),
                     "DENY-READER-ROUTE") == 0,
         "deny output must be fixed");
  Expect(std::strcmp(SanitizeProtectedRootInventoryReaderRouteDecision(
                         static_cast<ProtectedRootInventoryReaderRouteDecision>(255)),
                     "DENY-READER-ROUTE") == 0,
         "unknown decisions must sanitize to denial");
  for (int repeat = 0; repeat < 100; ++repeat) {
    Expect(Accepted(route), "evaluation must be deterministic");
  }
}

}  // namespace

int main() {
  TestIdentityAndFrozenSources();
  TestInterfaceRemainsNonExecutable();
  TestExactDecodedApiAllowlist();
  TestMetadataScope();
  TestHostRoutePrivacyAndAuthority();
  TestSanitizerAndDeterminism();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "companion protected-root inventory reader-route tests passed\n";
  return EXIT_SUCCESS;
}
