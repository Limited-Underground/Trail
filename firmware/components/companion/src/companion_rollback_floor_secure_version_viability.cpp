#include "opentrail/companion_rollback_floor_secure_version_viability.hpp"

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

constexpr std::array<SecureVersionSourceDigest,
                     kSecureVersionViabilitySourceCount>
    kExpectedSourceDigests{
        FixedText<65>(
            "718AE775CE4FEB58A5EB4A0BBAF85E91D4DB9DA1E636F5121FA430AFC2DA9D38"),
        FixedText<65>(
            "6CB345372D4C5786BF023B4D076B62B63BC86CC084242B6B9984F49E2D86A45D"),
        FixedText<65>(
            "E7C04ACDF54CDA0EFF2F2AC7551D6B25CB782E62A2F221C0E4B31DDC37D57AB5"),
        FixedText<65>(
            "A61F5FFF38616B9A6650C7675F149855B0E39B00D2E83651CC5C5ACAE67AE861"),
        FixedText<65>(
            "AFE45475F68C5952BFAEA73E8DC200E963DF4B9036E67EC17418CC4EA2986290"),
        FixedText<65>(
            "0B22F89D2B0F7EE315046DE5108C1DDDA8F46BB451985C7F66EB753301BFA69E"),
        FixedText<65>(
            "9DD27882AE44E1286DDC58967DF906523602A6381A6C39F21B788A5D9EB59303"),
    };

}  // namespace

SecureVersionFloorViabilityEvidence
MakeReviewedEsp32s3SecureVersionEvidence() {
  SecureVersionFloorViabilityEvidence evidence;
  evidence.esp_idf_version = FixedText<8>("6.0.2");
  evidence.target_is_esp32s3 = true;
  evidence.source_sha256 = kExpectedSourceDigests;
  evidence.field_known = true;
  evidence.efuse_block = 0;
  evidence.first_bit = 142;
  evidence.bit_count = 16;
  evidence.coding_scheme_known = true;
  evidence.coding_scheme = SecureVersionCodingScheme::kNone;
  evidence.value_is_popcount = true;
  evidence.update_burns_additional_low_bits = true;
  evidence.maximum_advances = 16;
  evidence.exhaustion_is_permanent = true;
  evidence.native_role_is_firmware_anti_rollback = true;
  evidence.app_header_carries_secure_version = true;
  evidence.bootloader_and_ota_consume_same_field = true;
  evidence.native_partition_model_requires_ota_without_factory = true;
  evidence.current_layout_contains_factory = true;
  evidence.accepted_recovery_route_restores_factory = true;
  evidence.independent_authorization_domain_required = true;
  return evidence;
}

SecureVersionFloorViabilityDecision EvaluateSecureVersionFloorViability(
    const SecureVersionFloorViabilityEvidence& evidence) noexcept {
  if (evidence.esp_idf_version != FixedText<8>("6.0.2") ||
      !evidence.target_is_esp32s3 ||
      evidence.source_sha256 != kExpectedSourceDigests ||
      !evidence.field_known || evidence.efuse_block != 0 ||
      evidence.first_bit != 142 || evidence.bit_count != 16 ||
      !evidence.coding_scheme_known ||
      evidence.coding_scheme != SecureVersionCodingScheme::kNone ||
      !evidence.value_is_popcount ||
      !evidence.update_burns_additional_low_bits ||
      evidence.maximum_advances != 16 ||
      !evidence.exhaustion_is_permanent ||
      !evidence.native_role_is_firmware_anti_rollback ||
      !evidence.app_header_carries_secure_version ||
      !evidence.bootloader_and_ota_consume_same_field ||
      !evidence.native_partition_model_requires_ota_without_factory ||
      !evidence.current_layout_contains_factory ||
      !evidence.accepted_recovery_route_restores_factory ||
      !evidence.independent_authorization_domain_required ||
      evidence.shared_firmware_version_budget_accepted ||
      evidence.sixteen_transition_budget_accepted ||
      evidence.factory_recovery_redesign_accepted ||
      evidence.trusted_firmware_policy_accepted ||
      evidence.descriptor_selected || evidence.provider_admitted ||
      evidence.device_access_authorized || evidence.efuse_read_authorized ||
      evidence.efuse_write_authorized || evidence.provisioning_authorized ||
      evidence.runtime_activation_authorized ||
      evidence.raw_device_data_present ||
      evidence.private_identity_or_path_present) {
    return SecureVersionFloorViabilityDecision::kDeny;
  }
  return SecureVersionFloorViabilityDecision::kReviewedCoupledNotAdmitted;
}

const char* SanitizeSecureVersionFloorViabilityDecision(
    const SecureVersionFloorViabilityDecision decision) noexcept {
  if (decision ==
      SecureVersionFloorViabilityDecision::kReviewedCoupledNotAdmitted) {
    return "REVIEWED-SECURE-VERSION-COUPLED-NOT-ADMITTED";
  }
  return "DENY-SECURE-VERSION-VIABILITY";
}

}  // namespace opentrail::companion
