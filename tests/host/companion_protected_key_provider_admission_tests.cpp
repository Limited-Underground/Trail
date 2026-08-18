#include "opentrail/companion_protected_key_provider_admission.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(const std::string& name,
            const CompanionProtectedKeyProviderPairEvidence& evidence,
            CompanionProtectedKeyProviderAdmissionDecision expected) {
  const auto actual =
      evaluate_companion_protected_key_provider_pair(evidence);
  if (actual != expected) {
    ++failures;
    std::cerr << "FAIL: " << name << " expected "
              << static_cast<int>(expected) << " got "
              << static_cast<int>(actual) << '\n';
  }
}

CompanionProtectedKeyProviderObservation observation(
    CompanionProtectedKeyRole role, std::uint8_t private_index,
    const CompanionProtectedKeyEvidenceId& operation_id,
    const CompanionProtectedKeyEvidenceId& evidence_id) {
  CompanionProtectedKeyProviderObservation result{};
  result.role = role;
  result.provider =
      CompanionProtectedKeyProviderKind::kEsp32s3HmacUpEfuse;
  result.block = {true, private_index};
  result.purpose_known = true;
  result.purpose = CompanionProtectedKeyPurpose::kHmacUp;
  result.provisioned_known = true;
  result.provisioned = true;
  result.read_protection_known = true;
  result.read_protected = true;
  result.operational_self_test_attempted = true;
  result.operational_self_test_passed = true;
  result.fresh = true;
  result.operation_id = operation_id;
  result.evidence_id = evidence_id;
  return result;
}

CompanionProtectedKeyProviderPairEvidence accepted_pair() {
  CompanionProtectedKeyProviderPairEvidence result{};
  for (std::size_t index = 0; index < result.operation_id.size(); ++index) {
    result.operation_id[index] = static_cast<std::uint8_t>(0x10U + index);
    result.evidence_id[index] = static_cast<std::uint8_t>(0x80U + index);
  }
  result.exclusive_evaluation_owner = true;
  result.nvs_encryption = observation(
      CompanionProtectedKeyRole::kOtAuthNvsEncryptionOnly, 1U,
      result.operation_id, result.evidence_id);
  result.bond_binding_prf = observation(
      CompanionProtectedKeyRole::kPrivateBondBindingPrfOnly, 4U,
      result.operation_id, result.evidence_id);
  result.configured_nvs_key.present = true;
  result.configured_nvs_key.block = result.nvs_encryption.block;
  result.configured_nvs_key.fresh = true;
  result.configured_nvs_key.operation_id = result.operation_id;
  result.configured_nvs_key.evidence_id = result.evidence_id;
  return result;
}

template <typename Mutator>
void denied(const std::string& name,
            CompanionProtectedKeyProviderAdmissionDecision expected,
            Mutator mutate) {
  auto evidence = accepted_pair();
  mutate(evidence);
  expect(name, evidence, expected);
}

void defaults_and_reentry_tests() {
  const CompanionProtectedKeyProviderPairEvidence defaults{};
  expect("default evidence denies", defaults,
         CompanionProtectedKeyProviderAdmissionDecision::kDenyReentry);
  denied("exclusive owner missing",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyReentry,
         [](auto& evidence) { evidence.exclusive_evaluation_owner = false; });
  denied("reentry observed",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyReentry,
         [](auto& evidence) { evidence.reentry_observed = true; });
}

void identity_tests() {
  denied("NVS role malformed",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyRole,
         [](auto& evidence) {
           evidence.nvs_encryption.role =
               CompanionProtectedKeyRole::kPrivateBondBindingPrfOnly;
         });
  denied("PRF role malformed",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyRole,
         [](auto& evidence) {
           evidence.bond_binding_prf.role =
               CompanionProtectedKeyRole::kOtAuthNvsEncryptionOnly;
         });
  denied("NVS provider malformed",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyProvider,
         [](auto& evidence) {
           evidence.nvs_encryption.provider =
               CompanionProtectedKeyProviderKind::kUnknown;
         });
  denied("PRF provider malformed",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyProvider,
         [](auto& evidence) {
           evidence.bond_binding_prf.provider =
               static_cast<CompanionProtectedKeyProviderKind>(0xFFU);
         });
}

void block_reference_tests() {
  denied("NVS block absent",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) { evidence.nvs_encryption.block.present = false; });
  denied("PRF block absent",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) {
           evidence.bond_binding_prf.block.present = false;
         });
  denied("configured NVS observation absent",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) { evidence.configured_nvs_key.present = false; });
  denied("configured NVS block absent",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) {
           evidence.configured_nvs_key.block.present = false;
         });
  denied("NVS block out of range",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) {
           evidence.nvs_encryption.block.private_index = 6U;
         });
  denied("PRF block out of range",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) {
           evidence.bond_binding_prf.block.private_index = 0xFFU;
         });
  denied("configured NVS block out of range",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyBlockReference,
         [](auto& evidence) {
           evidence.configured_nvs_key.block.private_index = 6U;
         });
  denied("same block reused",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyDistinctness,
         [](auto& evidence) {
           evidence.bond_binding_prf.block = evidence.nvs_encryption.block;
         });
}

void purpose_protection_and_self_test_tests() {
  denied("NVS purpose unknown",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyPurpose,
         [](auto& evidence) {
           evidence.nvs_encryption.purpose_known = false;
         });
  denied("PRF purpose mismatch",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyPurpose,
         [](auto& evidence) {
           evidence.bond_binding_prf.purpose =
               CompanionProtectedKeyPurpose::kUnknown;
         });
  denied("NVS provisioning unknown",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyProvisioning,
         [](auto& evidence) {
           evidence.nvs_encryption.provisioned_known = false;
         });
  denied("PRF not provisioned",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyProvisioning,
         [](auto& evidence) {
           evidence.bond_binding_prf.provisioned = false;
         });
  denied("NVS read protection unknown",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyReadProtection,
         [](auto& evidence) {
           evidence.nvs_encryption.read_protection_known = false;
         });
  denied("PRF not read protected",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyReadProtection,
         [](auto& evidence) {
           evidence.bond_binding_prf.read_protected = false;
         });
  denied("NVS self-test not attempted",
         CompanionProtectedKeyProviderAdmissionDecision::
             kDenyOperationalSelfTest,
         [](auto& evidence) {
           evidence.nvs_encryption.operational_self_test_attempted = false;
         });
  denied("PRF self-test failed",
         CompanionProtectedKeyProviderAdmissionDecision::
             kDenyOperationalSelfTest,
         [](auto& evidence) {
           evidence.bond_binding_prf.operational_self_test_passed = false;
         });
}

void freshness_and_binding_tests() {
  denied("NVS observation stale",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyFreshness,
         [](auto& evidence) { evidence.nvs_encryption.fresh = false; });
  denied("PRF observation stale",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyFreshness,
         [](auto& evidence) { evidence.bond_binding_prf.fresh = false; });
  denied("configured NVS observation stale",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyFreshness,
         [](auto& evidence) { evidence.configured_nvs_key.fresh = false; });
  denied("zero operation id",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyEvidenceBinding,
         [](auto& evidence) {
           evidence.operation_id = {};
           evidence.nvs_encryption.operation_id = {};
           evidence.bond_binding_prf.operation_id = {};
           evidence.configured_nvs_key.operation_id = {};
         });
  denied("zero evidence id",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyEvidenceBinding,
         [](auto& evidence) {
           evidence.evidence_id = {};
           evidence.nvs_encryption.evidence_id = {};
           evidence.bond_binding_prf.evidence_id = {};
           evidence.configured_nvs_key.evidence_id = {};
         });
  denied("mixed NVS operation",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyEvidenceBinding,
         [](auto& evidence) { evidence.nvs_encryption.operation_id[0] ^= 1U; });
  denied("mixed PRF evidence",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyEvidenceBinding,
         [](auto& evidence) { evidence.bond_binding_prf.evidence_id[0] ^= 1U; });
  denied("mixed configured operation",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyEvidenceBinding,
         [](auto& evidence) {
           evidence.configured_nvs_key.operation_id[0] ^= 1U;
         });
  denied("mixed configured evidence",
         CompanionProtectedKeyProviderAdmissionDecision::kDenyEvidenceBinding,
         [](auto& evidence) {
           evidence.configured_nvs_key.evidence_id[0] ^= 1U;
         });
}

void nvs_configuration_binding_tests() {
  denied("configured NVS id does not match NVS role",
         CompanionProtectedKeyProviderAdmissionDecision::
             kDenyNvsConfigurationBinding,
         [](auto& evidence) {
           evidence.configured_nvs_key.block.private_index = 2U;
         });
  denied("configured NVS id points to PRF role",
         CompanionProtectedKeyProviderAdmissionDecision::
             kDenyNvsConfigurationBinding,
         [](auto& evidence) {
           evidence.configured_nvs_key.block =
               evidence.bond_binding_prf.block;
         });
}

void acceptance_test() {
  expect("exact pair accepts", accepted_pair(),
         CompanionProtectedKeyProviderAdmissionDecision::kAcceptOfflinePair);
}

}  // namespace

int main() {
  defaults_and_reentry_tests();
  identity_tests();
  block_reference_tests();
  purpose_protection_and_self_test_tests();
  freshness_and_binding_tests();
  nvs_configuration_binding_tests();
  acceptance_test();

  if (failures != 0) {
    std::cerr << failures << " protected-key provider admission test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: protected-key provider pair admission\n";
  return 0;
}
