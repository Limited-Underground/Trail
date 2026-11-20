#pragma once

#include <cstdint>
#include "opentrail/companion_semantics.hpp"
#include "opentrail/pairing_completion_journal.hpp"

namespace opentrail::target::heltec_v4_bench {
struct BoardPairingResultProjection {
    companion::CompanionPairingState state;
    companion::CompanionPairingReason reason;
    std::uint16_t peer_short_id;
};

// A cached journal can outlive failed identity/binding validation during start.
// Only a successfully bound attempt may project that journal as current evidence.
inline BoardPairingResultProjection project_board_pairing_result(
    const security_evaluation::PairingCompletionSnapshot& record,
    bool successful_start, bool archive_recovery,
    companion::CompanionPairingState fallback, companion::CompanionPairingReason reason) {
    using State = companion::CompanionPairingState;
    using Reason = companion::CompanionPairingReason;
    using Completion = security_evaluation::PairingCompletionPhase;
    BoardPairingResultProjection result{fallback, reason, 0};
    if (!successful_start || archive_recovery) return result;
    if (record.phase == Completion::peer_confirmed || record.phase == Completion::committed) {
        result.state = record.phase == Completion::peer_confirmed ? State::paired_both : State::paired_local;
        result.reason = Reason::none;
        result.peer_short_id = static_cast<std::uint16_t>(
            (static_cast<unsigned>(record.peer[30]) << 8) | record.peer[31]);
    } else if (record.phase == Completion::confirmed || record.phase == Completion::prepared ||
               record.phase == Completion::completing || record.phase == Completion::uncertain) {
        result.state = State::incomplete;
        result.reason = record.phase == Completion::uncertain ? Reason::storage :
                        reason == Reason::request_ended ? Reason::request_ended : Reason::none;
    }
    return result;
}
}  // namespace opentrail::target::heltec_v4_bench
