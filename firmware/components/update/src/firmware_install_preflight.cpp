#include "opentrail/firmware_install_preflight.hpp"

namespace opentrail::update {
namespace {

bool known_processor(ProcessorFamily processor) {
    return processor == ProcessorFamily::esp32_s3 ||
           processor == ProcessorFamily::nrf52840;
}

bool known_profile_evidence(BoardProfileEvidence evidence) {
    return evidence == BoardProfileEvidence::none ||
           evidence == BoardProfileEvidence::runtime_reported ||
           evidence == BoardProfileEvidence::operator_confirmed ||
           evidence == BoardProfileEvidence::authenticated_descriptor;
}

bool exact_profile_evidence(BoardProfileEvidence evidence) {
    return evidence == BoardProfileEvidence::operator_confirmed ||
           evidence == BoardProfileEvidence::authenticated_descriptor;
}

bool known_mode(FirmwareInstallMode mode) {
    return mode == FirmwareInstallMode::normal_update ||
           mode == FirmwareInstallMode::clean_install ||
           mode == FirmwareInstallMode::recovery_install;
}

bool known_target_role(FirmwareTargetRole role) {
    return role == FirmwareTargetRole::bench_client ||
           role == FirmwareTargetRole::complete_client ||
           role == FirmwareTargetRole::packaged_repeater;
}

void add_issue(
    FirmwareInstallPreflightResult& result,
    FirmwareInstallIssue issue) {
    result.issue_mask |= install_issue_bit(issue);
}

bool valid_requirements(const FirmwareInstallRequirements& requirements) {
    return requirements.hardware_profile_id != 0 &&
           known_processor(requirements.processor) &&
           known_target_role(requirements.target_role) &&
           requirements.minimum_board_revision != 0 &&
           requirements.maximum_board_revision >=
               requirements.minimum_board_revision &&
           requirements.minimum_flash_bytes != 0 &&
           requirements.minimum_bootloader_schema != 0 &&
           requirements.candidate_image_bytes != 0 &&
           requirements.maximum_image_bytes != 0;
}

}  // namespace

FirmwareInstallPreflightResult evaluate_firmware_install(
    const FirmwareInstallRequest& request) {
    FirmwareInstallPreflightResult result{};
    result.inspection_available = request.probe.connected;
    result.destructive_install =
        request.mode == FirmwareInstallMode::clean_install ||
        request.mode == FirmwareInstallMode::recovery_install;

    if (!known_mode(request.mode) ||
        !known_profile_evidence(request.probe.profile_evidence) ||
        !valid_requirements(request.requirements)) {
        add_issue(result, FirmwareInstallIssue::invalid_request);
        return result;
    }
    if (request.requirements.candidate_image_bytes >
        request.requirements.maximum_image_bytes) {
        add_issue(result, FirmwareInstallIssue::candidate_image_too_large);
    }
    if (!request.probe.connected) {
        add_issue(result, FirmwareInstallIssue::device_not_connected);
        return result;
    }
    if (!request.probe.low_level_probe_complete) {
        add_issue(result, FirmwareInstallIssue::low_level_probe_required);
    }
    if (!known_processor(request.probe.processor)) {
        add_issue(result, FirmwareInstallIssue::processor_unresolved);
    } else if (request.probe.processor != request.requirements.processor) {
        add_issue(result, FirmwareInstallIssue::processor_mismatch);
    }

    if (!request.probe.flash_bytes_known || request.probe.flash_bytes == 0) {
        add_issue(result, FirmwareInstallIssue::flash_size_unresolved);
    } else if (request.probe.flash_bytes <
               request.requirements.minimum_flash_bytes) {
        add_issue(result, FirmwareInstallIssue::flash_too_small);
    }

    if (request.requirements.minimum_psram_bytes != 0) {
        if (!request.probe.psram_bytes_known) {
            add_issue(result, FirmwareInstallIssue::psram_size_unresolved);
        } else if (request.probe.psram_bytes <
                   request.requirements.minimum_psram_bytes) {
            add_issue(result, FirmwareInstallIssue::psram_too_small);
        }
    }

    if (!exact_profile_evidence(request.probe.profile_evidence)) {
        add_issue(result, FirmwareInstallIssue::exact_profile_required);
    } else {
        if (request.probe.hardware_profile_id !=
            request.requirements.hardware_profile_id) {
            add_issue(result, FirmwareInstallIssue::hardware_profile_mismatch);
        }
        if (!known_target_role(request.probe.target_role)) {
            add_issue(result, FirmwareInstallIssue::target_role_unresolved);
        } else if (request.probe.target_role !=
                   request.requirements.target_role) {
            add_issue(result, FirmwareInstallIssue::target_role_mismatch);
        }
        if (request.probe.board_revision == 0) {
            add_issue(result, FirmwareInstallIssue::board_revision_unresolved);
        } else if (request.probe.board_revision <
                       request.requirements.minimum_board_revision ||
                   request.probe.board_revision >
                       request.requirements.maximum_board_revision) {
            add_issue(result, FirmwareInstallIssue::board_revision_unsupported);
        }
    }

    if (!request.probe.bootloader_schema_known ||
        request.probe.bootloader_schema == 0) {
        add_issue(result, FirmwareInstallIssue::bootloader_schema_unresolved);
    } else if (request.probe.bootloader_schema <
               request.requirements.minimum_bootloader_schema) {
        add_issue(result, FirmwareInstallIssue::bootloader_schema_too_old);
    }

    if (result.destructive_install &&
        !request.destructive_erase_confirmed) {
        add_issue(
            result,
            FirmwareInstallIssue::destructive_confirmation_required);
    }
    if (request.mode == FirmwareInstallMode::recovery_install &&
        !request.physical_recovery_authorized) {
        add_issue(
            result,
            FirmwareInstallIssue::physical_recovery_authorization_required);
    }

    result.flashing_allowed = result.issue_mask == 0;
    return result;
}

}  // namespace opentrail::update
