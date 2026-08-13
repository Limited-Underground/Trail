#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/firmware_install_preflight.hpp"

namespace {

using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

FirmwareInstallRequirements requirements() {
    return {
        0x12345678U,
        ProcessorFamily::esp32_s3,
        FirmwareTargetRole::bench_client,
        1,
        2,
        16U * 1024U * 1024U,
        2U * 1024U * 1024U,
        3,
        3U * 1024U * 1024U,
        6U * 1024U * 1024U};
}

BoardProbeSnapshot exact_probe() {
    return {
        true,
        true,
        ProcessorFamily::esp32_s3,
        true,
        16U * 1024U * 1024U,
        true,
        2U * 1024U * 1024U,
        0x12345678U,
        FirmwareTargetRole::bench_client,
        1,
        BoardProfileEvidence::operator_confirmed,
        true,
        3};
}

FirmwareInstallRequest request() {
    return {exact_probe(), requirements(), FirmwareInstallMode::normal_update,
            false, false};
}

void test_disconnected_device_blocks_inspection_and_flash() {
    auto value = request();
    value.probe.connected = false;
    const auto result = evaluate_firmware_install(value);
    EXPECT(!result.inspection_available);
    EXPECT(!result.flashing_allowed);
    EXPECT(result.has_issue(FirmwareInstallIssue::device_not_connected));
}

void test_runtime_name_is_inspectable_but_not_exact_identity() {
    auto value = request();
    value.probe.profile_evidence = BoardProfileEvidence::runtime_reported;
    const auto result = evaluate_firmware_install(value);
    EXPECT(result.inspection_available);
    EXPECT(!result.flashing_allowed);
    EXPECT(result.has_issue(FirmwareInstallIssue::exact_profile_required));
    EXPECT(!result.has_issue(FirmwareInstallIssue::hardware_profile_mismatch));
}

void test_exact_normal_update_is_allowed_without_erase_confirmation() {
    const auto result = evaluate_firmware_install(request());
    EXPECT(result.inspection_available);
    EXPECT(result.flashing_allowed);
    EXPECT(!result.destructive_install);
    EXPECT(result.issue_mask == 0);

    auto authenticated = request();
    authenticated.probe.profile_evidence =
        BoardProfileEvidence::authenticated_descriptor;
    EXPECT(evaluate_firmware_install(authenticated).flashing_allowed);
}

void test_probe_and_processor_fail_closed_independently() {
    auto value = request();
    value.probe.low_level_probe_complete = false;
    value.probe.processor = ProcessorFamily::unknown;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::low_level_probe_required));
    EXPECT(result.has_issue(FirmwareInstallIssue::processor_unresolved));

    value = request();
    value.probe.processor = ProcessorFamily::nrf52840;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::processor_mismatch));
}

void test_flash_size_must_be_known_and_large_enough() {
    auto value = request();
    value.probe.flash_bytes_known = false;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::flash_size_unresolved));

    value = request();
    value.probe.flash_bytes = 8U * 1024U * 1024U;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::flash_too_small));
}

void test_psram_is_checked_only_when_the_profile_requires_it() {
    auto value = request();
    value.probe.psram_bytes_known = false;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::psram_size_unresolved));

    value = request();
    value.probe.psram_bytes = 1024;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::psram_too_small));

    value = request();
    value.requirements.minimum_psram_bytes = 0;
    value.probe.psram_bytes_known = false;
    result = evaluate_firmware_install(value);
    EXPECT(!result.has_issue(FirmwareInstallIssue::psram_size_unresolved));
    EXPECT(result.flashing_allowed);
}

void test_profile_and_revision_mismatches_are_distinct() {
    auto value = request();
    value.probe.hardware_profile_id = 7;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::hardware_profile_mismatch));

    value = request();
    value.probe.board_revision = 0;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::board_revision_unresolved));

    value = request();
    value.probe.board_revision = 3;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::board_revision_unsupported));
}

void test_firmware_role_must_match_the_exact_profile() {
    auto value = request();
    value.probe.target_role = FirmwareTargetRole::unknown;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::target_role_unresolved));

    value = request();
    value.requirements.target_role = FirmwareTargetRole::complete_client;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::target_role_mismatch));
    EXPECT(!result.flashing_allowed);

    value = request();
    value.probe.target_role = FirmwareTargetRole::packaged_repeater;
    value.requirements.target_role = FirmwareTargetRole::packaged_repeater;
    EXPECT(evaluate_firmware_install(value).flashing_allowed);
}

void test_bootloader_schema_must_be_known_and_current_enough() {
    auto value = request();
    value.probe.bootloader_schema_known = false;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::bootloader_schema_unresolved));

    value = request();
    value.probe.bootloader_schema = 2;
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::bootloader_schema_too_old));
}

void test_clean_install_requires_explicit_destructive_confirmation() {
    auto value = request();
    value.mode = FirmwareInstallMode::clean_install;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.destructive_install);
    EXPECT(result.has_issue(
        FirmwareInstallIssue::destructive_confirmation_required));

    value.destructive_erase_confirmed = true;
    result = evaluate_firmware_install(value);
    EXPECT(result.flashing_allowed);
}

void test_recovery_requires_both_local_authorizations() {
    auto value = request();
    value.mode = FirmwareInstallMode::recovery_install;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(
        FirmwareInstallIssue::destructive_confirmation_required));
    EXPECT(result.has_issue(
        FirmwareInstallIssue::physical_recovery_authorization_required));

    value.destructive_erase_confirmed = true;
    value.physical_recovery_authorized = true;
    result = evaluate_firmware_install(value);
    EXPECT(result.flashing_allowed);
}

void test_oversized_candidate_is_a_specific_blocker() {
    auto value = request();
    value.requirements.candidate_image_bytes =
        value.requirements.maximum_image_bytes + 1U;
    const auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::candidate_image_too_large));
    EXPECT(!result.flashing_allowed);
}

void test_invalid_requirements_and_unknown_values_fail_closed() {
    auto value = request();
    value.requirements.hardware_profile_id = 0;
    auto result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::invalid_request));

    value = request();
    value.mode = static_cast<FirmwareInstallMode>(255);
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::invalid_request));

    value = request();
    value.probe.profile_evidence =
        static_cast<BoardProfileEvidence>(255);
    result = evaluate_firmware_install(value);
    EXPECT(result.has_issue(FirmwareInstallIssue::invalid_request));
}

static_assert(std::is_trivially_copyable_v<BoardProbeSnapshot>);
static_assert(std::is_trivially_copyable_v<FirmwareInstallRequirements>);
static_assert(std::is_trivially_copyable_v<FirmwareInstallPreflightResult>);
static_assert(sizeof(FirmwareInstallPreflightResult) <= 16);

}  // namespace

int main() {
    test_disconnected_device_blocks_inspection_and_flash();
    test_runtime_name_is_inspectable_but_not_exact_identity();
    test_exact_normal_update_is_allowed_without_erase_confirmation();
    test_probe_and_processor_fail_closed_independently();
    test_flash_size_must_be_known_and_large_enough();
    test_psram_is_checked_only_when_the_profile_requires_it();
    test_profile_and_revision_mismatches_are_distinct();
    test_firmware_role_must_match_the_exact_profile();
    test_bootloader_schema_must_be_known_and_current_enough();
    test_clean_install_requires_explicit_destructive_confirmation();
    test_recovery_requires_both_local_authorizations();
    test_oversized_candidate_is_a_specific_blocker();
    test_invalid_requirements_and_unknown_values_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " firmware-install preflight assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 firmware-install preflight scenario groups\n";
    return EXIT_SUCCESS;
}
