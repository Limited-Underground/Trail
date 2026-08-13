#pragma once

#include <array>
#include <cstdint>

#include "opentrail/firmware_install_preflight.hpp"

namespace opentrail::update {

inline constexpr std::uint16_t kFirmwareBundleSchemaV0 = 0;
inline constexpr std::size_t kFirmwareImageDigestBytes = 32;
inline constexpr std::size_t kFirmwareSignerIdBytes = 8;

enum class FirmwareBundleIssue : std::uint32_t {
    invalid_policy = 1U << 0U,
    container_read_incomplete = 1U << 1U,
    manifest_parse_required = 1U << 2U,
    manifest_not_canonical = 1U << 3U,
    schema_unsupported = 1U << 4U,
    manifest_length_mismatch = 1U << 5U,
    manifest_digest_unverified = 1U << 6U,
    signature_unverified = 1U << 7U,
    signer_untrusted = 1U << 8U,
    signer_mismatch = 1U << 9U,
    hardware_profile_mismatch = 1U << 10U,
    processor_mismatch = 1U << 11U,
    target_role_mismatch = 1U << 12U,
    board_revision_range_invalid = 1U << 13U,
    board_revision_range_mismatch = 1U << 14U,
    bootloader_schema_too_old = 1U << 15U,
    release_generation_invalid = 1U << 16U,
    rollback_blocked = 1U << 17U,
    image_read_incomplete = 1U << 18U,
    image_length_invalid = 1U << 19U,
    image_length_mismatch = 1U << 20U,
    image_too_large = 1U << 21U,
    image_digest_unverified = 1U << 22U,
};

[[nodiscard]] constexpr std::uint32_t firmware_bundle_issue_bit(
    FirmwareBundleIssue issue) {
    return static_cast<std::uint32_t>(issue);
}

struct FirmwareBundleManifestV0 {
    std::uint16_t schema{kFirmwareBundleSchemaV0};
    std::uint16_t canonical_manifest_bytes{0};
    std::uint32_t hardware_profile_id{0};
    ProcessorFamily processor{ProcessorFamily::unknown};
    FirmwareTargetRole target_role{FirmwareTargetRole::unknown};
    std::uint16_t minimum_board_revision{0};
    std::uint16_t maximum_board_revision{0};
    std::uint16_t minimum_bootloader_schema{0};
    std::uint64_t release_generation{0};
    std::uint32_t image_bytes{0};
    std::array<std::uint8_t, kFirmwareImageDigestBytes> image_sha256{};
    std::array<std::uint8_t, kFirmwareSignerIdBytes> signer_id{};
};

struct FirmwareBundlePolicyV0 {
    std::uint16_t canonical_manifest_bytes{0};
    std::uint32_t hardware_profile_id{0};
    ProcessorFamily processor{ProcessorFamily::unknown};
    FirmwareTargetRole target_role{FirmwareTargetRole::unknown};
    std::uint16_t minimum_board_revision{0};
    std::uint16_t maximum_board_revision{0};
    std::uint16_t minimum_bootloader_schema{0};
    std::uint64_t trusted_generation_floor{0};
    std::uint32_t maximum_image_bytes{0};
    std::array<std::uint8_t, kFirmwareSignerIdBytes> trusted_signer_id{};
};

// Every Boolean is evidence supplied by a future bounded parser or
// cryptographic adapter. This component does not implement those adapters and
// never infers verification from the mere presence of a digest or signer ID.
struct FirmwareBundleEvidenceV0 {
    bool container_read_complete{false};
    bool manifest_parsed{false};
    bool manifest_canonical{false};
    bool manifest_digest_verified{false};
    bool signature_verified{false};
    bool signer_trusted{false};
    bool image_read_complete{false};
    bool image_digest_verified{false};
    std::uint32_t observed_image_bytes{0};
    FirmwareBundleManifestV0 manifest{};
};

struct FirmwareBundleAdmissionResult {
    std::uint32_t issue_mask{0};
    bool manifest_inspectable{false};
    bool admission_allowed{false};

    [[nodiscard]] bool has_issue(FirmwareBundleIssue issue) const {
        return (issue_mask & firmware_bundle_issue_bit(issue)) != 0;
    }
};

// Pure fail-closed admission over externally supplied evidence. Success means
// the bundle may proceed to the separate board/install preflight; it is never
// permission to erase or write a device.
[[nodiscard]] FirmwareBundleAdmissionResult evaluate_firmware_bundle(
    const FirmwareBundlePolicyV0& policy,
    const FirmwareBundleEvidenceV0& evidence);

}  // namespace opentrail::update
