#include "companion_boot_self_check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_request_coordinator.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;
using opentrail::protocol::QuickStatusKind;

// These counters observe calls made by the boot self-check. They are not
// device-authority state, queue state, or a resource reservation.
struct SelfCheckObservation {
    std::uint32_t snapshot_calls{0};
    std::uint32_t prepare_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t applied_calls{0};
};

template <std::size_t Size>
bool bytes_equal(const std::array<std::uint8_t, Size>& expected,
                 const std::array<std::uint8_t,
                                  kCompanionMaxResponseRecordBytes>& observed,
                 std::size_t observed_bytes) {
    if (observed_bytes != Size) {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index) {
        if (observed[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

class FixedSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    explicit FixedSnapshotAuthority(SelfCheckObservation& observation)
        : observation_(observation) {}

    CompanionSnapshotAuthorityResult read_snapshot() override {
        ++observation_.snapshot_calls;
        return {
            CompanionAuthorityError::none,
            {
                7,
                CompanionRadioState::ready,
                CompanionGnssState::current,
                CompanionPowerState::normal,
                CompanionPositionSharingState::active,
                2,
                0x1122334455667788ULL,
            },
        };
    }

private:
    SelfCheckObservation& observation_;
};

class FixedActionAuthority final : public CompanionActionAuthority {
public:
    explicit FixedActionAuthority(SelfCheckObservation& observation)
        : observation_(observation) {}

    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest& request) override {
        if (request.kind != CompanionActionKind::quick_status ||
            request.quick_status != QuickStatusKind::available_to_help ||
            request.critical_alert_id != 0) {
            return {CompanionAuthorityError::failed};
        }
        ++observation_.prepare_calls;
        return {
            CompanionAuthorityError::none,
            CompanionActionDisposition::queued,
            CompanionActionRejectReason::none,
            kOperationToken,
        };
    }

    CompanionAuthorityError commit_action(
        const CompanionActionRequest& request,
        const CompanionActionAuthorityResult& prepared) override {
        if (request.kind != CompanionActionKind::quick_status ||
            request.quick_status != QuickStatusKind::available_to_help ||
            request.critical_alert_id != 0 ||
            prepared.error != CompanionAuthorityError::none ||
            prepared.disposition != CompanionActionDisposition::queued ||
            prepared.reject_reason != CompanionActionRejectReason::none ||
            prepared.operation_token != kOperationToken) {
            return CompanionAuthorityError::failed;
        }
        ++observation_.commit_calls;
        ++observation_.applied_calls;
        return CompanionAuthorityError::none;
    }

private:
    static constexpr std::uint32_t kOperationToken = 0xC0DEC0DEU;
    SelfCheckObservation& observation_;
};

constexpr CompanionSessionEvidence kEvidence{0xA5, true, true, true};

constexpr std::array<std::uint8_t, 40> kActionRequest{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x14, 0x00,
    0x4F, 0x54, 0x41, 0x30, 0x00, 0x00, 0x01, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 40> kActionResponse{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x82, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x14, 0x00,
    0x4F, 0x54, 0x52, 0x30, 0x00, 0x00, 0x02, 0x00,
    0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 28> kSnapshotRequest{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x00,
    0x4F, 0x54, 0x58, 0x30, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 52> kSnapshotResponse{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x81, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x20, 0x00,
    0x4F, 0x54, 0x4E, 0x30, 0x00, 0x00, 0x02, 0x03,
    0x02, 0x02, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static_assert(kActionRequest.size() == kCompanionMaxRequestRecordBytes);
static_assert(kActionResponse.size() ==
              kCompanionFragmentHeaderBytes + kCompanionActionResultBytes);
static_assert(kSnapshotRequest.size() ==
              kCompanionFragmentHeaderBytes + kCompanionSnapshotRequestBytes);
static_assert(kSnapshotResponse.size() == kCompanionMaxResponseRecordBytes);

}  // namespace

bool run_companion_request_coordinator_self_check() {
    SelfCheckObservation observation{};
    FixedSnapshotAuthority snapshots(observation);
    FixedActionAuthority actions(observation);

    {
        CompanionRequestCoordinator coordinator(snapshots, actions);
        if (!coordinator.open_session(kEvidence, 1).opened()) {
            return false;
        }
        std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
        const auto first = coordinator.service(
            kEvidence,
            {kActionRequest.data(), kActionRequest.size()},
            {response.data(), response.size()});
        if (first.disposition != CompanionCoordinatorDisposition::processed_new ||
            first.error != CompanionCoordinatorError::none ||
            !bytes_equal(kActionResponse, response, first.response_bytes) ||
            observation.prepare_calls != 1 || observation.commit_calls != 1 ||
            observation.applied_calls != 1) {
            return false;
        }

        response.fill(0xA5);
        const auto duplicate = coordinator.service(
            kEvidence,
            {kActionRequest.data(), kActionRequest.size()},
            {response.data(), response.size()});
        if (duplicate.disposition !=
                CompanionCoordinatorDisposition::replayed_cached_response ||
            duplicate.error != CompanionCoordinatorError::none ||
            !bytes_equal(kActionResponse, response, duplicate.response_bytes) ||
            observation.prepare_calls != 1 || observation.commit_calls != 1 ||
            observation.applied_calls != 1) {
            return false;
        }
    }

    {
        CompanionRequestCoordinator coordinator(snapshots, actions);
        if (!coordinator.open_session(kEvidence, 3).opened()) {
            return false;
        }
        std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
        const auto result = coordinator.service(
            kEvidence,
            {kSnapshotRequest.data(), kSnapshotRequest.size()},
            {response.data(), response.size()});
        if (result.disposition !=
                CompanionCoordinatorDisposition::processed_new ||
            result.error != CompanionCoordinatorError::none ||
            !bytes_equal(kSnapshotResponse, response, result.response_bytes) ||
            observation.snapshot_calls != 1 || observation.prepare_calls != 1 ||
            observation.commit_calls != 1 || observation.applied_calls != 1) {
            return false;
        }
    }

    return true;
}

}  // namespace opentrail::target::heltec_v4_bench
