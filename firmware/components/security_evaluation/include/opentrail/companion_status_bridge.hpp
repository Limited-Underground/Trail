#pragma once
// Evaluation-only semantic adapter, not a production radio/BLE wire selection.
// Invoke after protected companion authorization and request deduplication.
// Success creates an encrypted record, not queued/TX/delivery evidence. The
// caller owns bounded transport admission and uncertain outcome handling.
#include "opentrail/enrolled_peer_endpoint.hpp"
#include "opentrail/companion_semantics.hpp"

namespace opentrail::security_evaluation {
class CompanionStatusBridge final {
public:
    explicit CompanionStatusBridge(EnrolledPeerEndpoint& endpoint) : endpoint_(endpoint) {}
    CompanionStatusBridge(const CompanionStatusBridge&) = delete;
    CompanionStatusBridge& operator=(const CompanionStatusBridge&) = delete;

    static bool supported(const companion::CompanionActionRequest& request) {
        return request.kind == companion::CompanionActionKind::quick_status &&
               request.critical_alert_id == 0 && known(request.quick_status);
    }
    bool encrypt(const companion::CompanionActionRequest& request, EvaluationRecord& output) {
        // Reject unsupported semantics before touching cryptographic/counter state.
        const auto copy = request;
        return supported(copy) &&
            endpoint_.send_status(static_cast<std::uint8_t>(copy.quick_status), output);
    }
    bool decrypt(const EvaluationRecord& input, protocol::QuickStatusPayload& output) {
        std::uint8_t status = 0;
        if (!endpoint_.receive_status(input, status)) return false;
        const auto kind = static_cast<protocol::QuickStatusKind>(status);
        // Evaluation statuses 5..8 have no companion meaning. Their authenticated
        // counters remain consumed, but no invented UI meaning is published.
        if (!known(kind)) return false;
        output = {kind};
        return true;
    }
private:
    static bool known(protocol::QuickStatusKind kind) {
        return kind == protocol::QuickStatusKind::ok ||
               kind == protocol::QuickStatusKind::need_assistance ||
               kind == protocol::QuickStatusKind::anyone_online ||
               kind == protocol::QuickStatusKind::available_to_help;
    }
    EnrolledPeerEndpoint& endpoint_;
};
} // namespace opentrail::security_evaluation
