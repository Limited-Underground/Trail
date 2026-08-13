#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/firmware_write_admission.hpp"

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

std::array<std::uint8_t, kFirmwareSignerIdBytes> signer_id() {
    return {1, 2, 3, 4, 5, 6, 7, 8};
}

FirmwareWriteAdmissionRequest request() {
    FirmwareWriteAdmissionRequest value{};
    value.bundle_policy = {
        128,
        0x12345678U,
        ProcessorFamily::esp32_s3,
        FirmwareTargetRole::bench_client,
        1,
        2,
        3,
        7,
        6U * 1024U * 1024U,
        signer_id()};

    auto& manifest = value.bundle_evidence.manifest;
    manifest.schema = kFirmwareBundleSchemaV0;
    manifest.canonical_manifest_bytes = 128;
    manifest.hardware_profile_id = 0x12345678U;
    manifest.processor = ProcessorFamily::esp32_s3;
    manifest.target_role = FirmwareTargetRole::bench_client;
    manifest.minimum_board_revision = 1;
    manifest.maximum_board_revision = 2;
    manifest.minimum_bootloader_schema = 3;
    manifest.release_generation = 7;
    manifest.image_bytes = 3U * 1024U * 1024U;
    manifest.image_sha256[0] = 0xA5;
    manifest.signer_id = signer_id();
    value.bundle_evidence.container_read_complete = true;
    value.bundle_evidence.manifest_parsed = true;
    value.bundle_evidence.manifest_canonical = true;
    value.bundle_evidence.manifest_digest_verified = true;
    value.bundle_evidence.signature_verified = true;
    value.bundle_evidence.signer_trusted = true;
    value.bundle_evidence.image_read_complete = true;
    value.bundle_evidence.image_digest_verified = true;
    value.bundle_evidence.observed_image_bytes = manifest.image_bytes;

    value.install_request.probe = {
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
    value.install_request.requirements = {
        0x12345678U,
        ProcessorFamily::esp32_s3,
        FirmwareTargetRole::bench_client,
        1,
        2,
        16U * 1024U * 1024U,
        2U * 1024U * 1024U,
        3,
        manifest.image_bytes,
        6U * 1024U * 1024U};
    value.install_request.mode = FirmwareInstallMode::normal_update;
    return value;
}

void test_exact_composition_is_ready_without_writing() {
    const auto result = evaluate_firmware_write(request());
    EXPECT(result.bundle.admission_allowed);
    EXPECT(result.board.flashing_allowed);
    EXPECT(result.binding_issue_mask == 0);
    EXPECT(result.ready_to_write);
}

void test_bundle_failure_blocks_ready_even_when_board_passes() {
    auto value = request();
    value.bundle_evidence.signature_verified = false;
    const auto result = evaluate_firmware_write(value);
    EXPECT(result.bundle.has_issue(FirmwareBundleIssue::signature_unverified));
    EXPECT(result.board.flashing_allowed);
    EXPECT(!result.ready_to_write);
}

void test_board_failure_blocks_ready_even_when_bundle_passes() {
    auto value = request();
    value.install_request.probe.connected = false;
    const auto result = evaluate_firmware_write(value);
    EXPECT(result.bundle.admission_allowed);
    EXPECT(result.board.has_issue(FirmwareInstallIssue::device_not_connected));
    EXPECT(!result.ready_to_write);
}

void test_profile_binding_cannot_diverge_between_gates() {
    auto value = request();
    value.install_request.requirements.hardware_profile_id ^= 1U;
    const auto result = evaluate_firmware_write(value);
    EXPECT(result.has_binding_issue(
        FirmwareWriteBindingIssue::hardware_profile_mismatch));
    EXPECT(!result.ready_to_write);
}

void test_processor_and_role_bindings_are_exact() {
    auto value = request();
    value.install_request.requirements.processor = ProcessorFamily::nrf52840;
    value.install_request.requirements.target_role =
        FirmwareTargetRole::complete_client;
    const auto result = evaluate_firmware_write(value);
    EXPECT(result.has_binding_issue(FirmwareWriteBindingIssue::processor_mismatch));
    EXPECT(result.has_binding_issue(FirmwareWriteBindingIssue::target_role_mismatch));
    EXPECT(!result.ready_to_write);
}

void test_revision_and_bootloader_policy_cannot_diverge() {
    auto value = request();
    value.install_request.requirements.maximum_board_revision = 3;
    value.install_request.requirements.minimum_bootloader_schema = 4;
    const auto result = evaluate_firmware_write(value);
    EXPECT(result.has_binding_issue(
        FirmwareWriteBindingIssue::board_revision_range_mismatch));
    EXPECT(result.has_binding_issue(
        FirmwareWriteBindingIssue::bootloader_schema_mismatch));
}

void test_image_capacity_and_candidate_length_are_bound() {
    auto value = request();
    value.install_request.requirements.maximum_image_bytes -= 1U;
    value.install_request.requirements.candidate_image_bytes -= 1U;
    const auto result = evaluate_firmware_write(value);
    EXPECT(result.has_binding_issue(
        FirmwareWriteBindingIssue::maximum_image_mismatch));
    EXPECT(result.has_binding_issue(
        FirmwareWriteBindingIssue::candidate_image_mismatch));
}

void test_destructive_authorizations_remain_board_gates() {
    auto value = request();
    value.install_request.mode = FirmwareInstallMode::recovery_install;
    auto result = evaluate_firmware_write(value);
    EXPECT(result.bundle.admission_allowed);
    EXPECT(result.board.has_issue(
        FirmwareInstallIssue::destructive_confirmation_required));
    EXPECT(result.board.has_issue(
        FirmwareInstallIssue::physical_recovery_authorization_required));
    EXPECT(!result.ready_to_write);

    value.install_request.destructive_erase_confirmed = true;
    value.install_request.physical_recovery_authorized = true;
    result = evaluate_firmware_write(value);
    EXPECT(result.ready_to_write);
}

static_assert(std::is_trivially_copyable_v<FirmwareWriteAdmissionRequest>);
static_assert(std::is_trivially_copyable_v<FirmwareWriteAdmissionResult>);

}  // namespace

int main() {
    test_exact_composition_is_ready_without_writing();
    test_bundle_failure_blocks_ready_even_when_board_passes();
    test_board_failure_blocks_ready_even_when_bundle_passes();
    test_profile_binding_cannot_diverge_between_gates();
    test_processor_and_role_bindings_are_exact();
    test_revision_and_bootloader_policy_cannot_diverge();
    test_image_capacity_and_candidate_length_are_bound();
    test_destructive_authorizations_remain_board_gates();

    if (failures != 0) {
        std::cerr << failures << " firmware-write admission assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 firmware-write admission scenario groups\n";
    return EXIT_SUCCESS;
}
