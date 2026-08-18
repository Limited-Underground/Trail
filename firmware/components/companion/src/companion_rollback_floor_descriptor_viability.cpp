#include "opentrail/companion_rollback_floor_descriptor_viability.hpp"

#include <algorithm>
#include <string_view>

namespace opentrail::companion {
namespace {

template <std::size_t N>
constexpr std::array<char, N> FixedText(const std::string_view value) {
  std::array<char, N> result{};
  const auto count = std::min(value.size(), N - 1);
  for (std::size_t index = 0; index < count; ++index) {
    result[index] = value[index];
  }
  return result;
}

constexpr std::array<RollbackFloorSourceDigest,
                     kRollbackFloorViabilitySourceCount>
    kExpectedSourceDigests{
        FixedText<65>(
            "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E"),
        FixedText<65>(
            "B5299EE67627C912C5E7A0E4A908D1678FD0D2F12D5AFD7A58D849FC1BADAA30"),
        FixedText<65>(
            "87EF1EA4E0B17AFEF9AB8E8939E67649A934C4CD3531FAFD07D343139B1B8E64"),
        FixedText<65>(
            "4D488D3F2A75F0E55B903410987E08DCBAA550E11344032E93B315BEC87648A7"),
        FixedText<65>(
            "477703495E87597CC55DB87C78DB71399199187789624C9397F8BBF53002E9E2"),
    };

}  // namespace

RollbackFloorDescriptorViabilityEvidence
MakeReviewedEsp32s3UserDataThermometerEvidence() {
  RollbackFloorDescriptorViabilityEvidence evidence;
  evidence.esp_idf_version = FixedText<8>("6.0.2");
  evidence.target_is_esp32s3 = true;
  evidence.source_sha256 = kExpectedSourceDigests;
  evidence.user_data_field_known = true;
  evidence.user_data_first_bit = 0;
  evidence.user_data_bit_count = 256;
  evidence.mac_custom_overlap_known = true;
  evidence.mac_custom_first_bit = 200;
  evidence.mac_custom_bit_count = 48;
  evidence.coding_scheme_known = true;
  evidence.coding_scheme = RollbackFloorCodingScheme::kReedSolomon;
  evidence.coding_unit_single_write_known = true;
  evidence.coding_unit_single_write = true;
  evidence.repeated_independent_advances_required = true;
  return evidence;
}

RollbackFloorDescriptorViabilityDecision
EvaluateRollbackFloorDescriptorViability(
    const RollbackFloorDescriptorViabilityEvidence& evidence) noexcept {
  if (evidence.esp_idf_version != FixedText<8>("6.0.2") ||
      !evidence.target_is_esp32s3 ||
      evidence.source_sha256 != kExpectedSourceDigests ||
      !evidence.user_data_field_known || evidence.user_data_first_bit != 0 ||
      evidence.user_data_bit_count != 256 ||
      !evidence.mac_custom_overlap_known ||
      evidence.mac_custom_first_bit != 200 ||
      evidence.mac_custom_bit_count != 48 ||
      !evidence.coding_scheme_known ||
      evidence.coding_scheme != RollbackFloorCodingScheme::kReedSolomon ||
      !evidence.coding_unit_single_write_known ||
      !evidence.coding_unit_single_write ||
      !evidence.repeated_independent_advances_required ||
      evidence.descriptor_selected || evidence.provider_admitted ||
      evidence.device_access_authorized || evidence.efuse_read_authorized ||
      evidence.efuse_write_authorized || evidence.provisioning_authorized ||
      evidence.runtime_activation_authorized ||
      evidence.raw_device_data_present ||
      evidence.private_identity_or_path_present) {
    return RollbackFloorDescriptorViabilityDecision::kDeny;
  }
  return RollbackFloorDescriptorViabilityDecision::
      kReviewedCustomThermometerIncompatible;
}

const char* SanitizeRollbackFloorDescriptorViabilityDecision(
    const RollbackFloorDescriptorViabilityDecision decision) noexcept {
  if (decision == RollbackFloorDescriptorViabilityDecision::
                      kReviewedCustomThermometerIncompatible) {
    return "REVIEWED-NO-VIABLE-CUSTOM-THERMOMETER";
  }
  return "DENY-DESCRIPTOR-VIABILITY";
}

}  // namespace opentrail::companion
