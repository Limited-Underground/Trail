#pragma once

#include <cstdint>

#include "opentrail/local_interface.hpp"

namespace opentrail::integration {

enum class UpdateRecoveryPresentationError : std::uint8_t {
    none = 0,
    invalid_revision,
    invalid_diagnostic,
};

struct UpdateRecoveryPresentationResult {
    UpdateRecoveryPresentationError error{
        UpdateRecoveryPresentationError::invalid_diagnostic};
    ui::UiFrame frame{};
    bool has_safe_frame{false};

    [[nodiscard]] constexpr bool decoded() const {
        return error == UpdateRecoveryPresentationError::none;
    }

    [[nodiscard]] constexpr bool presentable() const {
        return has_safe_frame;
    }
};

// Converts one versioned OTRD0 word into the existing semantic UI contract.
// Invalid diagnostics fail visibly to a generic service-required frame when a
// valid boot-local revision is available. No action executes reboot, cleanup,
// confirmation, or service; acknowledgement only dismisses nonblocking notice
// presentation at the application layer.
[[nodiscard]] UpdateRecoveryPresentationResult
make_update_recovery_presentation(
    std::uint32_t diagnostic_word,
    std::uint32_t frame_revision);

}  // namespace opentrail::integration
