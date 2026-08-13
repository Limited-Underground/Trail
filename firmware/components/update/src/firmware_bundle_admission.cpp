#include "opentrail/firmware_bundle_admission.hpp"

#include <algorithm>

namespace opentrail::update {
namespace {

bool known_processor(ProcessorFamily processor) {
    return processor == ProcessorFamily::esp32_s3 ||
           processor == ProcessorFamily::nrf52840;
}

bool known_target_role(FirmwareTargetRole role) {
    return role == FirmwareTargetRole::bench_client ||
           role == FirmwareTargetRole::complete_client ||
           role == FirmwareTargetRole::packaged_repeater;
}

template <std::size_t Size>
bool has_nonzero(const std::array<std::uint8_t, Size>& value) {
    return std::any_of(value.begin(), value.end(), [](std::uint8_t byte) {
        return byte != 0;
    });
}

bool valid_policy(const FirmwareBundlePolicyV0& policy) {
    return policy.canonical_manifest_bytes != 0 &&
           policy.hardware_profile_id != 0 &&
           known_processor(policy.processor) &&
           known_target_role(policy.target_role) &&
           policy.minimum_board_revision != 0 &&
           policy.maximum_board_revision >= policy.minimum_board_revision &&
           policy.minimum_bootloader_schema != 0 &&
           policy.trusted_generation_floor != 0 &&
           policy.maximum_image_bytes != 0 &&
           has_nonzero(policy.trusted_signer_id);
}

void add_issue(
    FirmwareBundleAdmissionResult& result,
    FirmwareBundleIssue issue) {
    result.issue_mask |= firmware_bundle_issue_bit(issue);
}

}  // namespace

FirmwareBundleAdmissionResult evaluate_firmware_bundle(
    const FirmwareBundlePolicyV0& policy,
    const FirmwareBundleEvidenceV0& evidence) {
    FirmwareBundleAdmissionResult result{};
    result.manifest_inspectable =
        evidence.container_read_complete && evidence.manifest_parsed;

    if (!valid_policy(policy)) {
        add_issue(result, FirmwareBundleIssue::invalid_policy);
        return result;
    }

    if (!evidence.container_read_complete) {
        add_issue(result, FirmwareBundleIssue::container_read_incomplete);
    }
    if (!evidence.manifest_parsed) {
        add_issue(result, FirmwareBundleIssue::manifest_parse_required);
    }
    if (!evidence.manifest_canonical) {
        add_issue(result, FirmwareBundleIssue::manifest_not_canonical);
    }
    if (evidence.manifest.schema != kFirmwareBundleSchemaV0) {
        add_issue(result, FirmwareBundleIssue::schema_unsupported);
    }
    if (evidence.manifest.canonical_manifest_bytes !=
        policy.canonical_manifest_bytes) {
        add_issue(result, FirmwareBundleIssue::manifest_length_mismatch);
    }
    if (!evidence.manifest_digest_verified) {
        add_issue(result, FirmwareBundleIssue::manifest_digest_unverified);
    }
    if (!evidence.signature_verified) {
        add_issue(result, FirmwareBundleIssue::signature_unverified);
    }
    if (!evidence.signer_trusted) {
        add_issue(result, FirmwareBundleIssue::signer_untrusted);
    }
    if (evidence.manifest.signer_id != policy.trusted_signer_id) {
        add_issue(result, FirmwareBundleIssue::signer_mismatch);
    }
    if (evidence.manifest.hardware_profile_id != policy.hardware_profile_id) {
        add_issue(result, FirmwareBundleIssue::hardware_profile_mismatch);
    }
    if (!known_processor(evidence.manifest.processor) ||
        evidence.manifest.processor != policy.processor) {
        add_issue(result, FirmwareBundleIssue::processor_mismatch);
    }
    if (!known_target_role(evidence.manifest.target_role) ||
        evidence.manifest.target_role != policy.target_role) {
        add_issue(result, FirmwareBundleIssue::target_role_mismatch);
    }
    if (evidence.manifest.minimum_board_revision == 0 ||
        evidence.manifest.maximum_board_revision <
            evidence.manifest.minimum_board_revision) {
        add_issue(result, FirmwareBundleIssue::board_revision_range_invalid);
    } else if (evidence.manifest.minimum_board_revision !=
                   policy.minimum_board_revision ||
               evidence.manifest.maximum_board_revision !=
                   policy.maximum_board_revision) {
        add_issue(result, FirmwareBundleIssue::board_revision_range_mismatch);
    }
    if (evidence.manifest.minimum_bootloader_schema <
        policy.minimum_bootloader_schema) {
        add_issue(result, FirmwareBundleIssue::bootloader_schema_too_old);
    }
    if (evidence.manifest.release_generation == 0) {
        add_issue(result, FirmwareBundleIssue::release_generation_invalid);
    } else if (evidence.manifest.release_generation <
               policy.trusted_generation_floor) {
        add_issue(result, FirmwareBundleIssue::rollback_blocked);
    }
    if (!evidence.image_read_complete) {
        add_issue(result, FirmwareBundleIssue::image_read_incomplete);
    }
    if (evidence.manifest.image_bytes == 0 ||
        evidence.observed_image_bytes == 0) {
        add_issue(result, FirmwareBundleIssue::image_length_invalid);
    } else if (evidence.manifest.image_bytes != evidence.observed_image_bytes) {
        add_issue(result, FirmwareBundleIssue::image_length_mismatch);
    }
    if (evidence.manifest.image_bytes > policy.maximum_image_bytes ||
        evidence.observed_image_bytes > policy.maximum_image_bytes) {
        add_issue(result, FirmwareBundleIssue::image_too_large);
    }
    if (!has_nonzero(evidence.manifest.image_sha256) ||
        !evidence.image_digest_verified) {
        add_issue(result, FirmwareBundleIssue::image_digest_unverified);
    }

    result.admission_allowed = result.issue_mask == 0;
    return result;
}

}  // namespace opentrail::update
