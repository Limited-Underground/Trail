#include "opentrail/companion_protected_key_provider_admission.hpp"

#include <algorithm>

namespace opentrail::companion {
namespace {

bool nonzero(const CompanionProtectedKeyEvidenceId& value) {
  return std::any_of(value.begin(), value.end(),
                     [](std::uint8_t byte) { return byte != 0U; });
}

bool valid_block(const CompanionProtectedKeyBlockReference& block) {
  return block.present &&
         block.private_index >= kCompanionProtectedKeyBlockMinimum &&
         block.private_index <= kCompanionProtectedKeyBlockMaximum;
}

bool same_block(const CompanionProtectedKeyBlockReference& left,
                const CompanionProtectedKeyBlockReference& right) {
  return valid_block(left) && valid_block(right) &&
         left.private_index == right.private_index;
}

bool same_binding(const CompanionProtectedKeyProviderObservation& observation,
                  const CompanionProtectedKeyProviderPairEvidence& evidence) {
  return observation.operation_id == evidence.operation_id &&
         observation.evidence_id == evidence.evidence_id;
}

}  // namespace

CompanionProtectedKeyProviderAdmissionDecision
evaluate_companion_protected_key_provider_pair(
    const CompanionProtectedKeyProviderPairEvidence& evidence) {
  using Decision = CompanionProtectedKeyProviderAdmissionDecision;
  using Provider = CompanionProtectedKeyProviderKind;
  using Purpose = CompanionProtectedKeyPurpose;
  using Role = CompanionProtectedKeyRole;

  if (!evidence.exclusive_evaluation_owner || evidence.reentry_observed) {
    return Decision::kDenyReentry;
  }

  const auto& nvs = evidence.nvs_encryption;
  const auto& prf = evidence.bond_binding_prf;
  if (nvs.role != Role::kOtAuthNvsEncryptionOnly ||
      prf.role != Role::kPrivateBondBindingPrfOnly) {
    return Decision::kDenyRole;
  }

  if (nvs.provider != Provider::kEsp32s3HmacUpEfuse ||
      prf.provider != Provider::kEsp32s3HmacUpEfuse) {
    return Decision::kDenyProvider;
  }

  if (!valid_block(nvs.block) || !valid_block(prf.block) ||
      !evidence.configured_nvs_key.present ||
      !valid_block(evidence.configured_nvs_key.block)) {
    return Decision::kDenyBlockReference;
  }
  if (same_block(nvs.block, prf.block)) {
    return Decision::kDenyDistinctness;
  }

  if (!nvs.purpose_known || !prf.purpose_known ||
      nvs.purpose != Purpose::kHmacUp || prf.purpose != Purpose::kHmacUp) {
    return Decision::kDenyPurpose;
  }

  if (!nvs.provisioned_known || !prf.provisioned_known ||
      !nvs.provisioned || !prf.provisioned) {
    return Decision::kDenyProvisioning;
  }

  if (!nvs.read_protection_known || !prf.read_protection_known ||
      !nvs.read_protected || !prf.read_protected) {
    return Decision::kDenyReadProtection;
  }

  if (!nvs.operational_self_test_attempted ||
      !prf.operational_self_test_attempted ||
      !nvs.operational_self_test_passed ||
      !prf.operational_self_test_passed) {
    return Decision::kDenyOperationalSelfTest;
  }

  if (!nvs.fresh || !prf.fresh || !evidence.configured_nvs_key.fresh) {
    return Decision::kDenyFreshness;
  }

  if (!nonzero(evidence.operation_id) || !nonzero(evidence.evidence_id) ||
      !same_binding(nvs, evidence) || !same_binding(prf, evidence) ||
      evidence.configured_nvs_key.operation_id != evidence.operation_id ||
      evidence.configured_nvs_key.evidence_id != evidence.evidence_id) {
    return Decision::kDenyEvidenceBinding;
  }

  if (!same_block(evidence.configured_nvs_key.block, nvs.block) ||
      same_block(evidence.configured_nvs_key.block, prf.block)) {
    return Decision::kDenyNvsConfigurationBinding;
  }

  return Decision::kAcceptOfflinePair;
}

}  // namespace opentrail::companion
