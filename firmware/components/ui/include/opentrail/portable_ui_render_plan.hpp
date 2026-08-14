#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/local_interface.hpp"

namespace opentrail::ui {

constexpr std::size_t kMaxUiRenderPrimitives = 16;
constexpr std::uint8_t kNoActionSlot = 0xFF;

enum class UiRenderPrimitiveKind : std::uint8_t {
    panel = 0,
    text,
    indicator,
    metric,
    action,
};

enum class UiRenderStyle : std::uint8_t {
    surface = 0,
    heading,
    body,
    muted,
    information,
    warning,
    critical,
    positive,
};

enum class UiViewportShape : std::uint8_t {
    rectangle = 0,
    circle,
};

// Tokens are stable semantic localization keys, never user text. Screen,
// notice, action, and indicator-state tokens occupy fixed numeric ranges.
enum class UiTextToken : std::uint16_t {
    none = 0,
    peers = 1,
    unread = 2,
    archive_queue = 3,
    screen_base = 100,
    notice_base = 200,
    action_base = 300,
    radio_indicator_base = 400,
    position_indicator_base = 410,
    power_indicator_base = 420,
};

struct UiLogicalDisplayProfile {
    std::uint16_t width{0};
    std::uint16_t height{0};
    std::uint16_t minimum_action_extent{0};
    UiViewportShape shape{UiViewportShape::rectangle};
};

constexpr UiLogicalDisplayProfile kSimulatorLogicalDisplayProfile{
    466, 466, 64, UiViewportShape::circle};

struct UiRenderRect {
    std::uint16_t x{0};
    std::uint16_t y{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
};

struct UiRenderPrimitive {
    UiRenderPrimitiveKind kind{UiRenderPrimitiveKind::panel};
    UiRenderRect bounds{};
    UiTextToken text{UiTextToken::none};
    UiRenderStyle style{UiRenderStyle::surface};
    std::uint8_t action_slot{kNoActionSlot};
    bool enabled{false};
    bool requires_hold{false};
    bool numeric_value_valid{false};
    std::uint16_t numeric_value{0};
    std::uint8_t owned_text_index{kNoUiOwnedText};
};

struct UiRenderPlan {
    std::uint32_t frame_revision{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
    UiViewportShape shape{UiViewportShape::rectangle};
    std::uint8_t primitive_count{0};
    std::array<UiRenderPrimitive, kMaxUiRenderPrimitives> primitives{};
};

enum class UiRenderPlanError : std::uint8_t {
    none = 0,
    invalid_profile,
    invalid_frame,
    capacity_exceeded,
    primitive_out_of_bounds,
    action_mismatch,
};

struct UiRenderPlanResult {
    UiRenderPlanError error{UiRenderPlanError::invalid_frame};
    UiRenderPlan plan{};

    [[nodiscard]] constexpr bool ok() const {
        return error == UiRenderPlanError::none;
    }
};

[[nodiscard]] UiRenderPlanResult make_portable_ui_render_plan(
    const UiFrame& frame,
    UiLogicalDisplayProfile profile);

[[nodiscard]] UiRenderPlanResult make_portable_ui_render_plan(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar,
    UiLogicalDisplayProfile profile);

[[nodiscard]] UiRenderPlanError validate_portable_ui_render_plan(
    const UiFrame& frame,
    const UiRenderPlan& plan,
    UiLogicalDisplayProfile profile);

[[nodiscard]] UiRenderPlanError validate_portable_ui_render_plan(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar,
    const UiRenderPlan& plan,
    UiLogicalDisplayProfile profile);

[[nodiscard]] bool valid_portable_ui_presentation(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar);

}  // namespace opentrail::ui
