#include "opentrail/protected_packet_budget.hpp"

#include <limits>

namespace opentrail::protocol {

ProtectedPacketBudgetResult calculate_protected_packet_budget(
    const ProtectedPacketBudgetRequest& request) {
    if (request.transport_mtu == 0 || request.transport_mtu > 255 ||
        request.authenticated_header_bytes == 0 ||
        request.authentication_tag_bytes == 0 ||
        request.maximum_fragments == 0 ||
        request.maximum_fragments > kMaximumProtectedPacketFragments) {
        return {};
    }

    ProtectedPacketBudgetResult result{};
    std::size_t remaining_mtu = request.transport_mtu;
    const std::size_t overhead_parts[]{
        request.authenticated_header_bytes,
        request.authentication_tag_bytes,
        request.source_authentication_bytes,
        request.forwarding_wrapper_bytes,
    };
    for (const auto part : overhead_parts) {
        if (part > remaining_mtu) {
            return {};
        }
        remaining_mtu -= part;
        result.overhead_bytes += part;
    }
    if (result.overhead_bytes >= request.transport_mtu) {
        result.error = ProtectedPacketBudgetError::no_plaintext_capacity;
        return result;
    }
    result.maximum_plaintext_per_frame =
        request.transport_mtu - result.overhead_bytes;
    result.fragment_count = request.plaintext_bytes == 0
        ? 1
        : request.plaintext_bytes / result.maximum_plaintext_per_frame +
            (request.plaintext_bytes % result.maximum_plaintext_per_frame == 0
                 ? 0
                 : 1);
    if (result.fragment_count > request.maximum_fragments) {
        result.error = ProtectedPacketBudgetError::fragment_limit_exceeded;
        result.fragment_count = 0;
        return result;
    }
    if (request.plaintext_bytes >
        std::numeric_limits<std::uint64_t>::max() -
            result.fragment_count * result.overhead_bytes) {
        result.error = ProtectedPacketBudgetError::invalid_request;
        result.fragment_count = 0;
        return result;
    }

    result.total_frame_bytes = request.plaintext_bytes +
        result.fragment_count * result.overhead_bytes;
    std::size_t remaining = request.plaintext_bytes;
    for (std::size_t index = 0; index < result.fragment_count; ++index) {
        const auto chunk = remaining > result.maximum_plaintext_per_frame
            ? result.maximum_plaintext_per_frame
            : remaining;
        const auto frame_bytes = result.overhead_bytes + chunk;
        const auto airtime = radio::calculate_lora_airtime(
            request.airtime,
            frame_bytes);
        if (!airtime.calculated() ||
            std::numeric_limits<std::uint64_t>::max() -
                    result.total_airtime_us <
                airtime.airtime_us) {
            result.error = ProtectedPacketBudgetError::airtime_failure;
            result.fragment_count = 0;
            result.final_frame_bytes = 0;
            result.total_frame_bytes = 0;
            result.total_airtime_us = 0;
            return result;
        }
        result.total_airtime_us += airtime.airtime_us;
        result.final_frame_bytes = frame_bytes;
        remaining -= chunk;
    }
    result.error = ProtectedPacketBudgetError::none;
    return result;
}

}  // namespace opentrail::protocol
