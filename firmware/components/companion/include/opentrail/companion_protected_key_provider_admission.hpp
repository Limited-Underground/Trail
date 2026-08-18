#pragma once

#include <array>
#include <cstdint>

namespace opentrail::companion {

inline constexpr std::uint8_t kCompanionProtectedKeyBlockMinimum = 0U;
inline constexpr std::uint8_t kCompanionProtectedKeyBlockMaximum = 5U;

using CompanionProtectedKeyEvidenceId = std::array<std::uint8_t, 16>;

enum class CompanionProtectedKeyRole : std::uint8_t {
  kUnknown = 0,
  kOtAuthNvsEncryptionOnly,
  kPrivateBondBindingPrfOnly,
};

enum class CompanionProtectedKeyProviderKind : std::uint8_t {
  kUnknown = 0,
  kEsp32s3HmacUpEfuse,
};

enum class CompanionProtectedKeyPurpose : std::uint8_t {
  kUnknown = 0,
  kHmacUp,
};

// This identifies only an opaque target-private key block. It never contains
// key bytes and must not be displayed, logged, serialized into public
// evidence, or used as authority by itself.
struct CompanionProtectedKeyBlockReference {
  bool present{false};
  std::uint8_t private_index{0};
};

// One factual read-only observation. Every fact defaults absent or false; an
// offline provider selection does not manufacture physical key evidence.
struct CompanionProtectedKeyProviderObservation {
  CompanionProtectedKeyRole role{CompanionProtectedKeyRole::kUnknown};
  CompanionProtectedKeyProviderKind provider{
      CompanionProtectedKeyProviderKind::kUnknown};
  CompanionProtectedKeyBlockReference block{};
  bool purpose_known{false};
  CompanionProtectedKeyPurpose purpose{CompanionProtectedKeyPurpose::kUnknown};
  bool provisioned_known{false};
  bool provisioned{false};
  bool read_protection_known{false};
  bool read_protected{false};
  bool operational_self_test_attempted{false};
  bool operational_self_test_passed{false};
  bool fresh{false};
  CompanionProtectedKeyEvidenceId operation_id{};
  CompanionProtectedKeyEvidenceId evidence_id{};
};

// The configured NVS HMAC selection is observed independently from the two
// provider roles. It must bind only to the NVS role and never to the PRF role.
struct CompanionConfiguredNvsKeyObservation {
  bool present{false};
  CompanionProtectedKeyBlockReference block{};
  bool fresh{false};
  CompanionProtectedKeyEvidenceId operation_id{};
  CompanionProtectedKeyEvidenceId evidence_id{};
};

struct CompanionProtectedKeyProviderPairEvidence {
  CompanionProtectedKeyProviderObservation nvs_encryption{};
  CompanionProtectedKeyProviderObservation bond_binding_prf{};
  CompanionConfiguredNvsKeyObservation configured_nvs_key{};
  bool exclusive_evaluation_owner{false};
  bool reentry_observed{false};
  CompanionProtectedKeyEvidenceId operation_id{};
  CompanionProtectedKeyEvidenceId evidence_id{};
};

enum class CompanionProtectedKeyProviderAdmissionDecision : std::uint8_t {
  kAcceptOfflinePair = 0,
  kDenyReentry,
  kDenyRole,
  kDenyProvider,
  kDenyBlockReference,
  kDenyDistinctness,
  kDenyPurpose,
  kDenyProvisioning,
  kDenyReadProtection,
  kDenyOperationalSelfTest,
  kDenyFreshness,
  kDenyEvidenceBinding,
  kDenyNvsConfigurationBinding,
};

// Pure target-neutral admission. Acceptance means only that two factual
// observations satisfy the offline pair contract. It performs no I/O, exposes
// no block reference or key material, and grants no provisioning or runtime
// authority.
[[nodiscard]] CompanionProtectedKeyProviderAdmissionDecision
evaluate_companion_protected_key_provider_pair(
    const CompanionProtectedKeyProviderPairEvidence& evidence);

}  // namespace opentrail::companion
