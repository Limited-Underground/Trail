#include "opentrail/companion_rollback_floor_descriptor_viability.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using opentrail::companion::EvaluateRollbackFloorDescriptorViability;
using opentrail::companion::MakeReviewedEsp32s3UserDataThermometerEvidence;
using opentrail::companion::RollbackFloorCodingScheme;
using opentrail::companion::RollbackFloorDescriptorViabilityDecision;
using opentrail::companion::RollbackFloorDescriptorViabilityEvidence;
using opentrail::companion::SanitizeRollbackFloorDescriptorViabilityDecision;

int failures = 0;

void Expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void ExpectDenied(const RollbackFloorDescriptorViabilityEvidence& evidence,
                  const std::string& message) {
  Expect(EvaluateRollbackFloorDescriptorViability(evidence) ==
             RollbackFloorDescriptorViabilityDecision::kDeny,
         message);
}

void TestExactReviewedIncompatibility() {
  const RollbackFloorDescriptorViabilityEvidence empty;
  ExpectDenied(empty, "default evidence must deny");

  const auto reviewed = MakeReviewedEsp32s3UserDataThermometerEvidence();
  const auto decision = EvaluateRollbackFloorDescriptorViability(reviewed);
  Expect(decision == RollbackFloorDescriptorViabilityDecision::
                         kReviewedCustomThermometerIncompatible,
         "exact pinned evidence must reject the custom thermometer");
  Expect(std::string(SanitizeRollbackFloorDescriptorViabilityDecision(
             decision)) == "REVIEWED-NO-VIABLE-CUSTOM-THERMOMETER",
         "reviewed result must use the fixed sanitized category");
  Expect(std::string(SanitizeRollbackFloorDescriptorViabilityDecision(
             RollbackFloorDescriptorViabilityDecision::kDeny)) ==
             "DENY-DESCRIPTOR-VIABILITY",
         "denial must use the fixed sanitized category");
}

void TestPinnedSourceMutationsDeny() {
  const auto reviewed = MakeReviewedEsp32s3UserDataThermometerEvidence();
  for (std::size_t index = 0; index < reviewed.source_sha256.size(); ++index) {
    auto changed = reviewed;
    changed.source_sha256[index][0] =
        changed.source_sha256[index][0] == '0' ? '1' : '0';
    ExpectDenied(changed, "every source digest mutation must deny");
  }
  auto changed = reviewed;
  changed.esp_idf_version[0] = '5';
  ExpectDenied(changed, "toolchain version drift must deny");
  changed = reviewed;
  changed.target_is_esp32s3 = false;
  ExpectDenied(changed, "target-family drift must deny");
}

void TestDescriptorFactMutationsDeny() {
  const auto reviewed = MakeReviewedEsp32s3UserDataThermometerEvidence();
  auto changed = reviewed;
  changed.user_data_field_known = false;
  ExpectDenied(changed, "unknown USER_DATA field must deny");
  changed = reviewed;
  changed.user_data_first_bit = 1;
  ExpectDenied(changed, "USER_DATA start drift must deny");
  changed = reviewed;
  changed.user_data_bit_count = 255;
  ExpectDenied(changed, "USER_DATA width drift must deny");
  changed = reviewed;
  changed.mac_custom_overlap_known = false;
  ExpectDenied(changed, "unknown MAC_CUSTOM overlap must deny");
  changed = reviewed;
  changed.mac_custom_first_bit = 199;
  ExpectDenied(changed, "MAC_CUSTOM start drift must deny");
  changed = reviewed;
  changed.mac_custom_bit_count = 47;
  ExpectDenied(changed, "MAC_CUSTOM width drift must deny");
  changed = reviewed;
  changed.coding_scheme_known = false;
  ExpectDenied(changed, "unknown coding scheme must deny");
  changed = reviewed;
  changed.coding_scheme = RollbackFloorCodingScheme::kNone;
  ExpectDenied(changed, "non-RS coding claim must deny");
  changed = reviewed;
  changed.coding_scheme = static_cast<RollbackFloorCodingScheme>(0xFF);
  ExpectDenied(changed, "unknown coding enum must deny");
  changed = reviewed;
  changed.coding_unit_single_write_known = false;
  ExpectDenied(changed, "unknown write-once fact must deny");
  changed = reviewed;
  changed.coding_unit_single_write = false;
  ExpectDenied(changed, "repeat-writable claim must deny");
  changed = reviewed;
  changed.repeated_independent_advances_required = false;
  ExpectDenied(changed, "missing repeated-advance requirement must deny");
}

void TestEveryAuthorityAndDisclosureClaimDenies() {
  const auto reviewed = MakeReviewedEsp32s3UserDataThermometerEvidence();
#define EXPECT_TRUE_DENIES(field)      \
  do {                                 \
    auto changed = reviewed;           \
    changed.field = true;              \
    ExpectDenied(changed, #field);     \
  } while (false)
  EXPECT_TRUE_DENIES(descriptor_selected);
  EXPECT_TRUE_DENIES(provider_admitted);
  EXPECT_TRUE_DENIES(device_access_authorized);
  EXPECT_TRUE_DENIES(efuse_read_authorized);
  EXPECT_TRUE_DENIES(efuse_write_authorized);
  EXPECT_TRUE_DENIES(provisioning_authorized);
  EXPECT_TRUE_DENIES(runtime_activation_authorized);
  EXPECT_TRUE_DENIES(raw_device_data_present);
  EXPECT_TRUE_DENIES(private_identity_or_path_present);
#undef EXPECT_TRUE_DENIES
}

void TestDeterminism() {
  const auto reviewed = MakeReviewedEsp32s3UserDataThermometerEvidence();
  for (int iteration = 0; iteration < 100; ++iteration) {
    Expect(EvaluateRollbackFloorDescriptorViability(reviewed) ==
               RollbackFloorDescriptorViabilityDecision::
                   kReviewedCustomThermometerIncompatible,
           "review result must be deterministic");
  }
}

}  // namespace

int main() {
  TestExactReviewedIncompatibility();
  TestPinnedSourceMutationsDeny();
  TestDescriptorFactMutationsDeny();
  TestEveryAuthorityAndDisclosureClaimDenies();
  TestDeterminism();
  if (failures != 0) {
    std::cerr << failures << " viability assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "rollback-floor descriptor viability tests passed\n";
  return EXIT_SUCCESS;
}
