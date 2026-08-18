#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::companion {

inline constexpr std::size_t kRollbackFloorViabilitySourceCount = 5;
using RollbackFloorSourceDigest = std::array<char, 65>;

enum class RollbackFloorCodingScheme : std::uint8_t {
  kUnknown = 0,
  kNone,
  kReedSolomon,
};

struct RollbackFloorDescriptorViabilityEvidence {
  std::array<char, 8> esp_idf_version{};
  bool target_is_esp32s3{false};
  std::array<RollbackFloorSourceDigest,
             kRollbackFloorViabilitySourceCount>
      source_sha256{};
  bool user_data_field_known{false};
  std::uint16_t user_data_first_bit{0};
  std::uint16_t user_data_bit_count{0};
  bool mac_custom_overlap_known{false};
  std::uint16_t mac_custom_first_bit{0};
  std::uint16_t mac_custom_bit_count{0};
  bool coding_scheme_known{false};
  RollbackFloorCodingScheme coding_scheme{
      RollbackFloorCodingScheme::kUnknown};
  bool coding_unit_single_write_known{false};
  bool coding_unit_single_write{false};
  bool repeated_independent_advances_required{false};

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

enum class RollbackFloorDescriptorViabilityDecision : std::uint8_t {
  kDeny = 0,
  kReviewedCustomThermometerIncompatible,
};

RollbackFloorDescriptorViabilityEvidence
MakeReviewedEsp32s3UserDataThermometerEvidence();

RollbackFloorDescriptorViabilityDecision
EvaluateRollbackFloorDescriptorViability(
    const RollbackFloorDescriptorViabilityEvidence& evidence) noexcept;

const char* SanitizeRollbackFloorDescriptorViabilityDecision(
    RollbackFloorDescriptorViabilityDecision decision) noexcept;

}  // namespace opentrail::companion
