#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

inline constexpr std::size_t kSecureVersionViabilitySourceCount = 7;
using SecureVersionSourceDigest = std::array<char, 65>;

enum class SecureVersionCodingScheme : std::uint8_t {
  kUnknown = 0,
  kNone,
  kReedSolomon,
};

struct SecureVersionFloorViabilityEvidence {
  std::array<char, 8> esp_idf_version{};
  bool target_is_esp32s3{false};
  std::array<SecureVersionSourceDigest,
             kSecureVersionViabilitySourceCount>
      source_sha256{};

  bool field_known{false};
  std::uint8_t efuse_block{0};
  std::uint16_t first_bit{0};
  std::uint16_t bit_count{0};
  bool coding_scheme_known{false};
  SecureVersionCodingScheme coding_scheme{
      SecureVersionCodingScheme::kUnknown};
  bool value_is_popcount{false};
  bool update_burns_additional_low_bits{false};
  std::uint16_t maximum_advances{0};
  bool exhaustion_is_permanent{false};

  bool native_role_is_firmware_anti_rollback{false};
  bool app_header_carries_secure_version{false};
  bool bootloader_and_ota_consume_same_field{false};
  bool native_partition_model_requires_ota_without_factory{false};
  bool current_layout_contains_factory{false};
  bool accepted_recovery_route_restores_factory{false};
  bool independent_authorization_domain_required{false};
  bool shared_firmware_version_budget_accepted{false};
  bool sixteen_transition_budget_accepted{false};
  bool factory_recovery_redesign_accepted{false};
  bool trusted_firmware_policy_accepted{false};

  bool descriptor_selected{false};
  bool provider_admitted{false};
  bool device_access_authorized{false};
  bool efuse_read_authorized{false};
  bool efuse_write_authorized{false};
  bool provisioning_authorized{false};
  bool runtime_activation_authorized{false};
  bool raw_device_data_present{false};
  bool private_identity_or_path_present{false};
};

enum class SecureVersionFloorViabilityDecision : std::uint8_t {
  kDeny = 0,
  kReviewedCoupledNotAdmitted,
};

SecureVersionFloorViabilityEvidence
MakeReviewedEsp32s3SecureVersionEvidence();

SecureVersionFloorViabilityDecision EvaluateSecureVersionFloorViability(
    const SecureVersionFloorViabilityEvidence& evidence) noexcept;

const char* SanitizeSecureVersionFloorViabilityDecision(
    SecureVersionFloorViabilityDecision decision) noexcept;

}  // namespace opentrail::companion
