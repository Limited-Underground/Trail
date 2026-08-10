#include "opentrail/portable_client_composition.hpp"

namespace opentrail::targets {
namespace {

void add_issue(PortableClientCompositionReview& review,
               PortableClientCompositionIssue issue) {
    review.issue_mask |= static_cast<std::uint32_t>(issue);
}

}  // namespace

PortableClientComposition::PortableClientComposition(
    PortableClientTargetBindings bindings,
    PortableClientTargetPolicy policy)
    : bindings_(bindings), policy_(policy) {}

PortableClientCompositionReview PortableClientComposition::review() const {
    PortableClientCompositionReview result{};

    if (bindings_.radio == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_radio);
    }
    if (bindings_.gps == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_gps);
    }
    if (bindings_.log_sink == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_log_sink);
    }
    if (bindings_.storage == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_storage);
    }
    if (bindings_.duplicate_checkpoint_storage == nullptr) {
        add_issue(
            result,
            PortableClientCompositionIssue::missing_duplicate_checkpoint_storage);
    }
    if (bindings_.secure_random == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_secure_random);
    }
    if (bindings_.monotonic_clock == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_monotonic_clock);
    }
    if (bindings_.power == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_power);
    }
    if (bindings_.display == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_display);
    }
    if (bindings_.input == nullptr) {
        add_issue(result, PortableClientCompositionIssue::missing_input);
    }

    const bool valid_radio_requirement =
        policy_.minimum_radio_mtu_bytes != 0 &&
        policy_.minimum_radio_mtu_bytes <= radio::kMaximumFrameBytes;
    if (!valid_radio_requirement) {
        add_issue(result,
                  PortableClientCompositionIssue::invalid_radio_requirement);
    }
    if (bindings_.radio != nullptr) {
        result.observed_radio_mtu_bytes = bindings_.radio->mtu();
        if (result.observed_radio_mtu_bytes == 0 ||
            result.observed_radio_mtu_bytes > radio::kMaximumFrameBytes) {
            add_issue(result,
                      PortableClientCompositionIssue::invalid_radio_capability);
        } else if (valid_radio_requirement &&
                   result.observed_radio_mtu_bytes <
                       policy_.minimum_radio_mtu_bytes) {
            add_issue(result,
                      PortableClientCompositionIssue::radio_mtu_too_small);
        }
    }

    if (!power::valid_power_policy(policy_.power)) {
        add_issue(result, PortableClientCompositionIssue::invalid_power_policy);
    }

    const bool valid_display =
        ui::valid_display_capabilities(policy_.display);
    if (!valid_display) {
        add_issue(result,
                  PortableClientCompositionIssue::invalid_display_capabilities);
    }

    const bool valid_ui_requirement =
        policy_.minimum_action_slots != 0 &&
        policy_.minimum_action_slots <= ui::kMaxUiActions &&
        (!policy_.critical_confirmation_required ||
         policy_.minimum_action_slots >= 2);
    if (!valid_ui_requirement) {
        add_issue(result,
                  PortableClientCompositionIssue::invalid_ui_requirement);
    } else if (valid_display &&
               policy_.display.max_action_slots <
                   policy_.minimum_action_slots) {
        add_issue(result,
                  PortableClientCompositionIssue::insufficient_ui_capability);
    }

    if (valid_display && policy_.critical_confirmation_required &&
        !policy_.display.supports_hold) {
        add_issue(result,
                  PortableClientCompositionIssue::critical_hold_unsupported);
    }

    return result;
}

}  // namespace opentrail::targets
