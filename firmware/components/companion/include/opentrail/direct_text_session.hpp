#pragma once

#include "opentrail/direct_conversation_owner.hpp"
#include "opentrail/f5_rekey_session.hpp"
#include "opentrail/otxt_payload_codec.hpp"

namespace opentrail::companion {

// The serialized packet owner lends the four stable views of the exact claim.
// A miss returns empty views. It destroys borrowers before retiring the claim.
class DirectTextStoragePort {
public:
    using Lanes = std::array<persistence::PersistentStorage*, 4>;
    virtual ~DirectTextStoragePort() = default;
    virtual Lanes packet_lanes(std::size_t slot, const DirectIdentity& scope,
                               std::uint64_t generation) = 0;
};

// Owns one Packet-v1 text lease and one entry in the shared transport queue.
// The launch caller must check frame_eligible for that exact queued copy.
// A terminal delivery failure retires the Packet lease and requires fresh rekey.
class DirectTextSession final {
    using Rekey = security_evaluation::F5RekeySession;
    using Node = protocol::v1::selected_packet_detail::Node;
    using Frame = protocol::v1::selected_packet_detail::Frame;

    class GuardedTransport final : public radio::RadioTransport {
    public:
        explicit GuardedTransport(DirectTextSession& owner) : owner_(owner) {}
        std::size_t mtu() const override;
        radio::TransportStatus status() const override;
        radio::SendResult send(radio::ByteView, std::uint64_t now) override;
        radio::ReceiveResult receive(radio::MutableByteView) override { return {}; }
        void service(std::uint64_t) override {}
    private:
        DirectTextSession& owner_;
    };

    struct PacketOwners {
        explicit PacketOwners(const DirectTextStoragePort::Lanes& lanes)
            : data_store(*lanes[0]), ack_store(*lanes[1]),
              data_counter(data_store), ack_counter(ack_store),
              data_replay(*lanes[2]), ack_replay(*lanes[3]) {}
        persistence::OutboundCounterLeaseStore data_store, ack_store;
        persistence::OutboundCounterAllocator data_counter, ack_counter;
        security_eval::EvaluationReplayStore data_replay, ack_replay;
        std::optional<Node> node;
    };

public:
    enum class Ingress { refused, delivered, duplicate, acknowledged };
    enum class SendState { idle, queued, acknowledged, closed };

    struct Received {
        Node::MessageIdentity identity{};
        std::array<std::uint8_t, otxt::max_text_bytes> bytes{};
        std::size_t size{};
    };

    DirectTextSession(DirectConversationOwner& owner, const DirectBinding& binding,
                      Rekey& rekey, DirectTextStoragePort& storage,
                      radio::RadioTransport& radio)
        : owner_(owner), binding_(binding), rekey_(rekey), storage_(storage), radio_(radio),
          transport_(*this), delivery_(transport_) {}
    ~DirectTextSession() { close(); }
    DirectTextSession(const DirectTextSession&) = delete;
    DirectTextSession& operator=(const DirectTextSession&) = delete;

    bool start(std::uint64_t now, std::uint64_t deadline);
    bool send_text(std::uint32_t id, radio::ByteView, std::uint64_t now);
    Ingress receive(radio::ByteView frame, std::uint64_t now);
    std::optional<Received> take_received(std::uint64_t now);
    void service(std::uint64_t now);
    bool frame_eligible(radio::ByteView exact_frame, std::uint64_t now);
    SendState send_state() const { return send_state_; }
    void close();

private:
    static bool admit_data(void*, protocol::v1::View);
    static bool packet_permitted(void*, std::uint64_t now);
    bool permitted(std::uint64_t now);
    bool fail() { close(); return false; }

    DirectConversationOwner& owner_;
    const DirectBinding binding_;
    Rekey& rekey_;
    DirectTextStoragePort& storage_;
    radio::RadioTransport& radio_;
    GuardedTransport transport_;
    delivery::DeliveryController delivery_;
    std::optional<DirectTrafficGrant> grant_;
    std::optional<PacketOwners> packet_;
    Frame pending_ack_{}, queued_frame_{};
    std::uint64_t deadline_{}, last_now_{}, data_deadline_{};
    SendState send_state_{SendState::idle};
    bool unread_{}, closed_{};
};

}  // namespace opentrail::companion
