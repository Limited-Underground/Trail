#include "opentrail/companion_rollback_floor_secure_version_viability.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using opentrail::companion::EvaluateSecureVersionFloorViability;
using opentrail::companion::MakeReviewedEsp32s3SecureVersionEvidence;
using opentrail::companion::SanitizeSecureVersionFloorViabilityDecision;
using opentrail::companion::SecureVersionCodingScheme;
using opentrail::companion::SecureVersionFloorViabilityDecision;
using opentrail::companion::SecureVersionFloorViabilityEvidence;

int failures = 0;

void Expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void ExpectDenied(const SecureVersionFloorViabilityEvidence& evidence,
                  const std::string& message) {
  Expect(EvaluateSecureVersionFloorViability(evidence) ==
             SecureVersionFloorViabilityDecision::kDeny,
         message);
}

void TestExactReviewedCoupling() {
  ExpectDenied({}, "default evidence must deny");
  const auto reviewed = MakeReviewedEsp32s3SecureVersionEvidence();
  const auto decision = EvaluateSecureVersionFloorViability(reviewed);
  Expect(decision ==
             SecureVersionFloorViabilityDecision::kReviewedCoupledNotAdmitted,
         "exact reviewed evidence must reject SECURE_VERSION admission");
  Expect(std::string(SanitizeSecureVersionFloorViabilityDecision(decision)) ==
             "REVIEWED-SECURE-VERSION-COUPLED-NOT-ADMITTED",
         "review must use the fixed sanitized category");
  Expect(std::string(SanitizeSecureVersionFloorViabilityDecision(
             SecureVersionFloorViabilityDecision::kDeny)) ==
             "DENY-SECURE-VERSION-VIABILITY",
         "denial must use the fixed sanitized category");
}

void TestSourceAndIdentityMutationsDeny() {
  const auto reviewed = MakeReviewedEsp32s3SecureVersionEvidence();
  for (std::size_t index = 0; index < reviewed.source_sha256.size(); ++index) {
    auto changed = reviewed;
    changed.source_sha256[index][0] =
        changed.source_sha256[index][0] == '0' ? '1' : '0';
    ExpectDenied(changed, "every pinned source digest mutation must deny");
  }
  auto changed = reviewed;
  changed.esp_idf_version[0] = '5';
  ExpectDenied(changed, "toolchain version drift must deny");
  changed = reviewed;
  changed.target_is_esp32s3 = false;
  ExpectDenied(changed, "target-family drift must deny");
}

void TestFieldFactMutationsDeny() {
  const auto reviewed = MakeReviewedEsp32s3SecureVersionEvidence();
#define EXPECT_FALSE_DENIES(field)     \
  do {                                 \
    auto changed = reviewed;           \
    changed.field = false;             \
    ExpectDenied(changed, #field);     \
  } while (false)
  EXPECT_FALSE_DENIES(field_known);
  EXPECT_FALSE_DENIES(coding_scheme_known);
  EXPECT_FALSE_DENIES(value_is_popcount);
  EXPECT_FALSE_DENIES(update_burns_additional_low_bits);
  EXPECT_FALSE_DENIES(exhaustion_is_permanent);
  EXPECT_FALSE_DENIES(native_role_is_firmware_anti_rollback);
  EXPECT_FALSE_DENIES(app_header_carries_secure_version);
  EXPECT_FALSE_DENIES(bootloader_and_ota_consume_same_field);
  EXPECT_FALSE_DENIES(native_partition_model_requires_ota_without_factory);
  EXPECT_FALSE_DENIES(current_layout_contains_factory);
  EXPECT_FALSE_DENIES(accepted_recovery_route_restores_factory);
  EXPECT_FALSE_DENIES(independent_authorization_domain_required);
#undef EXPECT_FALSE_DENIES
  auto changed = reviewed;
  changed.efuse_block = 1;
  ExpectDenied(changed, "block drift must deny");
  changed = reviewed;
  changed.first_bit = 141;
  ExpectDenied(changed, "field start drift must deny");
  changed = reviewed;
  changed.bit_count = 15;
  ExpectDenied(changed, "field width drift must deny");
  changed = reviewed;
  changed.maximum_advances = 15;
  ExpectDenied(changed, "capacity drift must deny");
  changed = reviewed;
  changed.coding_scheme = SecureVersionCodingScheme::kReedSolomon;
  ExpectDenied(changed, "coding-scheme drift must deny");
  changed = reviewed;
  changed.coding_scheme = static_cast<SecureVersionCodingScheme>(0xFF);
  ExpectDenied(changed, "unknown coding enum must deny");
}

void TestEveryAcceptanceAuthorityAndDisclosureClaimDenies() {
  const auto reviewed = MakeReviewedEsp32s3SecureVersionEvidence();
#define EXPECT_TRUE_DENIES(field)      \
  do {                                 \
    auto changed = reviewed;           \
    changed.field = true;              \
    ExpectDenied(changed, #field);     \
  } while (false)
  EXPECT_TRUE_DENIES(shared_firmware_version_budget_accepted);
  EXPECT_TRUE_DENIES(sixteen_transition_budget_accepted);
  EXPECT_TRUE_DENIES(factory_recovery_redesign_accepted);
  EXPECT_TRUE_DENIES(trusted_firmware_policy_accepted);
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
  const auto reviewed = MakeReviewedEsp32s3SecureVersionEvidence();
  for (int iteration = 0; iteration < 100; ++iteration) {
    Expect(EvaluateSecureVersionFloorViability(reviewed) ==
               SecureVersionFloorViabilityDecision::
                   kReviewedCoupledNotAdmitted,
           "review result must be deterministic");
  }
}

}  // namespace

int main() {
  TestExactReviewedCoupling();
  TestSourceAndIdentityMutationsDeny();
  TestFieldFactMutationsDeny();
  TestEveryAcceptanceAuthorityAndDisclosureClaimDenies();
  TestDeterminism();
  if (failures != 0) {
    std::cerr << failures << " secure-version assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "secure-version rollback-floor viability tests passed\n";
  return EXIT_SUCCESS;
}
