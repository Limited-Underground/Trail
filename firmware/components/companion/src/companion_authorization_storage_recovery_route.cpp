#include "opentrail/companion_authorization_storage_recovery_route.hpp"

#include <algorithm>

namespace opentrail::companion {
namespace {

template <std::size_t N, std::size_t M>
constexpr std::array<char, N> fixed_text(const char (&value)[M]) {
  static_assert(M <= N, "fixed recovery-route text does not fit");
  std::array<char, N> result{};
  for (std::size_t index = 0; index < M; ++index) {
    result[index] = value[index];
  }
  return result;
}

constexpr RecoverySha256 kApplicationSha256{
    0xA7, 0xD8, 0xE6, 0x72, 0xCF, 0x91, 0x69, 0xF1,
    0xD1, 0xD4, 0xE8, 0x6E, 0xEF, 0xF8, 0x03, 0x99,
    0xC4, 0x7A, 0x14, 0x5E, 0x7D, 0x64, 0x90, 0x4C,
    0x20, 0x7D, 0xD5, 0xF1, 0xB2, 0x3F, 0x35, 0x9B,
};

constexpr RecoverySha256 kPartitionTableSha256{
    0x84, 0x56, 0x9A, 0xA2, 0xBA, 0xDF, 0x3F, 0x72,
    0x94, 0x04, 0x21, 0x29, 0xB1, 0x9D, 0x0B, 0x48,
    0x07, 0x84, 0xA9, 0x3A, 0x55, 0x0A, 0xDA, 0x32,
    0x53, 0xB5, 0x7B, 0xC9, 0x2A, 0x06, 0x71, 0xAB,
};

constexpr RecoveryRegion kApplicationRegion{
    0x00010000U,
    470928U,
    kApplicationSha256,
};

constexpr RecoveryRegion kPartitionTableRegion{
    0x00008000U,
    3072U,
    kPartitionTableSha256,
};

bool same_region(const RecoveryRegion& left, const RecoveryRegion& right) {
  return left.offset == right.offset && left.length == right.length &&
         left.sha256 == right.sha256;
}

bool nonzero(const RecoveryBindingId& value) {
  return std::any_of(value.begin(), value.end(),
                     [](std::uint8_t byte) { return byte != 0U; });
}

}  // namespace

CompanionAuthorizationStorageRecoveryRoute
make_companion_authorization_storage_recovery_route() {
  CompanionAuthorizationStorageRecoveryRoute route{};
  route.schema = fixed_text<16>("OTRR0/v0");
  route.route_id =
      fixed_text<64>("OTRR0/v0/heltec-v4-ot064-source-restore");
  route.target = fixed_text<32>("heltec_v4_bench");
  route.transport = fixed_text<32>("ESP32-S3-ROM-SERIAL");
  route.esptool_version = fixed_text<16>("5.3.1");
  route.baud_rate = 115200U;
  route.maximum_connection_attempts = 1U;
  route.ram_stub = false;
  route.before_write_no_reset = true;
  route.after_write_no_reset = true;
  route.software_reset = false;
  route.full_erase = false;
  route.writes_keys = false;
  route.writes_efuses = false;
  route.writes_other_regions = false;

  // Application first, partition table last. The restored partition map is not
  // authoritative until the exact application bytes are already present.
  route.ordered_writes = {kApplicationRegion, kPartitionTableRegion};

  route.preflight.chip = fixed_text<16>("ESP32-S3");
  route.preflight.esp_idf_version = fixed_text<16>("6.0.2");
  route.preflight.partition_generator = fixed_text<32>("gen_esp32part");
  route.preflight.flash_size_bytes = 16U * 1024U * 1024U;
  route.preflight.source_identity_bound = false;
  route.preflight.candidate_identity_bound = false;
  route.preflight.security_state_admission_explicit = true;
  route.preflight.security_state.fresh = false;
  route.preflight.security_state.operation_id_bound = false;
  route.preflight.security_state.evidence_id_bound = false;
  route.preflight.security_state.secure_boot_known = false;
  route.preflight.security_state.flash_encryption_known = false;
  route.preflight.security_state.secure_download_mode_known = false;
  route.preflight.security_state.secure_boot_enabled = false;
  route.preflight.security_state.flash_encryption_enabled = false;
  route.preflight.security_state.secure_download_mode_enabled = false;
  route.preflight.source_partition_artifact_available = false;
  route.preflight.source_partition_artifact_verified = false;
  route.preflight.source_partition_deterministically_generated = true;
  route.preflight.private_application_custody_available = false;
  route.preflight.private_application_custody_verified = false;
  route.preflight.second_private_application_copy_independently_hashed = false;
  route.preflight.private_application_digest_bound = false;
  route.preflight.source_partition_region_is_all_ff = false;
  route.preflight.authorization_not_initialized = false;
  route.preflight.operation_id_fresh = false;
  route.preflight.evidence_id_fresh = false;

  route.postwrite.close_rom_connection_before_readback = true;
  route.postwrite.independent_readback = true;
  route.postwrite.readback_regions = {kApplicationRegion,
                                      kPartitionTableRegion};
  route.postwrite.post_first_write_ambiguity_is_recovery_uncertain = true;
  route.postwrite.post_first_write_failure.close_rom_connection = true;
  route.postwrite.post_first_write_failure.remain_in_rom_download_mode = true;
  route.postwrite.post_first_write_failure.no_reset = true;
  route.postwrite.post_first_write_failure.no_boot_success_claim = true;
  route.postwrite.post_first_write_failure.retain_private_artifacts = true;
  route.postwrite.post_first_write_failure.retain_minimum_private_journal = true;
  route.postwrite.post_first_write_failure.sanitized_public_result = true;
  route.postwrite.post_first_write_failure.transient_cleanup_only_after_evidence_preserved = true;
  route.postwrite.post_first_write_failure.no_automatic_retry = true;
  route.postwrite.post_first_write_failure.fresh_authorization_for_retry = true;
  route.postwrite.manual_owner_reset = true;
  route.postwrite.boot_observation = true;
  route.postwrite.require_self_check_pass = true;
  route.postwrite.require_trail_logo = true;
  route.postwrite.require_ble_advertising = true;
  route.postwrite.require_heartbeat = true;
  route.postwrite.minimum_heartbeats = 2U;
  route.postwrite.heartbeat_window_seconds = 12U;

  route.authority.offline_contract_authorized = true;
  route.authority.physical_execution_authorized = false;
  route.authority.recovery_write_authorized = false;
  route.authority.key_write_authorized = false;
  route.authority.efuse_write_authorized = false;
  return route;
}

RecoveryRouteDecision evaluate_companion_authorization_storage_recovery_route(
    const CompanionAuthorizationStorageRecoveryRoute& route) {
  const auto exact = make_companion_authorization_storage_recovery_route();

  if (route.schema != exact.schema || route.route_id != exact.route_id ||
      route.target != exact.target) {
    return RecoveryRouteDecision::kDenyRouteIdentity;
  }

  if (route.transport != exact.transport ||
      route.esptool_version != exact.esptool_version ||
      route.baud_rate != 115200U || route.maximum_connection_attempts != 1U ||
      route.ram_stub || !route.before_write_no_reset ||
      !route.after_write_no_reset || route.software_reset || route.full_erase ||
      route.writes_keys || route.writes_efuses || route.writes_other_regions) {
    return RecoveryRouteDecision::kDenyTransportPolicy;
  }

  if (!same_region(route.ordered_writes[0], kApplicationRegion) ||
      !same_region(route.ordered_writes[1], kPartitionTableRegion)) {
    return RecoveryRouteDecision::kDenyWritePlan;
  }

  const auto& preflight = route.preflight;
  if (preflight.chip != exact.preflight.chip ||
      preflight.flash_size_bytes != 16U * 1024U * 1024U ||
      preflight.esp_idf_version != exact.preflight.esp_idf_version ||
      preflight.partition_generator != exact.preflight.partition_generator ||
      !preflight.source_identity_bound ||
      !preflight.candidate_identity_bound ||
      !preflight.security_state_admission_explicit ||
      !preflight.security_state.fresh ||
      !preflight.security_state.operation_id_bound ||
      !preflight.security_state.evidence_id_bound ||
      !preflight.security_state.secure_boot_known ||
      !preflight.security_state.flash_encryption_known ||
      !preflight.security_state.secure_download_mode_known ||
      preflight.security_state.secure_boot_enabled ||
      preflight.security_state.flash_encryption_enabled ||
      preflight.security_state.secure_download_mode_enabled ||
      !preflight.source_partition_artifact_available ||
      !preflight.source_partition_artifact_verified ||
      !preflight.private_application_custody_available ||
      !preflight.private_application_custody_verified ||
      !preflight.source_partition_deterministically_generated ||
      !preflight.private_application_digest_bound ||
      !preflight.source_partition_region_is_all_ff ||
      !preflight.second_private_application_copy_independently_hashed ||
      !preflight.authorization_not_initialized ||
      !preflight.operation_id_fresh || !preflight.evidence_id_fresh ||
      !nonzero(preflight.operation_id) || !nonzero(preflight.evidence_id) ||
      preflight.operation_id != preflight.bound_operation_id ||
      preflight.evidence_id != preflight.bound_evidence_id) {
    return RecoveryRouteDecision::kDenyPreflight;
  }

  const auto& postwrite = route.postwrite;
  if (!postwrite.close_rom_connection_before_readback ||
      !postwrite.independent_readback ||
      !same_region(postwrite.readback_regions[0], kApplicationRegion) ||
      !same_region(postwrite.readback_regions[1], kPartitionTableRegion) ||
      !postwrite.post_first_write_ambiguity_is_recovery_uncertain ||
      !postwrite.manual_owner_reset || !postwrite.boot_observation ||
      !postwrite.post_first_write_failure.close_rom_connection ||
      !postwrite.post_first_write_failure.remain_in_rom_download_mode ||
      !postwrite.post_first_write_failure.no_reset ||
      !postwrite.post_first_write_failure.no_boot_success_claim ||
      !postwrite.post_first_write_failure.retain_private_artifacts ||
      !postwrite.post_first_write_failure.retain_minimum_private_journal ||
      !postwrite.post_first_write_failure.sanitized_public_result ||
      !postwrite.post_first_write_failure.transient_cleanup_only_after_evidence_preserved ||
      !postwrite.post_first_write_failure.no_automatic_retry ||
      !postwrite.post_first_write_failure.fresh_authorization_for_retry ||
      !postwrite.require_self_check_pass ||
      !postwrite.require_trail_logo ||
      !postwrite.require_ble_advertising || !postwrite.require_heartbeat ||
      postwrite.minimum_heartbeats != 2U ||
      postwrite.heartbeat_window_seconds != 12U) {
    return RecoveryRouteDecision::kDenyPostwriteRequirements;
  }

  if (!route.authority.offline_contract_authorized ||
      route.authority.physical_execution_authorized ||
      route.authority.recovery_write_authorized ||
      route.authority.key_write_authorized ||
      route.authority.efuse_write_authorized) {
    return RecoveryRouteDecision::kDenyAuthority;
  }

  return RecoveryRouteDecision::kAcceptOfflineContract;
}

}  // namespace opentrail::companion
