#include "opentrail/companion_authorization_rollback_floor_provider.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using opentrail::companion::EvaluateRollbackFloorAdvance;
using opentrail::companion::EvaluateRollbackFloorObservation;
using opentrail::companion::EvaluateRollbackFloorReconciliation;
using opentrail::companion::IsPhysicallyAdmitted;
using opentrail::companion::RollbackFloorAdvanceRequest;
using opentrail::companion::RollbackFloorAdvanceStatus;
using opentrail::companion::RollbackFloorObservationStatus;
using opentrail::companion::RollbackFloorProgrammingOutcome;
using opentrail::companion::RollbackFloorProviderEvidence;
using opentrail::companion::RollbackFloorRawObservation;
using opentrail::companion::RollbackFloorReconciliationStatus;

int failures = 0;

void Expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

RollbackFloorRawObservation Observed(std::vector<bool> bits) {
  RollbackFloorRawObservation result;
  result.read_succeeded = true;
  result.width_bits = bits.size();
  result.protection_state_observed = true;
  result.protection_state_matches = true;
  result.bits_low_to_high = std::move(bits);
  return result;
}

void TestAdmissionDefaultsClosed() {
  const RollbackFloorProviderEvidence evidence;
  Expect(!evidence.efuse_block.has_value(), "default block must be absent");
  Expect(!evidence.first_bit.has_value(), "default first bit must be absent");
  Expect(!evidence.capacity_bits.has_value(), "default capacity must be absent");
  Expect(!IsPhysicallyAdmitted(evidence),
         "default evidence must not admit a physical provider");

  auto malformed = evidence;
  malformed.provider_class = static_cast<
      opentrail::companion::RollbackFloorProviderClass>(0xFF);
  malformed.efuse_block = 1;
  malformed.first_bit = 0;
  malformed.capacity_bits = 8;
  malformed.exact_inventory_observed = true;
  malformed.protection_state_observed = true;
  malformed.protection_state_matches = true;
  malformed.provisioned_known = true;
  malformed.provisioned = true;
  malformed.post_provision_read_verified = true;
  malformed.operational_self_test_attempted = true;
  malformed.operational_self_test_passed = true;
  malformed.factory_target_admitted = true;
  malformed.provisioning_authorized = true;
  malformed.write_authorized = true;
  malformed.active = true;
  Expect(!IsPhysicallyAdmitted(malformed),
         "unknown provider class must never admit");

  malformed.provider_class =
      opentrail::companion::RollbackFloorProviderClass::
          kEsp32s3CustomEfuseThermometer;
  Expect(IsPhysicallyAdmitted(malformed),
         "complete exact factual evidence may admit the selected provider");
  auto missing_fact = malformed;
  missing_fact.provisioned_known = false;
  Expect(!IsPhysicallyAdmitted(missing_fact),
         "unknown provisioning state must deny");
  missing_fact = malformed;
  missing_fact.provisioned = false;
  Expect(!IsPhysicallyAdmitted(missing_fact),
         "unprovisioned field must deny");
  missing_fact = malformed;
  missing_fact.post_provision_read_verified = false;
  Expect(!IsPhysicallyAdmitted(missing_fact),
         "missing post-provision reread must deny");
  missing_fact = malformed;
  missing_fact.operational_self_test_attempted = false;
  Expect(!IsPhysicallyAdmitted(missing_fact),
         "unattempted operational self-test must deny");
  missing_fact = malformed;
  missing_fact.operational_self_test_passed = false;
  Expect(!IsPhysicallyAdmitted(missing_fact),
         "failed operational self-test must deny");
}

void TestCanonicalObservations() {
  const auto empty = EvaluateRollbackFloorObservation(
      Observed({false, false, false, false}));
  Expect(empty.accepted() && empty.floor == 0 && !empty.exhausted,
         "empty thermometer must decode to zero");

  const auto one = EvaluateRollbackFloorObservation(
      Observed({true, false, false, false}));
  Expect(one.accepted() && one.floor == 1 && !one.exhausted,
         "one low bit must decode to one");

  const auto full = EvaluateRollbackFloorObservation(
      Observed({true, true, true, true}));
  Expect(full.accepted() && full.floor == 4 && full.exhausted,
         "full thermometer must be accepted as exhausted");

  const auto hole = EvaluateRollbackFloorObservation(
      Observed({true, false, true, false}));
  Expect(hole.status == RollbackFloorObservationStatus::kNonCanonical,
         "a hole must deny the observation");

  auto unknown_width = Observed({false, false});
  unknown_width.width_bits.reset();
  Expect(EvaluateRollbackFloorObservation(unknown_width).status ==
             RollbackFloorObservationStatus::kWidthUnknown,
         "unknown width must deny the observation");

  auto read_failed = Observed({false, false});
  read_failed.read_succeeded = false;
  Expect(EvaluateRollbackFloorObservation(read_failed).status ==
             RollbackFloorObservationStatus::kReadFailed,
         "read failure must deny the observation");

  auto protection_mismatch = Observed({false, false});
  protection_mismatch.protection_state_matches = false;
  Expect(EvaluateRollbackFloorObservation(protection_mismatch).status ==
             RollbackFloorObservationStatus::kProtectionMismatch,
         "protection mismatch must deny the observation");
}

RollbackFloorAdvanceRequest Advance(std::uint32_t expected,
                                    std::uint32_t requested) {
  RollbackFloorAdvanceRequest request;
  request.expected_floor = expected;
  request.requested_floor = requested;
  request.before = Observed({true, false, false, false});
  request.programming_outcome =
      RollbackFloorProgrammingOutcome::kReportedSuccess;
  request.after = Observed({true, true, false, false});
  return request;
}

void TestAdvanceAdmission() {
  const auto success = EvaluateRollbackFloorAdvance(Advance(1, 2));
  Expect(success.status == RollbackFloorAdvanceStatus::kSucceeded &&
             success.observed_floor == 2 && !success.retry_allowed &&
             success.authorization_must_remain_closed,
         "floor success must still await record reconciliation");

  Expect(EvaluateRollbackFloorAdvance(Advance(0, 1)).status ==
             RollbackFloorAdvanceStatus::kDeniedStaleExpected,
         "stale expected floor must deny");
  Expect(EvaluateRollbackFloorAdvance(Advance(1, 3)).status ==
             RollbackFloorAdvanceStatus::kDeniedInvalidStep,
         "jump must deny");
  Expect(EvaluateRollbackFloorAdvance(Advance(1, 0)).status ==
             RollbackFloorAdvanceStatus::kDeniedInvalidStep,
         "decrement must deny");

  auto exhausted = Advance(4, 5);
  exhausted.before = Observed({true, true, true, true});
  Expect(EvaluateRollbackFloorAdvance(exhausted).status ==
             RollbackFloorAdvanceStatus::kExhausted,
         "exhausted counter must permanently deny an advance");

  auto reentry = Advance(1, 2);
  reentry.operation_already_in_progress = true;
  Expect(EvaluateRollbackFloorAdvance(reentry).status ==
             RollbackFloorAdvanceStatus::kDeniedReentry,
         "reentry must deny");
}

void TestFailureClassification() {
  auto definite = Advance(1, 2);
  definite.programming_outcome =
      RollbackFloorProgrammingOutcome::kDefinitePreBurnFailure;
  definite.after.reset();
  const auto definite_result = EvaluateRollbackFloorAdvance(definite);
  Expect(definite_result.status ==
             RollbackFloorAdvanceStatus::kDefiniteNoChange &&
             definite_result.authorization_must_remain_closed &&
             !definite_result.retry_allowed,
         "proven pre-burn failure must be no-change and require fresh authority");

  auto applied_error = Advance(1, 2);
  applied_error.programming_outcome =
      RollbackFloorProgrammingOutcome::kMayHaveAppliedWithError;
  const auto applied_error_result =
      EvaluateRollbackFloorAdvance(applied_error);
  Expect(applied_error_result.status ==
             RollbackFloorAdvanceStatus::kUncertain &&
             applied_error_result.authorization_must_remain_closed &&
             !applied_error_result.retry_allowed,
         "possible applied-with-error outcome must remain uncertain");

  auto shrink = Advance(1, 2);
  shrink.after = Observed({true, true, false});
  Expect(EvaluateRollbackFloorAdvance(shrink).status ==
             RollbackFloorAdvanceStatus::kUncertain,
         "changed-width post-burn reread must remain uncertain");

  auto grow = Advance(1, 2);
  grow.after = Observed({true, true, false, false, false});
  Expect(EvaluateRollbackFloorAdvance(grow).status ==
             RollbackFloorAdvanceStatus::kUncertain,
         "grown-width post-burn reread must remain uncertain");

  auto mismatch = Advance(1, 2);
  mismatch.after = Observed({true, true, true, false});
  Expect(EvaluateRollbackFloorAdvance(mismatch).status ==
             RollbackFloorAdvanceStatus::kUncertain,
         "mismatched post-burn reread must remain uncertain");

  auto unreadable = Advance(1, 2);
  unreadable.after->read_succeeded = false;
  Expect(EvaluateRollbackFloorAdvance(unreadable).status ==
             RollbackFloorAdvanceStatus::kUncertain,
         "failed post-burn reread must remain uncertain");
}

void TestRebootReconciliation() {
  const auto floor_two = Observed({true, true, false, false});
  Expect(EvaluateRollbackFloorReconciliation(floor_two, 2) ==
             RollbackFloorReconciliationStatus::kMatched,
         "equal record and floor must reconcile");
  Expect(EvaluateRollbackFloorReconciliation(floor_two, 3) ==
             RollbackFloorReconciliationStatus::kPreparedRecordAhead,
         "record ahead of floor must remain merely prepared");
  Expect(EvaluateRollbackFloorReconciliation(floor_two, 1) ==
             RollbackFloorReconciliationStatus::
                 kFloorAheadForwardRecoveryRequired,
         "floor ahead of record must require forward recovery");

  auto bad = floor_two;
  bad.bits_low_to_high = {true, false, true, false};
  Expect(EvaluateRollbackFloorReconciliation(bad, 2) ==
             RollbackFloorReconciliationStatus::kDeniedObservation,
         "reboot must not reconcile a noncanonical floor");
}

}  // namespace

int main() {
  TestAdmissionDefaultsClosed();
  TestCanonicalObservations();
  TestAdvanceAdmission();
  TestFailureClassification();
  TestRebootReconciliation();

  if (failures != 0) {
    std::cerr << failures << " rollback-floor provider assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "companion authorization rollback-floor provider tests passed\n";
  return EXIT_SUCCESS;
}
