#include "opentrail/firmware_write_admission.hpp"

namespace opentrail::update {
namespace {

void add_issue(
    FirmwareWriteAdmissionResult& result,
    FirmwareWriteBindingIssue issue) {
    result.binding_issue_mask |= firmware_write_binding_issue_bit(issue);
}

}  // namespace

FirmwareWriteAdmissionResult evaluate_firmware_write(
    const FirmwareWriteAdmissionRequest& request) {
    FirmwareWriteAdmissionResult result{};
    result.bundle = evaluate_firmware_bundle(
        request.bundle_policy, request.bundle_evidence);
    result.board = evaluate_firmware_install(request.install_request);

    const auto& policy = request.bundle_policy;
    const auto& requirements = request.install_request.requirements;
    const auto& manifest = request.bundle_evidence.manifest;

    if (policy.hardware_profile_id != requirements.hardware_profile_id) {
        add_issue(
            result, FirmwareWriteBindingIssue::hardware_profile_mismatch);
    }
    if (policy.processor != requirements.processor) {
        add_issue(result, FirmwareWriteBindingIssue::processor_mismatch);
    }
    if (policy.target_role != requirements.target_role) {
        add_issue(result, FirmwareWriteBindingIssue::target_role_mismatch);
    }
    if (policy.minimum_board_revision !=
            requirements.minimum_board_revision ||
        policy.maximum_board_revision !=
            requirements.maximum_board_revision) {
        add_issue(
            result,
            FirmwareWriteBindingIssue::board_revision_range_mismatch);
    }
    if (policy.minimum_bootloader_schema !=
        requirements.minimum_bootloader_schema) {
        add_issue(
            result, FirmwareWriteBindingIssue::bootloader_schema_mismatch);
    }
    if (policy.maximum_image_bytes != requirements.maximum_image_bytes) {
        add_issue(result, FirmwareWriteBindingIssue::maximum_image_mismatch);
    }
    if (manifest.image_bytes != requirements.candidate_image_bytes) {
        add_issue(result, FirmwareWriteBindingIssue::candidate_image_mismatch);
    }

    result.ready_to_write = result.bundle.admission_allowed &&
                            result.board.flashing_allowed &&
                            result.binding_issue_mask == 0;
    return result;
}

}  // namespace opentrail::update
