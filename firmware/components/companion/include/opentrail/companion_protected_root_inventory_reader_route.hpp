#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

inline constexpr char kProtectedRootInventoryReaderRouteSchema[] = "OTPRR0/v0";
inline constexpr char kProtectedRootInventoryReaderRouteId[] =
    "OTPRR0/v0/esp32s3-esp-idf-6.0.2-metadata-adapter";
inline constexpr char kProtectedRootInventoryReaderRouteAccepted[] =
    "OFFLINE-METADATA-INTERFACE-ACCEPTED-EXECUTION-NOT-AUTHORIZED";
inline constexpr char kProtectedRootInventoryReaderRouteDenied[] =
    "DENY-READER-ROUTE";

using InventoryReaderRouteDigest = std::array<std::uint8_t, 32U>;

inline constexpr InventoryReaderRouteDigest kEspIdfKeyPurposeApiSha256{
    0xCD, 0x6C, 0x54, 0x62, 0xCB, 0x1B, 0x2A, 0xDF,
    0xE7, 0x73, 0x59, 0x15, 0x81, 0x04, 0x61, 0xED,
    0xE9, 0x6E, 0xCF, 0x0B, 0x83, 0x0A, 0x07, 0x61,
    0xE4, 0xCA, 0xF2, 0xE6, 0xCB, 0x98, 0x2C, 0x73};
inline constexpr InventoryReaderRouteDigest kEspIdfEfuseApiSha256{
    0x66, 0xA1, 0x2F, 0xA2, 0x8B, 0x11, 0x64, 0x23,
    0x85, 0xC5, 0x4A, 0x24, 0x9C, 0xE8, 0xEB, 0xEE,
    0x13, 0x9B, 0xF7, 0xA5, 0xBF, 0x56, 0x2B, 0x9D,
    0x5A, 0xFF, 0x29, 0xA3, 0xB8, 0xCF, 0x3F, 0x4};
inline constexpr InventoryReaderRouteDigest kEspIdfEsp32s3EfuseTableSha256{
    0x0B, 0x22, 0xF8, 0x9D, 0x2B, 0x0F, 0x7E, 0xE3,
    0x15, 0x04, 0x6D, 0xE5, 0x10, 0x8C, 0x1D, 0xDD,
    0xA8, 0xF4, 0x6B, 0xB4, 0x51, 0x98, 0x5C, 0x7F,
    0x66, 0xEB, 0x75, 0x33, 0x01, 0xBF, 0xA6, 0x9E};
inline constexpr InventoryReaderRouteDigest kEspIdfEfuseHeaderSha256{
    0x4D, 0x48, 0x8D, 0x3F, 0x2A, 0x75, 0xF0, 0xE5,
    0x5B, 0x90, 0x34, 0x10, 0x98, 0x7E, 0x08, 0xDC,
    0xBA, 0xA5, 0x50, 0xE1, 0x13, 0x44, 0x03, 0x2E,
    0x93, 0xB3, 0x15, 0xBE, 0xC8, 0x76, 0x48, 0xA7};
inline constexpr InventoryReaderRouteDigest kEspIdfEsp32s3EfuseChipHeaderSha256{
    0xB5, 0x29, 0x9E, 0xE6, 0x76, 0x27, 0xC9, 0x12,
    0xC5, 0xE7, 0xA0, 0xE4, 0xA9, 0x08, 0xD1, 0x67,
    0x8F, 0xD0, 0xD2, 0xF1, 0x2D, 0x5A, 0xFD, 0x7A,
    0x58, 0xD8, 0x49, 0xFC, 0x1B, 0xAD, 0xAA, 0x30};

enum class InventoryReaderChip : std::uint8_t {
  kUnknown = 0,
  kEsp32s3 = 1,
};

enum class InventoryReaderInterface : std::uint8_t {
  kUnknown = 0,
  kAuditedTargetSideEspIdfMetadataAdapterRequired = 1,
};

enum class ProtectedRootInventoryReaderRouteDecision : std::uint8_t {
  kDeny = 0,
  kAcceptOfflineMetadataInterfaceExecutionNotAuthorized = 1,
};

struct InventoryReaderVersion {
  std::uint16_t major{0U};
  std::uint16_t minor{0U};
  std::uint16_t patch{0U};
};

struct ProtectedRootInventoryReaderSourceEvidence {
  InventoryReaderRouteDigest esp_idf_key_purpose_api_sha256{};
  InventoryReaderRouteDigest esp_idf_efuse_api_sha256{};
  InventoryReaderRouteDigest esp_idf_esp32s3_efuse_table_sha256{};
  InventoryReaderRouteDigest esp_idf_efuse_header_sha256{};
  InventoryReaderRouteDigest esp_idf_esp32s3_efuse_chip_header_sha256{};
};

struct ProtectedRootInventoryReaderApiAllowlist {
  bool get_key_purpose{false};
  bool get_key_dis_read{false};
  bool get_key_dis_write{false};
  bool get_keypurpose_dis_write{false};
  bool key_block_unused{false};
  bool get_or_read_raw_key_block{false};
  bool raw_block_or_dump_api{false};
  bool hmac_operation{false};
  bool any_write_set_burn_protect_or_destroy_api{false};
  bool any_unlisted_api{false};
};

struct ProtectedRootInventoryReaderMetadataScope {
  bool complete_six_slot_roster_required{false};
  bool key_purpose_metadata_required{false};
  bool key_provisioned_and_unused_metadata_required{false};
  bool key_protection_metadata_required{false};
  bool configured_nvs_binding_and_conflict_required{false};
  bool security_states_known_and_disabled_required{false};
  bool floor_offline_allocation_constraints_recorded{false};
  bool floor_offline_coding_constraints_recorded{false};
  bool floor_exact_nonsecret_descriptor_required_before_read{false};
  bool floor_physical_facts_pending{false};
  bool any_unbounded_or_extra_metadata{false};
};

struct ProtectedRootInventoryReaderPrivacyPolicy {
  bool host_python_full_efuse_route_reviewed{false};
  bool host_python_full_efuse_route_materializes_raw_key_blocks{false};
  bool host_python_full_efuse_route_rejected{false};
  bool host_python_full_efuse_route_allowed{false};
  bool raw_key_material_requested{false};
  bool raw_key_material_emitted_or_retained{false};
  bool raw_key_digest_emitted_or_retained{false};
  bool raw_key_block_emitted_or_retained{false};
  bool raw_floor_bitmap_emitted_or_retained{false};
  bool raw_unbounded_output_emitted_or_retained{false};
  bool public_identity_or_transport_detail{false};
  bool public_block_or_range_detail{false};
  bool fixed_sanitized_public_output{false};
};

struct ProtectedRootInventoryReaderAuthority {
  bool owner_authorized{false};
  bool device_access_authorized{false};
  bool connection_authorized{false};
  bool reset_or_bootloader_authorized{false};
  bool adapter_deployment_authorized{false};
  bool security_metadata_read_authorized{false};
  bool key_metadata_read_authorized{false};
  bool floor_metadata_read_authorized{false};
  bool raw_dump_authorized{false};
  bool key_material_read_authorized{false};
  bool efuse_write_or_burn_authorized{false};
  bool protection_change_authorized{false};
  bool provisioning_authorized{false};
  bool provider_admission_authorized{false};
  bool runtime_integration_authorized{false};
};

struct ProtectedRootInventoryReaderRoute {
  std::array<char, sizeof(kProtectedRootInventoryReaderRouteSchema)> schema{};
  std::array<char, sizeof(kProtectedRootInventoryReaderRouteId)> route_id{};
  InventoryReaderChip chip{InventoryReaderChip::kUnknown};
  InventoryReaderVersion esp_idf_version{};
  ProtectedRootInventoryReaderSourceEvidence sources{};
  InventoryReaderInterface interface{InventoryReaderInterface::kUnknown};
  bool metadata_interface_only{false};
  bool target_side_adapter_required{false};
  bool target_side_adapter_present{false};
  bool physical_execution_possible{false};
  bool floor_descriptor_selected{false};
  bool floor_physical_read_available{false};
  ProtectedRootInventoryReaderApiAllowlist api{};
  ProtectedRootInventoryReaderMetadataScope metadata{};
  bool private_nonzero_operation_binding_required{false};
  bool private_nonzero_evidence_binding_required{false};
  bool same_operation_evidence_binding_required{false};
  bool future_one_use_consumed_on_failure{false};
  bool future_transient_cleanup_required{false};
  ProtectedRootInventoryReaderPrivacyPolicy privacy{};
  ProtectedRootInventoryReaderAuthority authority{};
};

ProtectedRootInventoryReaderRouteDecision
EvaluateProtectedRootInventoryReaderRoute(
    const ProtectedRootInventoryReaderRoute& route) noexcept;

const char* SanitizeProtectedRootInventoryReaderRouteDecision(
    ProtectedRootInventoryReaderRouteDecision decision) noexcept;

}  // namespace opentrail::companion
