#include "opentrail/critical_alert_ack_responder.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

constexpr std::uint64_t kMaximumObservedAlertAgeMs = 86400000ULL;

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

struct Mapping {
    CriticalAlertAckResponseError error{
        CriticalAlertAckResponseError::inconsistent_decision};
    AlertAckDisposition disposition{AlertAckDisposition::rejected};
    AlertAckReason reason{AlertAckReason::internal_error};
};

Mapping map_decision(
    const AlertIngressResult& decision,
    const AlertIngressContext& context) {
    if (decision.error != AlertIngressError::codec_rejected &&
        decision.codec_error != AlertCodecError::none) {
        return {};
    }
    if (decision.disposition == AlertIngressDisposition::accepted ||
        decision.disposition == AlertIngressDisposition::duplicate) {
        if (decision.error != AlertIngressError::none ||
            !context.authorized_to_publish) {
            return {};
        }
        return {
            CriticalAlertAckResponseError::none,
            AlertAckDisposition::accepted,
            AlertAckReason::none};
    }
    if (decision.disposition != AlertIngressDisposition::rejected ||
        decision.error == AlertIngressError::none) {
        return {};
    }
    switch (decision.error) {
        case AlertIngressError::unauthorized:
            if (context.authorized_to_publish) {
                return {};
            }
            return {
                CriticalAlertAckResponseError::none,
                AlertAckDisposition::rejected,
                AlertAckReason::unauthorized};
        case AlertIngressError::stale:
        case AlertIngressError::future_timestamp:
            if (!context.authorized_to_publish) {
                return {};
            }
            return {
                CriticalAlertAckResponseError::none,
                AlertAckDisposition::rejected,
                AlertAckReason::stale};
        case AlertIngressError::duplicate_conflict:
            if (!context.authorized_to_publish) {
                return {};
            }
            return {
                CriticalAlertAckResponseError::none,
                AlertAckDisposition::rejected,
                AlertAckReason::conflict};
        case AlertIngressError::rate_limited:
        case AlertIngressError::producer_capacity_exhausted:
            if (!context.authorized_to_publish) {
                return {};
            }
            return {
                CriticalAlertAckResponseError::none,
                AlertAckDisposition::rejected,
                AlertAckReason::rate_limited};
        case AlertIngressError::codec_rejected:
        case AlertIngressError::unauthenticated:
        case AlertIngressError::producer_mismatch:
        case AlertIngressError::monotonic_time_rollback:
            return {CriticalAlertAckResponseError::response_suppressed};
        case AlertIngressError::none:
            break;
    }
    return {};
}

}  // namespace

CriticalAlertAckResponseError CriticalAlertAckResponder::start(
    const CriticalAlertAckResponderConfiguration& configuration) {
    if (status_.running) {
        return CriticalAlertAckResponseError::invalid_state;
    }
    if (configuration.consumer_id == 0 ||
        configuration.consumer_boot_session_id == 0) {
        return CriticalAlertAckResponseError::invalid_configuration;
    }
    configuration_ = configuration;
    status_ = {};
    status_.running = true;
    status_.next_ack_sequence = configuration.initial_ack_sequence;
    return CriticalAlertAckResponseError::none;
}

void CriticalAlertAckResponder::stop() {
    status_.running = false;
}

CriticalAlertAckResponse CriticalAlertAckResponder::respond(
    const AlertIngressResult& decision,
    const AlertIngressContext& context,
    std::uint64_t response_monotonic_ms) {
    if (!status_.running) {
        return {CriticalAlertAckResponseError::invalid_state};
    }
    if (!context.authenticated) {
        saturating_increment(status_.suppressed);
        return {CriticalAlertAckResponseError::response_suppressed};
    }
    if (decision.alert.producer_id == 0 || decision.alert.event_id == 0 ||
        decision.alert.condition_id == 0 ||
        context.authenticated_producer_id != decision.alert.producer_id) {
        saturating_increment(status_.suppressed);
        return {CriticalAlertAckResponseError::producer_mismatch};
    }
    if (validate_critical_alert(decision.alert) != AlertCodecError::none) {
        saturating_increment(status_.suppressed);
        return {CriticalAlertAckResponseError::response_suppressed};
    }
    if (response_monotonic_ms < context.receive_monotonic_ms) {
        saturating_increment(status_.failures);
        return {CriticalAlertAckResponseError::clock_regression};
    }
    const auto mapping = map_decision(decision, context);
    if (mapping.error != CriticalAlertAckResponseError::none) {
        if (mapping.error ==
            CriticalAlertAckResponseError::response_suppressed) {
            saturating_increment(status_.suppressed);
        } else {
            saturating_increment(status_.failures);
        }
        return {mapping.error};
    }
    const auto elapsed = response_monotonic_ms - context.receive_monotonic_ms;
    if (elapsed > kMaximumObservedAlertAgeMs ||
        decision.alert.age_ms > kMaximumObservedAlertAgeMs - elapsed) {
        saturating_increment(status_.failures);
        return {CriticalAlertAckResponseError::observed_age_out_of_range};
    }

    CriticalAlertAck acknowledgement{};
    acknowledgement.disposition = mapping.disposition;
    acknowledgement.reason = mapping.reason;
    acknowledgement.state = decision.alert.state;
    acknowledgement.consumer_id = configuration_.consumer_id;
    acknowledgement.producer_id = decision.alert.producer_id;
    acknowledgement.event_id = decision.alert.event_id;
    acknowledgement.condition_id = decision.alert.condition_id;
    acknowledgement.consumer_boot_session_id =
        configuration_.consumer_boot_session_id;
    acknowledgement.ack_sequence = status_.next_ack_sequence;
    acknowledgement.observed_alert_age_ms = static_cast<std::uint32_t>(
        decision.alert.age_ms + elapsed);

    CriticalAlertAckResponse response{};
    response.acknowledgement = acknowledgement;
    const auto encoded = encode_critical_alert_ack(
        acknowledgement, response.frame);
    if (!encoded.encoded()) {
        response.error = CriticalAlertAckResponseError::codec_failure;
        response.codec_error = encoded.error;
        response.frame = {};
        saturating_increment(status_.failures);
        return response;
    }
    response.error = CriticalAlertAckResponseError::none;
    ++status_.next_ack_sequence;
    saturating_increment(status_.produced);
    if (acknowledgement.disposition == AlertAckDisposition::accepted) {
        saturating_increment(status_.accepted);
    } else {
        saturating_increment(status_.rejected);
    }
    return response;
}

CriticalAlertAckResponderStatus CriticalAlertAckResponder::status() const {
    return status_;
}

}  // namespace opentrail::integration
