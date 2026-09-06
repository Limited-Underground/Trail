#pragma once

#include <cstdint>

#include "heltec_startup_display.hpp"
#include "opentrail/oled_presentation.hpp"

namespace opentrail::target::heltec_v4_bench {

/** Maps only existing target observations; never derives authority from pixels. */
class HeltecOledPresentation {
public:
    // Logo and ephemeral pairing remain separate panel-port paths. Calling this
    // mapper with logo or an invalid frame fails closed. Device-confirmed name
    // and clock may be supplied; region remains required until separately bound.
    [[nodiscard]] ui::oled_presentation::Frame present(
        const StartupDisplayView& view, std::uint64_t now_ms,
        std::string_view name = {}, time::OledClockReading clock = {});

private:
    ui::oled_presentation::PresentationOwner owner_{};
};

}  // namespace opentrail::target::heltec_v4_bench
