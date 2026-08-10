#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/duplicate_checkpoint_store.hpp"
#include "opentrail/gps_provider.hpp"
#include "opentrail/local_interface.hpp"
#include "opentrail/logger.hpp"
#include "opentrail/monotonic_clock.hpp"
#include "opentrail/persistent_storage.hpp"
#include "opentrail/power_state.hpp"
#include "opentrail/radio_transport.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::targets {

// These are the target-facing endpoints required by the first self-contained
// portable-client shape. The composition does not own them. A concrete board
// target must keep every adapter alive for at least as long as the application.
struct PortableClientTargetBindings {
    radio::RadioTransport* radio{nullptr};
    location::GpsProvider* gps{nullptr};
    diagnostics::LogSink* log_sink{nullptr};
    persistence::PersistentStorage* storage{nullptr};
    delivery::DuplicateCheckpointStorage* duplicate_checkpoint_storage{nullptr};
    security::SecureRandomSource* secure_random{nullptr};
    time::MonotonicCounterSource* monotonic_clock{nullptr};
    power::PowerStatusSource* power{nullptr};
    ui::DisplaySink* display{nullptr};
    ui::LocalInputSource* input{nullptr};
};

// Product policy is explicit and target-independent. Zero/default values are
// intentionally invalid so an unfinished board binding cannot look ready.
struct PortableClientTargetPolicy {
    std::size_t minimum_radio_mtu_bytes{0};
    power::PowerPolicy power{};
    ui::DisplayCapabilities display{};
    std::uint8_t minimum_action_slots{0};
    bool critical_confirmation_required{false};
};

enum class PortableClientCompositionIssue : std::uint32_t {
    none = 0,
    missing_radio = 1U << 0U,
    missing_gps = 1U << 1U,
    missing_log_sink = 1U << 2U,
    missing_storage = 1U << 3U,
    missing_duplicate_checkpoint_storage = 1U << 4U,
    missing_secure_random = 1U << 5U,
    missing_monotonic_clock = 1U << 6U,
    missing_power = 1U << 7U,
    missing_display = 1U << 8U,
    missing_input = 1U << 9U,
    invalid_radio_requirement = 1U << 10U,
    invalid_radio_capability = 1U << 11U,
    radio_mtu_too_small = 1U << 12U,
    invalid_power_policy = 1U << 13U,
    invalid_display_capabilities = 1U << 14U,
    invalid_ui_requirement = 1U << 15U,
    insufficient_ui_capability = 1U << 16U,
    critical_hold_unsupported = 1U << 17U,
};

struct PortableClientCompositionReview {
    std::uint32_t issue_mask{0};
    std::size_t observed_radio_mtu_bytes{0};

    [[nodiscard]] bool ready() const {
        return issue_mask == 0;
    }

    [[nodiscard]] bool has(PortableClientCompositionIssue issue) const {
        const auto value = static_cast<std::uint32_t>(issue);
        return value != 0 && (issue_mask & value) == value;
    }
};

// Hardware-independent composition preflight. It aggregates every structural
// issue in one result and performs no storage, entropy, clock, GPS, power,
// display, input, or log operation. Radio MTU is the only adapter capability
// queried. Runtime readiness remains a later boot/service concern.
class PortableClientComposition {
public:
    PortableClientComposition(PortableClientTargetBindings bindings,
                              PortableClientTargetPolicy policy);

    [[nodiscard]] PortableClientCompositionReview review() const;

private:
    PortableClientTargetBindings bindings_{};
    PortableClientTargetPolicy policy_{};
};

}  // namespace opentrail::targets
