#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

using RecoverySha256 = std::array<std::uint8_t, 32>;
using RecoveryBindingId = std::array<std::uint8_t, 16>;

struct RecoveryRegion {
  std::uint32_t offset;
  std::uint32_t length;
  RecoverySha256 sha256;
};

struct RecoverySecurityStateObservation {
  bool fresh;
  bool operation_id_bound;
  bool evidence_id_bound;
  bool secure_boot_known;
  bool flash_encryption_known;
  bool secure_download_mode_known;
  bool secure_boot_enabled;
  bool flash_encryption_enabled;
  bool secure_download_mode_enabled;
};

struct RecoveryPostFirstWriteFailurePolicy {
  bool close_rom_connection;
  bool remain_in_rom_download_mode;
  bool no_reset;
  bool no_boot_success_claim;
  bool retain_private_artifacts;
  bool retain_minimum_private_journal;
  bool sanitized_public_result;
  bool transient_cleanup_only_after_evidence_preserved;
  bool no_automatic_retry;
  bool fresh_authorization_for_retry;
};

struct RecoveryPreflight {
  std::array<char, 16> chip;
  std::array<char, 16> esp_idf_version;
  std::array<char, 32> partition_generator;
  std::uint32_t flash_size_bytes;
  bool source_identity_bound;
  bool candidate_identity_bound;
  bool private_application_digest_bound;
  bool security_state_admission_explicit;
  RecoverySecurityStateObservation security_state;
  bool source_partition_artifact_available;
  bool source_partition_artifact_verified;
  bool source_partition_deterministically_generated;
  bool private_application_custody_available;
  bool private_application_custody_verified;
  bool second_private_application_copy_independently_hashed;
  bool source_partition_region_is_all_ff;
  bool authorization_not_initialized;
  bool operation_id_fresh;
  bool evidence_id_fresh;
  RecoveryBindingId operation_id;
  RecoveryBindingId bound_operation_id;
  RecoveryBindingId evidence_id;
  RecoveryBindingId bound_evidence_id;
};

struct RecoveryPostwriteRequirements {
  bool close_rom_connection_before_readback;
  bool independent_readback;
  std::array<RecoveryRegion, 2> readback_regions;
  bool manual_owner_reset;
  bool boot_observation;
  bool post_first_write_ambiguity_is_recovery_uncertain;
  RecoveryPostFirstWriteFailurePolicy post_first_write_failure;
  bool require_self_check_pass;
  bool require_trail_logo;
  bool require_ble_advertising;
  bool require_heartbeat;
  std::uint8_t minimum_heartbeats;
  std::uint8_t heartbeat_window_seconds;
};

struct RecoveryAuthority {
  bool offline_contract_authorized;
  bool physical_execution_authorized;
  bool recovery_write_authorized;
  bool key_write_authorized;
  bool efuse_write_authorized;
};

struct CompanionAuthorizationStorageRecoveryRoute {
  std::array<char, 16> schema;
  std::array<char, 64> route_id;
  std::array<char, 32> target;
  std::array<char, 32> transport;
  std::array<char, 16> esptool_version;
  std::uint32_t baud_rate;
  std::uint8_t maximum_connection_attempts;
  bool ram_stub;
  bool before_write_no_reset;
  bool after_write_no_reset;
  bool software_reset;
  bool full_erase;
  bool writes_keys;
  bool writes_efuses;
  bool writes_other_regions;
  std::array<RecoveryRegion, 2> ordered_writes;
  RecoveryPreflight preflight;
  RecoveryPostwriteRequirements postwrite;
  RecoveryAuthority authority;
};

enum class RecoveryRouteDecision : std::uint8_t {
  kAcceptOfflineContract,
  kDenyRouteIdentity,
  kDenyTransportPolicy,
  kDenyWritePlan,
  kDenyPreflight,
  kDenyPostwriteRequirements,
  kDenyAuthority,
};

// Returns the one exact OT-077 route. The binding identifiers are deliberately
// zero: a caller must replace them with fresh, nonzero values and bind the
// preflight evidence to the same values before evaluation can accept the route.
CompanionAuthorizationStorageRecoveryRoute
make_companion_authorization_storage_recovery_route();

// Pure, target-neutral validation only. Acceptance means the offline route
// contract is internally complete; it never grants physical execution or write
// authority and performs no I/O.
RecoveryRouteDecision evaluate_companion_authorization_storage_recovery_route(
    const CompanionAuthorizationStorageRecoveryRoute& route);

}  // namespace opentrail::companion
