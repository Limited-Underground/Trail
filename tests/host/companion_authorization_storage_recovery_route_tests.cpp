#include "opentrail/companion_authorization_storage_recovery_route.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using opentrail::companion::CompanionAuthorizationStorageRecoveryRoute;
using opentrail::companion::RecoveryRouteDecision;
using opentrail::companion::evaluate_companion_authorization_storage_recovery_route;
using opentrail::companion::make_companion_authorization_storage_recovery_route;

int failures = 0;

void expect(const std::string& name,
            const CompanionAuthorizationStorageRecoveryRoute& route,
            RecoveryRouteDecision expected) {
  const auto actual =
      evaluate_companion_authorization_storage_recovery_route(route);
  if (actual != expected) {
    ++failures;
    std::cerr << "FAIL: " << name << " expected "
              << static_cast<int>(expected) << " got "
              << static_cast<int>(actual) << '\n';
  }
}

CompanionAuthorizationStorageRecoveryRoute bound_route() {
  auto route = make_companion_authorization_storage_recovery_route();
  route.preflight.source_identity_bound = true;
  route.preflight.candidate_identity_bound = true;
  route.preflight.security_state.fresh = true;
  route.preflight.security_state.operation_id_bound = true;
  route.preflight.security_state.evidence_id_bound = true;
  route.preflight.security_state.secure_boot_known = true;
  route.preflight.security_state.flash_encryption_known = true;
  route.preflight.security_state.secure_download_mode_known = true;
  route.preflight.source_partition_artifact_available = true;
  route.preflight.source_partition_artifact_verified = true;
  route.preflight.private_application_custody_available = true;
  route.preflight.private_application_custody_verified = true;
  route.preflight.second_private_application_copy_independently_hashed = true;
  route.preflight.private_application_digest_bound = true;
  route.preflight.source_partition_region_is_all_ff = true;
  route.preflight.authorization_not_initialized = true;
  route.preflight.operation_id_fresh = true;
  route.preflight.evidence_id_fresh = true;
  route.preflight.security_state.secure_boot_enabled = false;
  route.preflight.security_state.flash_encryption_enabled = false;
  for (std::size_t index = 0; index < route.preflight.operation_id.size();
       ++index) {
    route.preflight.operation_id[index] =
        static_cast<std::uint8_t>(0x10U + index);
    route.preflight.evidence_id[index] =
        static_cast<std::uint8_t>(0x80U + index);
  }
  route.preflight.bound_operation_id = route.preflight.operation_id;
  route.preflight.bound_evidence_id = route.preflight.evidence_id;
  return route;
}

template <typename Mutator>
void denied(const std::string& name,
            RecoveryRouteDecision expected,
            Mutator mutate) {
  auto route = bound_route();
  mutate(route);
  expect(name, route, expected);
}

void route_identity_tests() {
  denied("schema mismatch", RecoveryRouteDecision::kDenyRouteIdentity,
         [](auto& route) { route.schema[0] ^= 1; });
  denied("route id mismatch", RecoveryRouteDecision::kDenyRouteIdentity,
         [](auto& route) { route.route_id[0] ^= 1; });
  denied("target mismatch", RecoveryRouteDecision::kDenyRouteIdentity,
         [](auto& route) { route.target[0] ^= 1; });
}

void transport_tests() {
  denied("transport mismatch", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.transport[0] ^= 1; });
  denied("tool version mismatch", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.esptool_version[0] ^= 1; });
  denied("wrong baud", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.baud_rate = 460800U; });
  denied("retry", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.maximum_connection_attempts = 2U; });
  denied("RAM stub", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.ram_stub = true; });
  denied("before reset allowed", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.before_write_no_reset = false; });
  denied("after reset allowed", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.after_write_no_reset = false; });
  denied("software reset", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.software_reset = true; });
  denied("full erase", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.full_erase = true; });
  denied("key write", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.writes_keys = true; });
  denied("efuse write", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.writes_efuses = true; });
  denied("extra region", RecoveryRouteDecision::kDenyTransportPolicy,
         [](auto& route) { route.writes_other_regions = true; });
}

void write_plan_tests() {
  denied("partition table cannot be first",
         RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) {
           const auto first = route.ordered_writes[0];
           route.ordered_writes[0] = route.ordered_writes[1];
           route.ordered_writes[1] = first;
         });
  denied("application offset", RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) { route.ordered_writes[0].offset += 1U; });
  denied("application length", RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) { route.ordered_writes[0].length -= 1U; });
  denied("application digest", RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) { route.ordered_writes[0].sha256[0] ^= 1U; });
  denied("partition offset", RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) { route.ordered_writes[1].offset += 1U; });
  denied("partition length", RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) { route.ordered_writes[1].length -= 1U; });
  denied("partition digest", RecoveryRouteDecision::kDenyWritePlan,
         [](auto& route) { route.ordered_writes[1].sha256[31] ^= 1U; });
}

void preflight_tests() {
  denied("chip", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.chip[0] ^= 1; });
  denied("flash size", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.flash_size_bytes /= 2U; });
  denied("source identity", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.source_identity_bound = false; });
  denied("candidate identity", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.candidate_identity_bound = false; });
  denied("security admission", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state_admission_explicit = false;
         });
  denied("security observation stale", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.security_state.fresh = false; });
  denied("security operation unbound", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.operation_id_bound = false;
         });
  denied("security evidence unbound", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.evidence_id_bound = false;
         });
  denied("secure boot unknown", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.secure_boot_known = false;
         });
  denied("flash encryption unknown", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.flash_encryption_known = false;
         });
  denied("secure download unknown", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.secure_download_mode_known = false;
         });
  denied("secure boot enabled", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.secure_boot_enabled = true;
         });
  denied("flash encryption enabled", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.flash_encryption_enabled = true;
         });
  denied("secure download enabled", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.security_state.secure_download_mode_enabled = true;
         });
  denied("partition unavailable", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.source_partition_artifact_available = false;
         });
  denied("partition unverified", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.source_partition_artifact_verified = false;
         });
  denied("partition not deterministic", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.source_partition_deterministically_generated = false;
         });
  denied("IDF version", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.esp_idf_version[0] ^= 1; });
  denied("partition generator", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.partition_generator[0] ^= 1; });
  denied("private application custody", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.private_application_custody_available = false;
         });
  denied("private application unverified",
         RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.private_application_custody_verified = false;
         });
  denied("second application copy not independently hashed",
         RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.second_private_application_copy_independently_hashed =
               false;
         });
  denied("private application digest unbound",
         RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.private_application_digest_bound = false;
         });
  denied("partition region not erased", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.source_partition_region_is_all_ff = false;
         });
  denied("authorization initialized", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.authorization_not_initialized = false;
         });
  denied("operation id stale", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.operation_id_fresh = false; });
  denied("evidence id stale", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.evidence_id_fresh = false; });
  denied("zero operation id", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.operation_id.fill(0U);
           route.preflight.bound_operation_id.fill(0U);
         });
  denied("zero evidence id", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) {
           route.preflight.evidence_id.fill(0U);
           route.preflight.bound_evidence_id.fill(0U);
         });
  denied("operation binding mismatch", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.bound_operation_id[0] ^= 1U; });
  denied("evidence binding mismatch", RecoveryRouteDecision::kDenyPreflight,
         [](auto& route) { route.preflight.bound_evidence_id[0] ^= 1U; });
}

void postwrite_tests() {
  denied("connection remains open",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.close_rom_connection_before_readback = false;
         });
  denied("readback not independent",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.independent_readback = false; });
  denied("application readback range",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.readback_regions[0].length -= 1U; });
  denied("partition readback digest",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.readback_regions[1].sha256[0] ^= 1U;
         });
  denied("ambiguous first write not uncertain",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_ambiguity_is_recovery_uncertain =
               false;
         });
  denied("failure leaves connection open",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.close_rom_connection = false;
         });
  denied("failure exits ROM mode",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.remain_in_rom_download_mode = false;
         });
  denied("failure permits reset",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.no_reset = false;
         });
  denied("failure permits boot claim",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.no_boot_success_claim = false;
         });
  denied("failure discards private artifacts",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.retain_private_artifacts = false;
         });
  denied("failure discards private journal",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.retain_minimum_private_journal = false;
         });
  denied("failure leaks public detail",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.sanitized_public_result = false;
         });
  denied("failure cleans before evidence",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.transient_cleanup_only_after_evidence_preserved = false;
         });
  denied("failure permits automatic retry",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.no_automatic_retry = false;
         });
  denied("failure reuses authorization",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.post_first_write_failure.fresh_authorization_for_retry = false;
         });
  denied("automatic reset allowed",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.manual_owner_reset = false; });
  denied("no boot observation",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.boot_observation = false; });
  denied("self-check omitted",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.require_self_check_pass = false; });
  denied("logo omitted", RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.require_trail_logo = false; });
  denied("BLE advertising omitted",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) {
           route.postwrite.require_ble_advertising = false;
         });
  denied("heartbeat omitted",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.require_heartbeat = false; });
  denied("heartbeat count reduced",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.minimum_heartbeats = 1U; });
  denied("heartbeat window expanded",
         RecoveryRouteDecision::kDenyPostwriteRequirements,
         [](auto& route) { route.postwrite.heartbeat_window_seconds = 13U; });
}

void authority_tests() {
  denied("offline contract missing", RecoveryRouteDecision::kDenyAuthority,
         [](auto& route) {
           route.authority.offline_contract_authorized = false;
         });
  denied("physical execution authority", RecoveryRouteDecision::kDenyAuthority,
         [](auto& route) {
           route.authority.physical_execution_authorized = true;
         });
  denied("recovery write authority", RecoveryRouteDecision::kDenyAuthority,
         [](auto& route) {
           route.authority.recovery_write_authorized = true;
         });
  denied("key authority", RecoveryRouteDecision::kDenyAuthority,
         [](auto& route) { route.authority.key_write_authorized = true; });
  denied("efuse authority", RecoveryRouteDecision::kDenyAuthority,
         [](auto& route) { route.authority.efuse_write_authorized = true; });
}

}  // namespace

int main() {
  auto unbound = make_companion_authorization_storage_recovery_route();
  expect("unbound default denies", unbound,
         RecoveryRouteDecision::kDenyPreflight);
  expect("exact bound offline contract accepts", bound_route(),
         RecoveryRouteDecision::kAcceptOfflineContract);

  route_identity_tests();
  transport_tests();
  write_plan_tests();
  preflight_tests();
  postwrite_tests();
  authority_tests();

  if (failures != 0) {
    std::cerr << failures << " recovery-route test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: companion authorization storage recovery route\n";
  return 0;
}
