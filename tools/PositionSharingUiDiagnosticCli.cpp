#include <cstdlib>
#include <iostream>
#include <string_view>

#include "opentrail/position_sharing_ui_diagnostics.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: position_sharing_ui_diagnostic_cli "
                     "OTPD0=XXXXXXXX\n";
        return EXIT_FAILURE;
    }

    const auto parsed =
        opentrail::diagnostics::parse_position_sharing_ui_diagnostic_message(
            std::string_view{argv[1]});
    if (!parsed.parsed()) {
        std::cerr << "OTPD0 decode failed: "
                  << opentrail::diagnostics::
                         position_sharing_ui_diagnostic_error_name(
                             parsed.error)
                  << '\n';
        return EXIT_FAILURE;
    }

    const auto& diagnostic = parsed.diagnostic;
    using namespace opentrail::diagnostics;
    std::cout
        << "event="
        << position_sharing_ui_diagnostic_event_name(diagnostic.event)
        << " outcome="
        << position_sharing_ui_diagnostic_outcome_name(diagnostic.outcome)
        << " notice="
        << position_sharing_ui_diagnostic_notice_name(diagnostic.notice)
        << " reason="
        << position_sharing_ui_diagnostic_reason_name(diagnostic.reason)
        << " frame_presented=" << diagnostic.frame_presented
        << " state_changed=" << diagnostic.state_changed
        << " sharing_contained=" << diagnostic.sharing_contained
        << " sensitive_detail_redacted="
        << diagnostic.sensitive_detail_redacted << '\n';
    return EXIT_SUCCESS;
}
