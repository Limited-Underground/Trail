#pragma once

#include <cstdint>

#include "opentrail/compact_status_footer.hpp"
#include "opentrail/companion_ble_runtime_owner.hpp"
#include "opentrail/power_state.hpp"

namespace opentrail::ui::compact_status_footer::adapters {

struct Composition {
    Snapshot snapshot{};
    Fields fields{};
    Page page{};
};

// Reconstructs the original observation time from the assessment's age and
// the caller's explicit monotonic assessment time. Unsafe subtraction fails
// closed instead of silently refreshing the observation.
[[nodiscard]] Metric battery_metric_from_power(
    const power::PowerAssessment& assessment,
    std::uint64_t assessment_now_ms);

// A terminal error always wins over phase. Unknown values fail closed.
[[nodiscard]] BleCode ble_code_from_runtime(
    const companion::CompanionBleRuntimeStatus& status);

// GPS remains unavailable and the activity field remains blank. This adapter
// owns no source, clock, identifier, logger, transport, or persistent state.
[[nodiscard]] Composition compose(
    const power::PowerAssessment& power_assessment,
    const companion::CompanionBleRuntimeStatus& ble_status,
    std::uint64_t battery_fresh_for_ms,
    std::uint64_t assessment_now_ms,
    std::uint64_t render_now_ms);

}  // namespace opentrail::ui::compact_status_footer::adapters
