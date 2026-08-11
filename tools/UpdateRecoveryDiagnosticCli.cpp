#include <cstdlib>
#include <iostream>
#include <string_view>

#include "opentrail/update_recovery_diagnostics.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: update_recovery_diagnostic_cli "
                     "OTRD0=XXXXXXXX\n";
        return EXIT_FAILURE;
    }

    const auto parsed =
        opentrail::diagnostics::parse_update_recovery_diagnostic_message(
            std::string_view{argv[1]});
    if (!parsed.parsed()) {
        std::cerr << "OTRD0 decode failed: "
                  << opentrail::diagnostics::
                         update_recovery_diagnostic_error_name(parsed.error)
                  << '\n';
        return EXIT_FAILURE;
    }

    const auto& diagnostic = parsed.diagnostic;
    using namespace opentrail::diagnostics;
    std::cout
        << "operation="
        << update_recovery_diagnostic_operation_name(diagnostic.operation)
        << " state="
        << update_recovery_diagnostic_state_name(diagnostic.state)
        << " reason="
        << update_recovery_diagnostic_reason_name(diagnostic.reason)
        << " action="
        << update_recovery_diagnostic_action_name(diagnostic.action)
        << " operation_succeeded=" << diagnostic.operation_succeeded
        << " normal_operation_blocked="
        << diagnostic.normal_operation_blocked
        << " attention_required=" << diagnostic.attention_required
        << " reboot_required=" << diagnostic.reboot_required
        << " confirmation_required=" << diagnostic.confirmation_required
        << " cleanup_required=" << diagnostic.cleanup_required
        << " sensitive_detail_redacted="
        << diagnostic.sensitive_detail_redacted << '\n';
    return EXIT_SUCCESS;
}
