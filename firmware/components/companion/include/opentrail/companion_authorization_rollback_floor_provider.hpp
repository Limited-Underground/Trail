#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace opentrail::companion {

enum class RollbackFloorProviderClass {
  kEsp32s3CustomEfuseThermometer,
};

// This is evidence about a possible provider, not permission to use one.
// The default value deliberately describes no admitted physical provider.
struct RollbackFloorProviderEvidence {
  RollbackFloorProviderClass provider_class{
      RollbackFloorProviderClass::kEsp32s3CustomEfuseThermometer};
  std::optional<std::uint32_t> efuse_block;
  std::optional<std::uint32_t> first_bit;
  std::optional<std::size_t> capacity_bits;
  bool exact_inventory_observed{false};
  bool protection_state_observed{false};
  bool protection_state_matches{false};
  bool provisioned_known{false};
  bool provisioned{false};
  bool post_provision_read_verified{false};
  bool operational_self_test_attempted{false};
  bool operational_self_test_passed{false};
  bool factory_target_admitted{false};
  bool provisioning_authorized{false};
  bool write_authorized{false};
  bool active{false};
};

bool IsPhysicallyAdmitted(const RollbackFloorProviderEvidence& evidence);

// Bits are ordered from the lowest-addressed counter bit to the highest.
// A canonical thermometer value contains only a contiguous low-order prefix
// of one bits followed by zero bits. For example, 111000 represents floor 3.
struct RollbackFloorRawObservation {
  bool read_succeeded{false};
  std::optional<std::size_t> width_bits;
  bool protection_state_observed{false};
  bool protection_state_matches{false};
  std::vector<bool> bits_low_to_high;
};

enum class RollbackFloorObservationStatus {
  kAccepted,
  kReadFailed,
  kWidthUnknown,
  kWidthMismatch,
  kProtectionUnknown,
  kProtectionMismatch,
  kNonCanonical,
};

struct RollbackFloorObservation {
  RollbackFloorObservationStatus status{
      RollbackFloorObservationStatus::kReadFailed};
  std::optional<std::uint32_t> floor;
  bool exhausted{false};

  bool accepted() const {
    return status == RollbackFloorObservationStatus::kAccepted &&
           floor.has_value();
  }
};

RollbackFloorObservation EvaluateRollbackFloorObservation(
    const RollbackFloorRawObservation& raw);

enum class RollbackFloorProgrammingOutcome {
  // Programming was not attempted. This is not a successful transition.
  kNotAttempted,
  // The failure is proven to have happened before any irreversible write.
  kDefinitePreBurnFailure,
  // The programming API reported success. An exact reread is still required.
  kReportedSuccess,
  // Programming may have started or completed, but returned an error.
  kMayHaveAppliedWithError,
};

struct RollbackFloorAdvanceRequest {
  std::uint32_t expected_floor{0};
  std::uint32_t requested_floor{0};
  bool operation_already_in_progress{false};
  RollbackFloorRawObservation before;
  RollbackFloorProgrammingOutcome programming_outcome{
      RollbackFloorProgrammingOutcome::kNotAttempted};
  std::optional<RollbackFloorRawObservation> after;
};

enum class RollbackFloorAdvanceStatus {
  kSucceeded,
  kDefiniteNoChange,
  kUncertain,
  kDeniedObservation,
  kDeniedReentry,
  kDeniedStaleExpected,
  kDeniedInvalidStep,
  kExhausted,
};

struct RollbackFloorAdvanceResult {
  RollbackFloorAdvanceStatus status{
      RollbackFloorAdvanceStatus::kDeniedObservation};
  std::optional<std::uint32_t> observed_floor;
  // The evaluator never authorizes automatic retry. A caller must obtain a
  // fresh observation and authorization for any later operation.
  bool retry_allowed{false};
  bool authorization_must_remain_closed{true};

  bool succeeded() const {
    return status == RollbackFloorAdvanceStatus::kSucceeded;
  }
};

RollbackFloorAdvanceResult EvaluateRollbackFloorAdvance(
    const RollbackFloorAdvanceRequest& request);

enum class RollbackFloorReconciliationStatus {
  kMatched,
  // A record prepared ahead of the floor is not committed and must not be
  // published as the active authorization state.
  kPreparedRecordAhead,
  // The floor is ahead of the available record. Authorization remains closed
  // until an authenticated forward recovery reconstructs that generation.
  kFloorAheadForwardRecoveryRequired,
  kDeniedObservation,
};

RollbackFloorReconciliationStatus EvaluateRollbackFloorReconciliation(
    const RollbackFloorRawObservation& floor_observation,
    std::uint32_t record_generation);

}  // namespace opentrail::companion
