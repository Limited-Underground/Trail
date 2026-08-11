#include <cstdlib>
#include <iostream>
#include <string_view>

#include "opentrail/position_sharing_ui_diagnostics.hpp"
#include "opentrail/update_recovery_diagnostics.hpp"

namespace {

using namespace opentrail::diagnostics;

int decode_position_ui(std::string_view message) {
    const auto parsed =
        parse_position_sharing_ui_diagnostic_message(message);
    if (!parsed.parsed()) {
        std::cerr << "position_ui decode failed: "
                  << position_sharing_ui_diagnostic_error_name(parsed.error)
                  << '\n';
        return EXIT_FAILURE;
    }

    const auto& diagnostic = parsed.diagnostic;
    std::cout
        << "record=position_ui event="
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

int decode_update_recovery(std::string_view message) {
    const auto parsed =
        parse_update_recovery_diagnostic_message(message);
    if (!parsed.parsed()) {
        std::cerr << "update_recovery decode failed: "
                  << update_recovery_diagnostic_error_name(parsed.error)
                  << '\n';
        return EXIT_FAILURE;
    }

    const auto& diagnostic = parsed.diagnostic;
    std::cout
        << "record=update_recovery operation="
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

bool has_prefix(std::string_view message, std::string_view prefix) {
    return message.size() >= prefix.size() &&
           message.substr(0, prefix.size()) == prefix;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: opentrail_diagnostic_cli "
                     "OTPD0=XXXXXXXX|OTRD0=XXXXXXXX\n";
        return EXIT_FAILURE;
    }

    const std::string_view message{argv[1]};
    if (has_prefix(message, "OTPD0=")) {
        return decode_position_ui(message);
    }
    if (has_prefix(message, "OTRD0=")) {
        return decode_update_recovery(message);
    }

    std::cerr << "diagnostic decode failed: unsupported_record\n";
    return EXIT_FAILURE;
}
