#pragma once

#include <cstdint>

namespace opentrail::update {

enum class ProcessorFamily : std::uint8_t {
    unknown = 0,
    esp32_s3,
    nrf52840,
};

enum class BoardProfileEvidence : std::uint8_t {
    none = 0,
    runtime_reported,
    operator_confirmed,
    authenticated_descriptor,
};

enum class FirmwareInstallMode : std::uint8_t {
    normal_update = 0,
    clean_install,
    recovery_install,
};

enum class FirmwareTargetRole : std::uint8_t {
    unknown = 0,
    bench_client,
    complete_client,
    packaged_repeater,
};

enum class FirmwareInstallIssue : std::uint32_t {
    invalid_request = 1U << 0U,
    device_not_connected = 1U << 1U,
    low_level_probe_required = 1U << 2U,
    processor_unresolved = 1U << 3U,
    processor_mismatch = 1U << 4U,
    flash_size_unresolved = 1U << 5U,
    flash_too_small = 1U << 6U,
    psram_size_unresolved = 1U << 7U,
    psram_too_small = 1U << 8U,
    exact_profile_required = 1U << 9U,
    hardware_profile_mismatch = 1U << 10U,
    target_role_unresolved = 1U << 11U,
    target_role_mismatch = 1U << 12U,
    board_revision_unresolved = 1U << 13U,
    board_revision_unsupported = 1U << 14U,
    bootloader_schema_unresolved = 1U << 15U,
    bootloader_schema_too_old = 1U << 16U,
    candidate_image_too_large = 1U << 17U,
    destructive_confirmation_required = 1U << 18U,
    physical_recovery_authorization_required = 1U << 19U,
};

[[nodiscard]] constexpr std::uint32_t install_issue_bit(
    FirmwareInstallIssue issue) {
    return static_cast<std::uint32_t>(issue);
}

struct BoardProbeSnapshot {
    bool connected{false};
    bool low_level_probe_complete{false};
    ProcessorFamily processor{ProcessorFamily::unknown};
    bool flash_bytes_known{false};
    std::uint32_t flash_bytes{0};
    bool psram_bytes_known{false};
    std::uint32_t psram_bytes{0};
    std::uint32_t hardware_profile_id{0};
    FirmwareTargetRole target_role{FirmwareTargetRole::unknown};
    std::uint16_t board_revision{0};
    BoardProfileEvidence profile_evidence{BoardProfileEvidence::none};
    bool bootloader_schema_known{false};
    std::uint16_t bootloader_schema{0};
};

struct FirmwareInstallRequirements {
    std::uint32_t hardware_profile_id{0};
    ProcessorFamily processor{ProcessorFamily::unknown};
    FirmwareTargetRole target_role{FirmwareTargetRole::unknown};
    std::uint16_t minimum_board_revision{0};
    std::uint16_t maximum_board_revision{0};
    std::uint32_t minimum_flash_bytes{0};
    std::uint32_t minimum_psram_bytes{0};
    std::uint16_t minimum_bootloader_schema{0};
    std::uint32_t candidate_image_bytes{0};
    std::uint32_t maximum_image_bytes{0};
};

struct FirmwareInstallRequest {
    BoardProbeSnapshot probe{};
    FirmwareInstallRequirements requirements{};
    FirmwareInstallMode mode{FirmwareInstallMode::normal_update};
    bool destructive_erase_confirmed{false};
    bool physical_recovery_authorized{false};
};

struct FirmwareInstallPreflightResult {
    std::uint32_t issue_mask{0};
    bool inspection_available{false};
    bool flashing_allowed{false};
    bool destructive_install{false};

    [[nodiscard]] bool has_issue(FirmwareInstallIssue issue) const {
        return (issue_mask & install_issue_bit(issue)) != 0;
    }
};

// Pure fail-closed policy over adapter-supplied evidence. This function does
// not discover USB devices, trust runtime names, verify signatures, erase, or
// write firmware.
[[nodiscard]] FirmwareInstallPreflightResult evaluate_firmware_install(
    const FirmwareInstallRequest& request);

}  // namespace opentrail::update
