#pragma once
// OT233 host-only experimental packet composition. The trusted owner supplies a
// begun endpoint and an exclusive, fresh transport lease. Routing IDs and CRC
// are not peer authentication; signed provisioning and Noise remain authoritative.
// One serialized caller owns the endpoint and adapter; reentry is refused.
// No retries, fragmentation, confirmation, traffic or membership capability.
// RadioTransport cannot withdraw already queued public handshake bytes on close.
#include <algorithm>
#include "opentrail/independent_handshake_endpoint.hpp"
#include "opentrail/packet_codec.hpp"

namespace opentrail::security_evaluation {
struct HandshakeTransportConfig {
    std::uint32_t local_node_id{0}, peer_node_id{0}, network_id{0};
};
enum class HandshakeTransportPoll { waiting, frame, refused };

class IndependentHandshakeTransport final {
public:
    static constexpr std::size_t maximum_packet_bytes = protocol::kPacketOverheadBytes + 4 + 128;
    IndependentHandshakeTransport(IndependentHandshakeEndpoint& endpoint,
                                  radio::RadioTransport& transport,
                                  HandshakeTransportConfig config)
        : endpoint_(endpoint), transport_(transport), config_(config) {}
    ~IndependentHandshakeTransport() { (void)close(); }
    IndependentHandshakeTransport(const IndependentHandshakeTransport&) = delete;
    IndependentHandshakeTransport& operator=(const IndependentHandshakeTransport&) = delete;

    // now_ms schedules the transport only. Endpoint authority supplies the local
    // signed invitation clock; receive metadata never supplies freshness.
    bool send(std::uint64_t now_ms) {
        Buffers buffers;
        return operation(now_ms, [&] {
            if (!check_mtu() || !endpoint_.next_send(buffers.frame) || !fresh()) return false;
            const auto& frame = buffers.frame;
            buffers.payload[0] = frame.version;
            buffers.payload[1] = frame.step;
            buffers.payload[2] = static_cast<std::uint8_t>(frame.payload_bytes);
            buffers.payload[3] = static_cast<std::uint8_t>(frame.payload_bytes >> 8);
            std::copy_n(frame.payload.data(), frame.payload_bytes, buffers.payload.data() + 4);
            const protocol::PacketView packet{
                {protocol::kExperimentalPacketVersion, protocol::PacketType::experimental_probe,
                 0, config_.local_node_id, config_.network_id, frame.step},
                {buffers.payload.data(), 4U + frame.payload_bytes}};
            const auto encoded = protocol::encode_packet(packet, {buffers.wire.data(), buffers.wire.size()});
            if (!encoded.encoded() || encoded.encoded_bytes > maximum_packet_bytes || !fresh()) return false;
            const auto sent = transport_.send({buffers.wire.data(), encoded.encoded_bytes}, now_ms);
            return fresh() && sent.accepted() && sent.accepted_bytes == encoded.encoded_bytes;
        });
    }

    // Advances cooperative transmission and retrieves at most one complete frame.
    // Loss expires through endpoint.poll(); duplicate and out-of-order frames are
    // deliberately delivered to the endpoint's strict one-use state machine.
    HandshakeTransportPoll poll(std::uint64_t now_ms) {
        Buffers buffers;
        auto result = HandshakeTransportPoll::waiting;
        const bool accepted = operation(now_ms, [&] {
            if (!check_mtu() || !fresh()) return false;
            transport_.service(now_ms);
            if (!fresh()) return false;
            const auto received = transport_.receive({buffers.wire.data(), buffers.wire.size()});
            if (!fresh()) return false;
            if (received.error == radio::RadioError::no_data)
                return received.received_bytes == 0;
            if (!received.has_frame() || received.received_bytes == 0 ||
                received.received_bytes > maximum_packet_bytes) return false;
            const auto decoded = protocol::decode_packet({buffers.wire.data(), received.received_bytes});
            if (!decoded.decoded()) return false;
            const auto& packet = decoded.packet;
            if (packet.header.type != protocol::PacketType::experimental_probe ||
                packet.header.source_node_id != config_.peer_node_id ||
                packet.header.network_id != config_.network_id ||
                packet.header.message_id < 1 || packet.header.message_id > 3 ||
                packet.payload.size < 5 || packet.payload.size > buffers.payload.size()) return false;
            const auto* p = packet.payload.data;
            const auto bytes = static_cast<std::uint16_t>(p[2] | (static_cast<std::uint16_t>(p[3]) << 8));
            if (p[0] != 1 || p[1] != packet.header.message_id || bytes == 0 || bytes > 128 ||
                packet.payload.size != 4U + bytes) return false;
            buffers.frame.version = p[0];
            buffers.frame.step = p[1];
            buffers.frame.payload_bytes = bytes;
            // Value initialization leaves every unused payload byte canonical zero.
            std::copy_n(p + 4, bytes, buffers.frame.payload.begin());
            if (!endpoint_.receive(buffers.frame) || !fresh()) return false;
            result = HandshakeTransportPoll::frame;
            return true;
        });
        return accepted ? result : HandshakeTransportPoll::refused;
    }

    bool close() {
        if (busy_) { revoked_ = true; return false; }
        if (closed_) return cleanup_ok_ && !revoked_;
        busy_ = true;
        cleanup();
        busy_ = false;
        return cleanup_ok_ && !revoked_;
    }

private:
    struct Buffers {
        HandshakeFrame frame{};
        std::array<std::uint8_t, 132> payload{};
        std::array<std::uint8_t, radio::kMaximumFrameBytes> wire{};
        ~Buffers() {
            sodium_memzero(&frame, sizeof(frame));
            sodium_memzero(payload.data(), payload.size());
            sodium_memzero(wire.data(), wire.size());
        }
    };
    bool fresh() { return !revoked_ && endpoint_.poll() && !revoked_; }
    bool check_mtu() {
        if (!fresh()) return false;
        const auto mtu = transport_.mtu();
        return fresh() && mtu >= maximum_packet_bytes && mtu <= radio::kMaximumFrameBytes;
    }
    template<class Action> bool operation(std::uint64_t now_ms, Action action) {
        if (busy_) { revoked_ = true; return false; }
        if (closed_) return false;
        busy_ = true;
        const bool valid = config_.local_node_id != 0 && config_.peer_node_id != 0 &&
            config_.network_id != 0 && config_.local_node_id != config_.peer_node_id &&
            now_ms != std::numeric_limits<std::uint64_t>::max() &&
            (!clock_seen_ || now_ms >= last_now_ms_);
        last_now_ms_ = now_ms;
        clock_seen_ = true;
        const bool accepted = valid && fresh() && action() && !revoked_;
        if (!accepted) cleanup();
        busy_ = false;
        return accepted;
    }
    void cleanup() {
        closed_ = true;
        cleanup_ok_ = endpoint_.close() && endpoint_.secrets_cleared();
    }
    IndependentHandshakeEndpoint& endpoint_;
    radio::RadioTransport& transport_;
    const HandshakeTransportConfig config_;
    std::uint64_t last_now_ms_{0};
    bool clock_seen_{false}, busy_{false}, revoked_{false}, closed_{false}, cleanup_ok_{false};
};
} // namespace opentrail::security_evaluation
