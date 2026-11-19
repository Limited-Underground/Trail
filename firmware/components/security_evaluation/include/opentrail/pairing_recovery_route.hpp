#pragma once
// Product and host harness share this routing boundary. A code-matched
// challenge only selects an already signed historical negative decision;
// the session authenticates the subsequent fresh peer response.
#include <cstring>
#include "opentrail/enrollment_candidate_session.hpp"
#include "opentrail/pairing_frame.hpp"

namespace opentrail::security_evaluation {
enum class PairingArchiveRoute { current, ignored, selected, failed };

template<class Restart>
PairingArchiveRoute route_pairing_archive_challenge(
    EnrollmentCandidateSession& session, const protocol::pairing::Frame& frame, Restart restart) {
    if (frame.type != protocol::pairing::FrameType::recovery_challenge)
        return PairingArchiveRoute::current;
    if (!frame.payload || frame.size != protocol::pairing::payload_bytes(frame.type))
        return PairingArchiveRoute::ignored;
    InvitationKey transaction{};
    std::memcpy(transaction.data(), frame.payload, transaction.size());
    if (transaction == session.pairing_recovery_transaction()) return PairingArchiveRoute::current;
    if (!session.select_pairing_archive(transaction))
        return session.failed() ? PairingArchiveRoute::failed : PairingArchiveRoute::ignored;
    return restart() ? PairingArchiveRoute::selected : PairingArchiveRoute::failed;
}
}  // namespace opentrail::security_evaluation
