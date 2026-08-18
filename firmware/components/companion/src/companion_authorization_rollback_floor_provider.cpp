#include "opentrail/companion_authorization_rollback_floor_provider.hpp"

#include <limits>

namespace opentrail::companion {

bool IsPhysicallyAdmitted(const RollbackFloorProviderEvidence& evidence) {
  // No physical floor provider class is currently admitted. OT-083 proved
  // that the ESP32-S3 custom USER_DATA candidate cannot support repeated
  // one-bit advances because its Reed-Solomon coding unit is writable once.
  // The semantic observation/reconciliation helpers below remain useful for a
  // future provider, but factual fields and authority cannot revive a rejected
  // physical provider class.
  static_cast<void>(evidence);
  return false;
}

RollbackFloorObservation EvaluateRollbackFloorObservation(
    const RollbackFloorRawObservation& raw) {
  RollbackFloorObservation result;
  if (!raw.read_succeeded) {
    result.status = RollbackFloorObservationStatus::kReadFailed;
    return result;
  }
  if (!raw.width_bits.has_value() || raw.width_bits.value() == 0) {
    result.status = RollbackFloorObservationStatus::kWidthUnknown;
    return result;
  }
  if (raw.width_bits.value() != raw.bits_low_to_high.size() ||
      raw.width_bits.value() >
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    result.status = RollbackFloorObservationStatus::kWidthMismatch;
    return result;
  }
  if (!raw.protection_state_observed) {
    result.status = RollbackFloorObservationStatus::kProtectionUnknown;
    return result;
  }
  if (!raw.protection_state_matches) {
    result.status = RollbackFloorObservationStatus::kProtectionMismatch;
    return result;
  }

  std::uint32_t floor = 0;
  bool saw_zero = false;
  for (const bool bit : raw.bits_low_to_high) {
    if (bit) {
      if (saw_zero) {
        result.status = RollbackFloorObservationStatus::kNonCanonical;
        return result;
      }
      ++floor;
    } else {
      saw_zero = true;
    }
  }

  result.status = RollbackFloorObservationStatus::kAccepted;
  result.floor = floor;
  result.exhausted = floor == raw.width_bits.value();
  return result;
}

RollbackFloorAdvanceResult EvaluateRollbackFloorAdvance(
    const RollbackFloorAdvanceRequest& request) {
  RollbackFloorAdvanceResult result;
  if (request.operation_already_in_progress) {
    result.status = RollbackFloorAdvanceStatus::kDeniedReentry;
    return result;
  }

  const auto before = EvaluateRollbackFloorObservation(request.before);
  if (!before.accepted()) {
    result.status = RollbackFloorAdvanceStatus::kDeniedObservation;
    return result;
  }
  result.observed_floor = before.floor;

  if (before.floor.value() != request.expected_floor) {
    result.status = RollbackFloorAdvanceStatus::kDeniedStaleExpected;
    return result;
  }
  if (request.expected_floor == std::numeric_limits<std::uint32_t>::max() ||
      request.requested_floor != request.expected_floor + 1) {
    result.status = RollbackFloorAdvanceStatus::kDeniedInvalidStep;
    return result;
  }
  if (before.exhausted) {
    result.status = RollbackFloorAdvanceStatus::kExhausted;
    return result;
  }

  switch (request.programming_outcome) {
    case RollbackFloorProgrammingOutcome::kDefinitePreBurnFailure:
      result.status = RollbackFloorAdvanceStatus::kDefiniteNoChange;
      return result;
    case RollbackFloorProgrammingOutcome::kMayHaveAppliedWithError:
      result.status = RollbackFloorAdvanceStatus::kUncertain;
      return result;
    case RollbackFloorProgrammingOutcome::kNotAttempted:
      result.status = RollbackFloorAdvanceStatus::kDeniedObservation;
      return result;
    case RollbackFloorProgrammingOutcome::kReportedSuccess:
      break;
  }

  if (!request.after.has_value()) {
    result.status = RollbackFloorAdvanceStatus::kUncertain;
    return result;
  }
  const auto after = EvaluateRollbackFloorObservation(request.after.value());
  if (!after.accepted() || after.floor.value() != request.requested_floor ||
      request.after->width_bits != request.before.width_bits) {
    result.status = RollbackFloorAdvanceStatus::kUncertain;
    if (after.floor.has_value()) {
      result.observed_floor = after.floor;
    }
    return result;
  }

  result.status = RollbackFloorAdvanceStatus::kSucceeded;
  result.observed_floor = after.floor;
  return result;
}

RollbackFloorReconciliationStatus EvaluateRollbackFloorReconciliation(
    const RollbackFloorRawObservation& floor_observation,
    const std::uint32_t record_generation) {
  const auto observed = EvaluateRollbackFloorObservation(floor_observation);
  if (!observed.accepted()) {
    return RollbackFloorReconciliationStatus::kDeniedObservation;
  }
  if (observed.floor.value() == record_generation) {
    return RollbackFloorReconciliationStatus::kMatched;
  }
  if (record_generation > observed.floor.value()) {
    return RollbackFloorReconciliationStatus::kPreparedRecordAhead;
  }
  return RollbackFloorReconciliationStatus::kFloorAheadForwardRecoveryRequired;
}

}  // namespace opentrail::companion
