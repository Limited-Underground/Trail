#include "companion_connection_diagnostics.hpp"

#if OPENTRAIL_CONNECTION_DIAGNOSTICS
#include "esp_timer.h"
namespace opentrail::target::heltec_v4_bench::connection_diagnostics {
Events events;
__attribute__((noinline)) void record(
    Kind kind, std::uint32_t first, std::uint32_t second) noexcept {
    // Low 32 bits only: collector preserves raw boot/event order and never
    // infers a duration or boot identity across wrap/reset discontinuities.
    events.push({kind, static_cast<std::uint32_t>(esp_timer_get_time() / 1000),
                 first, second});
}
}  // namespace opentrail::target::heltec_v4_bench::connection_diagnostics
#endif
