#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/firmware_bundle_admission.hpp"

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
    return {0x41, 0x11, 0xA2, 0x22, 0xB3, 0x33, 0xC4, 0x44};
}

std::array<std::uint8_t, kFirmwareImageDigestBytes> image_digest() {
    std::array<std::uint8_t, kFirmwareImageDigestBytes> value{};
    value[0] = 0xA5;
    value[value.size() - 1] = 0x5A;
    return value;
}

FirmwareBundlePolicyV0 policy() {
    return {
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
}

FirmwareBundleEvidenceV0 evidence() {
    FirmwareBundleManifestV0 manifest{};
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
    manifest.image_sha256 = image_digest();
    manifest.signer_id = signer_id();
    return {true, true, true, true, true, true, true, true,
            manifest.image_bytes, manifest};
}

void test_exact_bundle_is_admitted_to_next_preflight_only() {
    const auto result = evaluate_firmware_bundle(policy(), evidence());
    EXPECT(result.manifest_inspectable);
    EXPECT(result.admission_allowed);
    EXPECT(result.issue_mask == 0);
}

void test_invalid_policy_fails_before_admission() {
    auto value = policy();
    value.hardware_profile_id = 0;
    auto result = evaluate_firmware_bundle(value, evidence());
    EXPECT(result.has_issue(FirmwareBundleIssue::invalid_policy));
    EXPECT(!result.admission_allowed);

    value = policy();
    value.trusted_signer_id = {};
    result = evaluate_firmware_bundle(value, evidence());
    EXPECT(result.has_issue(FirmwareBundleIssue::invalid_policy));
}

void test_container_and_parse_evidence_are_distinct() {
    auto value = evidence();
    value.container_read_complete = false;
    value.manifest_parsed = false;
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(!result.manifest_inspectable);
    EXPECT(result.has_issue(FirmwareBundleIssue::container_read_incomplete));
    EXPECT(result.has_issue(FirmwareBundleIssue::manifest_parse_required));
}

void test_schema_canonical_form_and_length_are_exact() {
    auto value = evidence();
    value.manifest.schema = 1;
    value.manifest_canonical = false;
    value.manifest.canonical_manifest_bytes = 127;
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::schema_unsupported));
    EXPECT(result.has_issue(FirmwareBundleIssue::manifest_not_canonical));
    EXPECT(result.has_issue(FirmwareBundleIssue::manifest_length_mismatch));
}

void test_manifest_digest_and_signature_are_independent() {
    auto value = evidence();
    value.manifest_digest_verified = false;
    value.signature_verified = false;
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::manifest_digest_unverified));
    EXPECT(result.has_issue(FirmwareBundleIssue::signature_unverified));
}

void test_signer_trust_and_exact_id_are_independent() {
    auto value = evidence();
    value.signer_trusted = false;
    value.manifest.signer_id[0] ^= 0xFF;
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::signer_untrusted));
    EXPECT(result.has_issue(FirmwareBundleIssue::signer_mismatch));
}

void test_profile_processor_and_role_cannot_cross() {
    auto value = evidence();
    value.manifest.hardware_profile_id = 4;
    value.manifest.processor = ProcessorFamily::nrf52840;
    value.manifest.target_role = FirmwareTargetRole::packaged_repeater;
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::hardware_profile_mismatch));
    EXPECT(result.has_issue(FirmwareBundleIssue::processor_mismatch));
    EXPECT(result.has_issue(FirmwareBundleIssue::target_role_mismatch));
}

void test_revision_range_must_be_valid_and_exact() {
    auto value = evidence();
    value.manifest.minimum_board_revision = 0;
    auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::board_revision_range_invalid));

    value = evidence();
    value.manifest.maximum_board_revision = 3;
    result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::board_revision_range_mismatch));
}

void test_bootloader_and_generation_floor_are_bound() {
    auto value = evidence();
    value.manifest.minimum_bootloader_schema = 2;
    value.manifest.release_generation = 6;
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::bootloader_schema_too_old));
    EXPECT(result.has_issue(FirmwareBundleIssue::rollback_blocked));

    value = evidence();
    value.manifest.release_generation = 0;
    EXPECT(evaluate_firmware_bundle(policy(), value).has_issue(
        FirmwareBundleIssue::release_generation_invalid));
}

void test_image_read_length_capacity_and_digest_all_gate() {
    auto value = evidence();
    value.image_read_complete = false;
    value.observed_image_bytes = value.manifest.image_bytes - 1;
    value.image_digest_verified = false;
    auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::image_read_incomplete));
    EXPECT(result.has_issue(FirmwareBundleIssue::image_length_mismatch));
    EXPECT(result.has_issue(FirmwareBundleIssue::image_digest_unverified));

    value = evidence();
    value.manifest.image_bytes = 7U * 1024U * 1024U;
    value.observed_image_bytes = value.manifest.image_bytes;
    result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::image_too_large));

    value = evidence();
    value.manifest.image_bytes = 0;
    value.observed_image_bytes = 0;
    result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::image_length_invalid));
}

void test_digest_presence_never_substitutes_for_verification() {
    auto value = evidence();
    value.manifest.image_sha256 = {};
    auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::image_digest_unverified));

    value = evidence();
    value.image_digest_verified = false;
    result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::image_digest_unverified));
}

void test_unknown_manifest_enums_fail_closed() {
    auto value = evidence();
    value.manifest.processor = static_cast<ProcessorFamily>(255);
    value.manifest.target_role = static_cast<FirmwareTargetRole>(255);
    const auto result = evaluate_firmware_bundle(policy(), value);
    EXPECT(result.has_issue(FirmwareBundleIssue::processor_mismatch));
    EXPECT(result.has_issue(FirmwareBundleIssue::target_role_mismatch));
}

static_assert(std::is_trivially_copyable_v<FirmwareBundleManifestV0>);
static_assert(std::is_trivially_copyable_v<FirmwareBundleEvidenceV0>);
static_assert(std::is_trivially_copyable_v<FirmwareBundleAdmissionResult>);
static_assert(sizeof(FirmwareBundleAdmissionResult) <= 8);

}  // namespace

int main() {
    test_exact_bundle_is_admitted_to_next_preflight_only();
    test_invalid_policy_fails_before_admission();
    test_container_and_parse_evidence_are_distinct();
    test_schema_canonical_form_and_length_are_exact();
    test_manifest_digest_and_signature_are_independent();
    test_signer_trust_and_exact_id_are_independent();
    test_profile_processor_and_role_cannot_cross();
    test_revision_range_must_be_valid_and_exact();
    test_bootloader_and_generation_floor_are_bound();
    test_image_read_length_capacity_and_digest_all_gate();
    test_digest_presence_never_substitutes_for_verification();
    test_unknown_manifest_enums_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " firmware-bundle admission assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 firmware-bundle admission scenario groups\n";
    return EXIT_SUCCESS;
}
