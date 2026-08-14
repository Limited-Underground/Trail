#include "opentrail/portable_ui_render_plan.hpp"

namespace opentrail::ui {
namespace {

std::uint16_t scaled(std::uint16_t value, std::uint16_t extent) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(value) * extent) / 480U);
}

UiTextToken token_with_offset(UiTextToken base, std::uint16_t value) {
    return static_cast<UiTextToken>(
        static_cast<std::uint16_t>(base) + value);
}

UiRenderStyle attention_style(UiAttention attention) {
    switch (attention) {
        case UiAttention::information:
            return UiRenderStyle::information;
        case UiAttention::warning:
            return UiRenderStyle::warning;
        case UiAttention::critical:
            return UiRenderStyle::critical;
        default:
            return UiRenderStyle::body;
    }
}

UiRenderStyle indicator_style(UiIndicatorState state) {
    switch (state) {
        case UiIndicatorState::normal:
            return UiRenderStyle::positive;
        case UiIndicatorState::warning:
            return UiRenderStyle::warning;
        case UiIndicatorState::critical:
            return UiRenderStyle::critical;
        default:
            return UiRenderStyle::muted;
    }
}

bool append(UiRenderPlan& plan, const UiRenderPrimitive& primitive) {
    if (plan.primitive_count >= plan.primitives.size()) {
        return false;
    }
    plan.primitives[plan.primitive_count++] = primitive;
    return true;
}

UiRenderRect rect(
    UiLogicalDisplayProfile profile,
    std::uint16_t x,
    std::uint16_t y,
    std::uint16_t width,
    std::uint16_t height) {
    return {
        scaled(x, profile.width),
        scaled(y, profile.height),
        scaled(width, profile.width),
        scaled(height, profile.height),
    };
}

bool in_bounds(const UiRenderPrimitive& primitive, const UiRenderPlan& plan) {
    const auto right = static_cast<std::uint32_t>(primitive.bounds.x) +
                       primitive.bounds.width;
    const auto bottom = static_cast<std::uint32_t>(primitive.bounds.y) +
                        primitive.bounds.height;
    return primitive.bounds.width != 0 && primitive.bounds.height != 0 &&
           right <= plan.width && bottom <= plan.height;
}

bool primitive_inside_circle(
    const UiRenderPrimitive& primitive,
    const UiRenderPlan& plan) {
    if (plan.shape != UiViewportShape::circle) {
        return true;
    }
    const auto center_x = static_cast<std::int64_t>(plan.width) / 2;
    const auto center_y = static_cast<std::int64_t>(plan.height) / 2;
    const auto radius = static_cast<std::int64_t>(
        plan.width < plan.height ? plan.width : plan.height) / 2;
    const std::array<std::pair<std::int64_t, std::int64_t>, 4> corners{{
        {primitive.bounds.x, primitive.bounds.y},
        {static_cast<std::int64_t>(primitive.bounds.x) + primitive.bounds.width,
         primitive.bounds.y},
        {primitive.bounds.x,
         static_cast<std::int64_t>(primitive.bounds.y) + primitive.bounds.height},
        {static_cast<std::int64_t>(primitive.bounds.x) + primitive.bounds.width,
         static_cast<std::int64_t>(primitive.bounds.y) + primitive.bounds.height},
    }};
    for (const auto& corner : corners) {
        const auto dx = corner.first - center_x;
        const auto dy = corner.second - center_y;
        if (dx * dx + dy * dy > radius * radius) {
            return false;
        }
    }
    return true;
}

bool requires_hold(UiAction action) {
    return action == UiAction::confirm_critical_alert ||
           action == UiAction::confirm_archive_start ||
           action == UiAction::acknowledge_inbound_alert;
}

bool known_kind(UiRenderPrimitiveKind kind) {
    return kind == UiRenderPrimitiveKind::panel ||
           kind == UiRenderPrimitiveKind::text ||
           kind == UiRenderPrimitiveKind::indicator ||
           kind == UiRenderPrimitiveKind::metric ||
           kind == UiRenderPrimitiveKind::action;
}

bool known_style(UiRenderStyle style) {
    return style == UiRenderStyle::surface || style == UiRenderStyle::heading ||
           style == UiRenderStyle::body || style == UiRenderStyle::muted ||
           style == UiRenderStyle::information || style == UiRenderStyle::warning ||
           style == UiRenderStyle::critical || style == UiRenderStyle::positive;
}

bool known_token(UiTextToken token) {
    const auto value = static_cast<std::uint16_t>(token);
    return token == UiTextToken::none || token == UiTextToken::peers ||
           token == UiTextToken::unread || token == UiTextToken::archive_queue ||
           (value >= 100U && value <= 111U) ||
           (value >= 200U && value <= 227U) ||
           (value >= 301U && value <= 332U) ||
           (value >= 400U && value <= 404U) ||
           (value >= 410U && value <= 414U) ||
           (value >= 420U && value <= 424U);
}

bool valid_owned_text(const UiOwnedText& text) {
    if (text.byte_count == 0 || text.byte_count > kMaxUiOwnedTextBytes ||
        text.bytes[text.byte_count] != '\0' ||
        (text.truncated && text.unavailable)) {
        return false;
    }
    for (std::size_t index = 0; index < text.bytes.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text.bytes[index]);
        if (index < text.byte_count) {
            if (byte < 0x20U || byte > 0x7EU) return false;
        } else if (byte != 0U) {
            return false;
        }
    }
    return true;
}

bool valid_delivery(UiMessageDeliveryState value) {
    return value == UiMessageDeliveryState::none ||
           value == UiMessageDeliveryState::queued ||
           value == UiMessageDeliveryState::bridge_accepted ||
           value == UiMessageDeliveryState::bridge_observed ||
           value == UiMessageDeliveryState::bridge_acknowledgement_observed ||
           value == UiMessageDeliveryState::failed;
}

bool valid_message_kind(UiMessageKind value) {
    return value == UiMessageKind::none || value == UiMessageKind::chat ||
           value == UiMessageKind::quick_status || value == UiMessageKind::alert ||
           value == UiMessageKind::acknowledgement;
}

bool valid_message_priority(UiMessagePriority value) {
    return value == UiMessagePriority::none ||
           value == UiMessagePriority::normal ||
           value == UiMessagePriority::important ||
           value == UiMessagePriority::critical;
}

bool empty_sidecar(const UiPresentationSidecar& sidecar) {
    if (sidecar.owned_text_count != 0 ||
        sidecar.messages.list_kind != UiMessageListKind::none ||
        sidecar.messages.page_index != 0 ||
        sidecar.messages.page_count != 0 ||
        sidecar.messages.row_count != 0 ||
        sidecar.messages.detail_valid ||
        sidecar.messages.detail_delivery != UiMessageDeliveryState::none ||
        sidecar.messages.detail_kind != UiMessageKind::none ||
        sidecar.messages.detail_priority != UiMessagePriority::none ||
        sidecar.messages.detail_unread ||
        sidecar.messages.detail_acknowledge_available ||
        sidecar.messages.compose_template_id != 0) {
        return false;
    }
    for (const auto& text : sidecar.owned_texts) {
        if (text.byte_count != 0 || text.truncated || text.unavailable) return false;
        for (const auto byte : text.bytes) if (byte != '\0') return false;
    }
    for (const auto& row : sidecar.messages.rows) {
        if (row.text_index != kNoUiOwnedText ||
            row.delivery != UiMessageDeliveryState::none ||
            row.kind != UiMessageKind::none ||
            row.priority != UiMessagePriority::none || row.unread) return false;
    }
    return true;
}

bool valid_sidecar_storage(const UiPresentationSidecar& sidecar) {
    if (sidecar.owned_text_count > sidecar.owned_texts.size() ||
        sidecar.messages.row_count > sidecar.messages.rows.size() ||
        sidecar.messages.compose_template_id > 8 ||
        !valid_delivery(sidecar.messages.detail_delivery) ||
        !valid_message_kind(sidecar.messages.detail_kind) ||
        !valid_message_priority(sidecar.messages.detail_priority)) return false;
    for (std::size_t index = 0; index < sidecar.owned_texts.size(); ++index) {
        const auto& text = sidecar.owned_texts[index];
        if (index < sidecar.owned_text_count) {
            if (!valid_owned_text(text)) return false;
        } else {
            if (text.byte_count != 0 || text.truncated || text.unavailable) return false;
            for (const auto byte : text.bytes) if (byte != '\0') return false;
        }
    }
    for (std::size_t index = 0; index < sidecar.messages.rows.size(); ++index) {
        const auto& row = sidecar.messages.rows[index];
        if (index < sidecar.messages.row_count) {
            if (row.text_index != index || !valid_delivery(row.delivery) ||
                !valid_message_kind(row.kind) || row.kind == UiMessageKind::none ||
                !valid_message_priority(row.priority) ||
                row.priority == UiMessagePriority::none) return false;
        } else if (row.text_index != kNoUiOwnedText ||
                   row.delivery != UiMessageDeliveryState::none ||
                   row.kind != UiMessageKind::none ||
                   row.priority != UiMessagePriority::none || row.unread) {
            return false;
        }
    }
    return sidecar.messages.detail_valid ||
           (sidecar.messages.detail_delivery == UiMessageDeliveryState::none &&
            sidecar.messages.detail_kind == UiMessageKind::none &&
            sidecar.messages.detail_priority == UiMessagePriority::none &&
            !sidecar.messages.detail_unread &&
            !sidecar.messages.detail_acknowledge_available);
}

bool valid_presentation(const UiFrame& frame,
                        const UiPresentationSidecar& sidecar) {
    if (!valid_sidecar_storage(sidecar)) return false;
    const auto message_screen = frame.screen == UiScreen::message_center ||
        frame.screen == UiScreen::message_list ||
        frame.screen == UiScreen::message_detail ||
        frame.screen == UiScreen::message_compose ||
        frame.screen == UiScreen::message_compose_confirmation;
    if (!message_screen) return empty_sidecar(sidecar);
    const auto& messages = sidecar.messages;
    if (frame.screen == UiScreen::message_center) return empty_sidecar(sidecar);
    if (frame.screen == UiScreen::message_list) {
        if ((messages.list_kind != UiMessageListKind::inbox &&
             messages.list_kind != UiMessageListKind::outbox) ||
            messages.detail_valid || messages.compose_template_id != 0 ||
            messages.page_count == 0 || messages.page_count > 6 ||
            messages.page_index >= messages.page_count ||
            sidecar.owned_text_count != messages.row_count ||
            (messages.page_index + 1U < messages.page_count &&
             messages.row_count != kMaxUiMessageRows) ||
            (messages.page_count > 1 &&
             messages.page_index + 1U == messages.page_count &&
             messages.row_count == 0)) return false;
        const auto expected_actions = static_cast<std::size_t>(
            messages.row_count + (messages.page_count > 1 ? 1U : 0U) + 1U);
        if (frame.action_count != expected_actions) return false;
        const std::array<UiAction, kMaxUiMessageRows> row_actions{
            UiAction::open_message_row_1, UiAction::open_message_row_2};
        std::size_t action_index = 0;
        for (std::size_t index = 0; index < messages.row_count; ++index) {
            const auto& row = messages.rows[index];
            if (frame.actions[action_index].action != row_actions[index] ||
                !frame.actions[action_index].enabled) return false;
            ++action_index;
            if ((messages.list_kind == UiMessageListKind::inbox &&
                 row.delivery != UiMessageDeliveryState::none) ||
                (messages.list_kind == UiMessageListKind::outbox &&
                 (row.delivery == UiMessageDeliveryState::none || row.unread))) return false;
        }
        if (messages.page_count > 1 &&
            (frame.actions[action_index].action != UiAction::show_next_message_page ||
             !frame.actions[action_index++].enabled)) return false;
        return frame.actions[action_index].action == UiAction::cancel &&
               frame.actions[action_index].enabled;
    }
    if (frame.screen == UiScreen::message_detail) {
        const bool inbox = messages.list_kind == UiMessageListKind::inbox;
        const bool outbox = messages.list_kind == UiMessageListKind::outbox;
        const bool acknowledge = messages.detail_acknowledge_available;
        const bool frame_acknowledge = frame.action_count == 2 &&
            frame.actions[0].action == UiAction::acknowledge_inbound_alert;
        return acknowledge == frame_acknowledge && (inbox || outbox) &&
            messages.detail_valid &&
            sidecar.owned_text_count == 2 && messages.compose_template_id == 0 &&
            !messages.detail_unread && messages.detail_kind != UiMessageKind::none &&
            messages.detail_priority != UiMessagePriority::none &&
            frame.attention == (messages.detail_priority == UiMessagePriority::critical
                ? UiAttention::critical : UiAttention::information) &&
            ((inbox && messages.detail_delivery == UiMessageDeliveryState::none) ||
             (outbox && messages.detail_delivery != UiMessageDeliveryState::none &&
              !acknowledge)) &&
            (!acknowledge || (inbox && messages.detail_kind == UiMessageKind::alert &&
              messages.detail_priority == UiMessagePriority::critical));
    }
    if (frame.screen == UiScreen::message_compose) {
        return sidecar.owned_text_count == 2 &&
               messages.list_kind == UiMessageListKind::none &&
               messages.page_index == 0 && messages.page_count == 0 &&
               messages.row_count == 0 && !messages.detail_valid &&
               messages.compose_template_id == 0;
    }
    return sidecar.owned_text_count == 1 &&
           messages.list_kind == UiMessageListKind::none &&
           messages.page_index == 0 && messages.page_count == 0 &&
           messages.row_count == 0 && !messages.detail_valid &&
           messages.compose_template_id >= 1 &&
           messages.compose_template_id <= 8;
}

}  // namespace

static UiRenderPlanResult build_portable_ui_render_plan(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar,
    UiLogicalDisplayProfile profile) {
    UiRenderPlanResult result{};
    if (profile.width < 320 || profile.height < 320 ||
        profile.minimum_action_extent < 44 ||
        (profile.shape != UiViewportShape::rectangle &&
         profile.shape != UiViewportShape::circle) ||
        (profile.shape == UiViewportShape::circle &&
         profile.width != profile.height)) {
        result.error = UiRenderPlanError::invalid_profile;
        return result;
    }
    const DisplayCapabilities validation_capabilities{
        profile.width, profile.height, 16, 4, true, false, true};
    if (!valid_ui_frame(frame, validation_capabilities) ||
        !valid_presentation(frame, sidecar)) {
        result.error = UiRenderPlanError::invalid_frame;
        return result;
    }

    auto& plan = result.plan;
    plan.frame_revision = frame.revision;
    plan.width = profile.width;
    plan.height = profile.height;
    plan.shape = profile.shape;
    bool capacity_ok = true;
    capacity_ok &= append(plan, {UiRenderPrimitiveKind::panel, rect(profile, 0, 0, 480, 480)});
    capacity_ok &= append(plan, {
        UiRenderPrimitiveKind::text,
        rect(profile, 126, 36, 228, 36),
        token_with_offset(UiTextToken::screen_base,
                          static_cast<std::uint16_t>(frame.screen)),
        UiRenderStyle::heading});

    const bool message_screen =
        frame.screen == UiScreen::message_center ||
        frame.screen == UiScreen::message_list ||
        frame.screen == UiScreen::message_detail ||
        frame.screen == UiScreen::message_compose ||
        frame.screen == UiScreen::message_compose_confirmation;
    const std::array<UiIndicatorState, 3> indicators{
        frame.status.radio, frame.status.position, frame.status.power};
    const std::array<UiTextToken, 3> indicator_bases{
        UiTextToken::radio_indicator_base,
        UiTextToken::position_indicator_base,
        UiTextToken::power_indicator_base};
    for (std::uint16_t index = 0;
         !message_screen && index < indicators.size(); ++index) {
        capacity_ok &= append(plan, {
            UiRenderPrimitiveKind::indicator,
            rect(profile, static_cast<std::uint16_t>(60U + 120U * index), 88, 105, 40),
            token_with_offset(indicator_bases[index],
                              static_cast<std::uint16_t>(indicators[index])),
            indicator_style(indicators[index])});
    }

    if (!message_screen && frame.status.peer_count_valid) {
        capacity_ok &= append(plan, {UiRenderPrimitiveKind::metric, rect(profile, 30, 130, 120, 34),
                      UiTextToken::peers, UiRenderStyle::body, kNoActionSlot,
                      false, false, true, frame.status.peer_count});
    }
    if (!message_screen) {
        capacity_ok &= append(plan, {UiRenderPrimitiveKind::metric, rect(profile, 173, 130, 120, 34),
                      UiTextToken::unread, UiRenderStyle::body, kNoActionSlot,
                      false, false, true, frame.status.unread_messages});
    }
    if (!message_screen && frame.status.archive_queue_count_valid) {
        capacity_ok &= append(plan, {UiRenderPrimitiveKind::metric, rect(profile, 316, 130, 120, 34),
                      UiTextToken::archive_queue, UiRenderStyle::body, kNoActionSlot,
                      false, false, true, frame.status.archive_queue_count});
    }
    if (frame.notice != UiNotice::none) {
        capacity_ok &= append(plan, {
            UiRenderPrimitiveKind::text,
            rect(profile, 42, 176, 382, 64),
            token_with_offset(UiTextToken::notice_base,
                              static_cast<std::uint16_t>(frame.notice)),
            attention_style(frame.attention)});
    }

    if (frame.screen == UiScreen::message_detail) {
        capacity_ok &= append(plan, {
            UiRenderPrimitiveKind::text, rect(profile, 72, 132, 336, 112),
            UiTextToken::none, UiRenderStyle::body, kNoActionSlot,
            false, false, false, 0, 0});
        capacity_ok &= append(plan, {
            UiRenderPrimitiveKind::text, rect(profile, 110, 254, 246, 40),
            UiTextToken::none,
            frame.attention == UiAttention::critical
                ? UiRenderStyle::critical
                : UiRenderStyle::muted,
            kNoActionSlot, false, false, false, 0, 1});
    } else if (frame.screen == UiScreen::message_compose_confirmation) {
        capacity_ok &= append(plan, {
            UiRenderPrimitiveKind::text, rect(profile, 72, 150, 336, 120),
            UiTextToken::none, UiRenderStyle::information, kNoActionSlot,
            false, false, false, 0, 0});
    }

    for (std::uint8_t slot = 0; slot < frame.action_count; ++slot) {
        UiRenderRect bounds{};
        if (frame.screen == UiScreen::message_list) {
            if (slot < sidecar.messages.row_count) {
                bounds = rect(profile, 90,
                              static_cast<std::uint16_t>(150U + slot * 82U),
                              286, 68);
            } else if (frame.action_count - sidecar.messages.row_count == 1) {
                bounds = rect(profile, 110, 350, 246, 68);
            } else {
                const auto nav = static_cast<std::uint16_t>(
                    slot - sidecar.messages.row_count);
                bounds = rect(profile,
                              static_cast<std::uint16_t>(110U + nav * 128U),
                              350, 118, 68);
            }
        } else if (frame.screen == UiScreen::message_compose) {
            if (slot < 2) {
                bounds = rect(profile, 90,
                              static_cast<std::uint16_t>(160U + slot * 84U),
                              286, 68);
            } else {
                bounds = rect(profile,
                              static_cast<std::uint16_t>(110U +
                                  (slot - 2U) * 128U),
                              350, 118, 68);
            }
        } else if (frame.action_count == 1) {
            bounds = rect(profile, 110, 360, 246, 66);
        } else if (frame.action_count == 2) {
            bounds = rect(profile, static_cast<std::uint16_t>(90U + slot * 146U),
                          330, 140, 80);
        } else {
            const auto column = static_cast<std::uint16_t>(slot % 2U);
            const auto row = static_cast<std::uint16_t>(slot / 2U);
            if (row == 0) {
                bounds = rect(profile,
                              static_cast<std::uint16_t>(70U + column * 168U),
                              278, 158, 70);
            } else if (frame.action_count == 3) {
                bounds = rect(profile, 110, 360, 246, 66);
            } else {
                bounds = rect(profile,
                              static_cast<std::uint16_t>(110U + column * 128U),
                              360, 118, 66);
            }
        }
        const bool row_text = frame.screen == UiScreen::message_list &&
                              slot < sidecar.messages.row_count;
        const bool template_text = frame.screen == UiScreen::message_compose &&
                                   slot < 2;
        capacity_ok &= append(plan, {
            UiRenderPrimitiveKind::action,
            bounds,
            row_text || template_text
                ? UiTextToken::none
                : token_with_offset(
                      UiTextToken::action_base,
                      static_cast<std::uint16_t>(frame.actions[slot].action)),
            (row_text && sidecar.messages.rows[slot].priority ==
                             UiMessagePriority::critical) ||
                    frame.attention == UiAttention::critical
                ? UiRenderStyle::critical
                : UiRenderStyle::surface,
            slot,
            frame.actions[slot].enabled,
            requires_hold(frame.actions[slot].action),
            false,
            0,
            row_text || template_text ? slot : kNoUiOwnedText});
    }

    if (!capacity_ok) {
        result.error = UiRenderPlanError::capacity_exceeded;
        return result;
    }
    result.error = UiRenderPlanError::none;
    return result;
}

UiRenderPlanResult make_portable_ui_render_plan(
    const UiFrame& frame,
    UiLogicalDisplayProfile profile) {
    return make_portable_ui_render_plan(frame, UiPresentationSidecar{}, profile);
}

UiRenderPlanResult make_portable_ui_render_plan(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar,
    UiLogicalDisplayProfile profile) {
    auto result = build_portable_ui_render_plan(frame, sidecar, profile);
    if (result.ok()) {
        result.error = validate_portable_ui_render_plan(
            frame, sidecar, result.plan, profile);
    }
    return result;
}

UiRenderPlanError validate_portable_ui_render_plan(
    const UiFrame& frame,
    const UiRenderPlan& plan,
    UiLogicalDisplayProfile profile) {
    return validate_portable_ui_render_plan(
        frame, UiPresentationSidecar{}, plan, profile);
}

UiRenderPlanError validate_portable_ui_render_plan(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar,
    const UiRenderPlan& plan,
    UiLogicalDisplayProfile profile) {
    if (profile.width < 320 || profile.height < 320 ||
        profile.minimum_action_extent < 44 ||
        (profile.shape != UiViewportShape::rectangle &&
         profile.shape != UiViewportShape::circle) ||
        (profile.shape == UiViewportShape::circle &&
         profile.width != profile.height) ||
        plan.width != profile.width || plan.height != profile.height ||
        plan.shape != profile.shape) {
        return UiRenderPlanError::invalid_profile;
    }
    const DisplayCapabilities validation_capabilities{
        profile.width, profile.height, 16, 4, true, false, true};
    if (!valid_ui_frame(frame, validation_capabilities) ||
        !valid_presentation(frame, sidecar) ||
        plan.frame_revision != frame.revision || plan.primitive_count == 0 ||
        plan.primitive_count > plan.primitives.size()) {
        return UiRenderPlanError::invalid_frame;
    }
    std::uint8_t actions = 0;
    for (std::size_t index = 0; index < plan.primitive_count; ++index) {
        const auto& primitive = plan.primitives[index];
        if (!known_kind(primitive.kind) || !known_style(primitive.style) ||
            !known_token(primitive.text) || !in_bounds(primitive, plan) ||
            (primitive.owned_text_index != kNoUiOwnedText &&
             (primitive.text != UiTextToken::none ||
              primitive.owned_text_index >= sidecar.owned_text_count ||
              (primitive.kind != UiRenderPrimitiveKind::text &&
               primitive.kind != UiRenderPrimitiveKind::action))) ||
            (primitive.owned_text_index == kNoUiOwnedText &&
             primitive.text == UiTextToken::none &&
             primitive.kind != UiRenderPrimitiveKind::panel) ||
            (primitive.kind != UiRenderPrimitiveKind::panel &&
             !primitive_inside_circle(primitive, plan))) {
            return UiRenderPlanError::primitive_out_of_bounds;
        }
        if (primitive.kind != UiRenderPrimitiveKind::action) {
            continue;
        }
        if (primitive.action_slot != actions || actions >= frame.action_count ||
            primitive.enabled != frame.actions[actions].enabled ||
            primitive.requires_hold != requires_hold(frame.actions[actions].action) ||
            ((frame.screen == UiScreen::message_list &&
              actions < sidecar.messages.row_count) ||
             (frame.screen == UiScreen::message_compose && actions < 2)
                 ? (primitive.text != UiTextToken::none ||
                    primitive.owned_text_index != actions)
                 : (primitive.text != token_with_offset(
                       UiTextToken::action_base,
                       static_cast<std::uint16_t>(
                           frame.actions[actions].action)) ||
                    primitive.owned_text_index != kNoUiOwnedText)) ||
            primitive.bounds.width < profile.minimum_action_extent ||
            primitive.bounds.height < profile.minimum_action_extent) {
            return UiRenderPlanError::action_mismatch;
        }
        ++actions;
    }
    if (actions != frame.action_count) {
        return UiRenderPlanError::action_mismatch;
    }
    const auto canonical = build_portable_ui_render_plan(frame, sidecar, profile);
    if (!canonical.ok() || canonical.plan.frame_revision != plan.frame_revision ||
        canonical.plan.width != plan.width ||
        canonical.plan.height != plan.height ||
        canonical.plan.shape != plan.shape ||
        canonical.plan.primitive_count != plan.primitive_count) {
        return UiRenderPlanError::invalid_frame;
    }
    for (std::size_t index = 0; index < plan.primitives.size(); ++index) {
        const auto& actual = plan.primitives[index];
        const auto& expected = canonical.plan.primitives[index];
        if (actual.kind != expected.kind ||
            actual.bounds.x != expected.bounds.x ||
            actual.bounds.y != expected.bounds.y ||
            actual.bounds.width != expected.bounds.width ||
            actual.bounds.height != expected.bounds.height ||
            actual.text != expected.text || actual.style != expected.style ||
            actual.action_slot != expected.action_slot ||
            actual.enabled != expected.enabled ||
            actual.requires_hold != expected.requires_hold ||
            actual.numeric_value_valid != expected.numeric_value_valid ||
            actual.numeric_value != expected.numeric_value ||
            actual.owned_text_index != expected.owned_text_index) {
            return UiRenderPlanError::invalid_frame;
        }
    }
    return UiRenderPlanError::none;
}

bool valid_portable_ui_presentation(
    const UiFrame& frame,
    const UiPresentationSidecar& sidecar) {
    return valid_presentation(frame, sidecar);
}

}  // namespace opentrail::ui
