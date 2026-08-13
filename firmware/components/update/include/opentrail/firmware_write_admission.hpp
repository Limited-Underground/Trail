#pragma once

#include <cstdint>

#include "opentrail/firmware_bundle_admission.hpp"
#include "opentrail/firmware_install_preflight.hpp"

namespace opentrail::update {

enum class FirmwareWriteBindingIssue : std::uint32_t {
    hardware_profile_mismatch = 1U << 0U,
    processor_mismatch = 1U << 1U,
    target_role_mismatch = 1U << 2U,
    board_revision_range_mismatch = 1U << 3U,
    bootloader_schema_mismatch = 1U << 4U,
    maximum_image_mismatch = 1U << 5U,
    candidate_image_mismatch = 1U << 6U,
};

[[nodiscard]] constexpr std::uint32_t firmware_write_binding_issue_bit(
    FirmwareWriteBindingIssue issue) {
    return static_cast<std::uint32_t>(issue);
}

struct FirmwareWriteAdmissionRequest {
    FirmwareBundlePolicyV0 bundle_policy{};
    FirmwareBundleEvidenceV0 bundle_evidence{};
    FirmwareInstallRequest install_request{};
};

struct FirmwareWriteAdmissionResult {
    FirmwareBundleAdmissionResult bundle{};
    FirmwareInstallPreflightResult board{};
    std::uint32_t binding_issue_mask{0};
    bool ready_to_write{false};

    [[nodiscard]] bool has_binding_issue(
        FirmwareWriteBindingIssue issue) const {
        return (binding_issue_mask &
                firmware_write_binding_issue_bit(issue)) != 0;
    }
};

// Final pure admission composition. ready_to_write is true only when the
// independently admitted bundle, board/install preflight, and their exact
// shared fields all agree. The result still performs no write or other I/O.
[[nodiscard]] FirmwareWriteAdmissionResult evaluate_firmware_write(
    const FirmwareWriteAdmissionRequest& request);

}  // namespace opentrail::update
